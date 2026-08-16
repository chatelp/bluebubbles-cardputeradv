#include "bb_client.h"
#include "bb_errors.h"
#include "app_config.h"
#include "bb_emoji.h"
#include "bb_streams.h"
#include "i18n.h"
#include "certs.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// Clients TCP réutilisés entre les requêtes (une seule connexion TLS à la
// fois : pas de PSRAM sur l'ESP32-S3FN8, ~45 Ko de heap par session TLS).
static WiFiClientSecure sTls;
static WiFiClient sPlain;
// Les HTTPClient aussi DOIVENT être persistants : un objet local a _host vide,
// et beginInternal() coupe alors la connexion encore ouverte avant chaque
// requête — le keep-alive n'aurait jamais lieu (revue du 2026-08-15).
static HTTPClient sHttpTls;
static HTTPClient sHttpPlain;

// Leçon de docs/02 : le tas fragmenté n'offre que ~31 Ko CONTIGUS en
// fonctionnement. On ne bufferise donc jamais un corps de réponse entier.
// Quand la taille est connue (Content-Length — le cas normal derrière
// Cloudflare, vérifié sur l'appareil), le JSON est parsé directement depuis
// le flux TLS avec le filtre ArduinoJson : seul le résultat filtré (quelques
// Ko, alloué par petits blocs) occupe la mémoire. Le repli tamponné ne sert
// qu'aux réponses chunked, rares, et reste borné.
static const int LEN_SANITY = 200000;         // au-delà, le serveur déraille
static const uint32_t MIN_WORK_HEAP = 16000;  // bloc contigu minimal pour parser

static String i64(int64_t v) {
    char b[24];
    snprintf(b, sizeof(b), "%lld", (long long)v);
    return String(b);
}

static String urlEncode(const String& s) {
    static const char* hex = "0123456789ABCDEF";
    String out;
    out.reserve(s.length() * 3);
    for (size_t i = 0; i < s.length(); i++) {
        char c = s[i];
        if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += c;
        } else {
            out += '%';
            out += hex[(c >> 4) & 0xF];
            out += hex[c & 0xF];
        }
    }
    return out;
}

// Tout texte venant du serveur passe par ici : les espaces insécables du
// français (iMessage en insère avant « ? ! ; : ») n'ont pas de glyphe dans
// efont et s'afficheraient en carrés.
static String srvText(const char* s) {
    if (!s || !*s) return String();
    size_t len = strlen(s);
    std::vector<char> buf(s, s + len + 1);
    size_t n = bbNormalizeSpaces(buf.data(), len);
    buf[n] = 0;
    return String(buf.data());
}

String bbChatKey(const String& chatGuid) {
    char buf[96];
    bbBuildKey(chatGuid.c_str(), buf, sizeof(buf));
    return String(buf);
}

// Vraies valeurs de tapback du sérialiseur : voir bb_tapback.h. Un sticker
// porte aussi associatedMessageGuid mais N'EST PAS une réaction : le filtrer
// ferait disparaître des messages réels de l'affichage.
static bool isTapbackType(const char* t) {
    bool remove;
    return bbTapParseType(t, &remove) >= 0;
}

String BBClient::apiUrl(const String& path) const {
    String url = gConfig.serverUrl + path;
    url += (path.indexOf('?') >= 0) ? '&' : '?';
    url += "password=" + urlEncode(gConfig.serverPass);
    return url;
}

// Source réseau des décodeurs. Ne JAMAIS lire par NetworkClient::readBytes :
// sur TLS, NetworkClientSecure::read() rend -1 dès que available() == 0 —
// même connexion saine, simple creux entre deux records — et readBytes()
// traite ce -1 en erreur fatale (break immédiat, sa boucle d'attente ne vaut
// que pour r == 0). Résultat : toute réponse dépassant le tampon déchiffré
// (~3-4 Ko) « finissait » en plein corps → IncompleteInput (2026-08-16).
// On applique donc ici l'idiome du core lui-même (celui de HTTPClient) :
// available()/connected() + attente courte, borné par une échéance absolue.
struct NetSource {
    WiFiClient& c;
    uint32_t deadline;  // échéance absolue (ms)
    size_t total = 0;   // diagnostic : octets réellement livrés
    uint8_t ring[32];   // diagnostic : fenêtre glissante de fin de flux
    size_t w = 0;
    NetSource(WiFiClient& cl, uint32_t budgetMs) : c(cl), deadline(millis() + budgetMs) {}
    size_t readBytes(char* b, size_t n) {
        while ((int32_t)(millis() - deadline) < 0) {
            int av = c.available();  // peut fermer la connexion si le TLS est mort
            if (av > 0) {
                size_t want = (size_t)av < n ? (size_t)av : n;
                int r = c.read((uint8_t*)b, want);
                if (r > 0) {
                    total += (size_t)r;
                    for (int k = 0; k < r; k++) ring[w++ % sizeof(ring)] = (uint8_t)b[k];
                    return (size_t)r;
                }
            } else if (!c.connected()) {
                return 0;  // vraie fin : plus rien à lire ET connexion close
            }
            delay(2);
        }
        return 0;  // budget épuisé
    }
    // Raccourcit l'échéance (drain : on ne s'éternise pas sur un reliquat).
    void shorten(uint32_t ms) { deadline = millis() + ms; }
    void dump(const char* tag, int expected) const {
        char txt[sizeof(ring) + 1];
        size_t n = w < sizeof(ring) ? w : sizeof(ring);
        for (size_t k = 0; k < n; k++) {
            uint8_t ch = ring[(w - n + k) % sizeof(ring)];
            txt[k] = (ch >= 32 && ch < 127) ? (char)ch : '.';
        }
        txt[n] = 0;
        Serial.printf("[bb] source %s : %u/%d octets livres, fin \"%s\"\n", tag,
                      (unsigned)total, expected, txt);
    }
};
using BoundedStream = BbBoundedStream<NetSource>;
using ChunkedStream = BbChunkedStream<NetSource>;

// Attend et regarde le premier octet du corps sans le consommer : il
// discrimine l'enveloppe réelle quand Content-Length est absent (un corps
// JSON commence par '{' ou '[', une taille de chunk par un chiffre hexa).
static int peekFirstByte(WiFiClient& c, uint32_t budgetMs) {
    uint32_t t0 = millis();
    while (millis() - t0 < budgetMs) {
        if (c.available()) return c.peek();
        if (!c.connected()) return -1;
        delay(5);
    }
    return -1;
}

// ---------------------------------------------------------------------------
// Requête + parse filtré, sans jamais bufferiser le corps entier.
// ---------------------------------------------------------------------------
bool BBClient::requestJson(const char* method, const String& path, const String& jsonBody,
                           JsonDocument& filter, JsonDocument& out, String& err,
                           uint32_t timeoutMs, int* httpCodeOut) {
    if (httpCodeOut) *httpCodeOut = 0;
    err = "";
    if (WiFi.status() != WL_CONNECTED) {
        err = T(S_NO_WIFI);  // cas évident : message nu, sans code
        return false;
    }
    if (ESP.getMaxAllocHeap() < MIN_WORK_HEAP) {
        Serial.printf("[bb] heap contigu %u < %u : requete differee\n",
                      (unsigned)ESP.getMaxAllocHeap(), (unsigned)MIN_WORK_HEAP);
        err = bbErr(BB_E40_DEVICE_MEMORY);
        return false;
    }

    const bool https = gConfig.serverUrl.startsWith("https");
    WiFiClient* raw = https ? (WiFiClient*)&sTls : &sPlain;
    HTTPClient& http = https ? sHttpTls : sHttpPlain;
    http.setConnectTimeout(8000);
    http.setTimeout(timeoutMs);
    // Keep-alive : les clients TCP (sTls/sPlain) sont statiques, la connexion
    // survit d'une requête à l'autre → une seule poignée de main TLS.
    http.setReuse(true);

    bool ok;
    if (https) {
        if (gConfig.tlsVerify) sTls.setCACert(ROOT_CA_BUNDLE);
        else sTls.setInsecure();
        ok = http.begin(sTls, apiUrl(path));
    } else {
        ok = http.begin(sPlain, apiUrl(path));
    }
    if (!ok) {
        err = bbErr(BB_E50_URL);
        return false;
    }

    if (jsonBody.length()) http.addHeader("Content-Type", "application/json");
    int code = (strcmp(method, "POST") == 0) ? http.POST(jsonBody) : http.GET();

    if (code <= 0) {
        // Le détail (refus TCP, poignée de main TLS, coupure…) va en série ;
        // l'utilisateur n'a besoin que de la cause actionnable.
        Serial.printf("[bb] transport %d : %s\n", code,
                      HTTPClient::errorToString(code).c_str());
        err = bbErr(code == HTTPC_ERROR_READ_TIMEOUT ? BB_E13_TIMEOUT
                                                     : BB_E11_UNREACHABLE);
        http.end();
        return false;
    }
    if (httpCodeOut) *httpCodeOut = code;

    int len = http.getSize();
    if (len > LEN_SANITY) {
        Serial.printf("[bb] rejet %d o (aberrant)\n", len);
        err = bbErr(BB_E30_RESPONSE_HUGE, len / 1024);
        raw->stop();  // corps non lu : la connexion est sale
        http.end();
        return false;
    }

    DeserializationError e = DeserializationError::Ok;
    bool clean = true;  // la connexion a-t-elle été entièrement consommée ?

    // Le corps d'erreur BlueBubbles est petit : {status, message, error}.
    JsonDocument efilter;
    efilter["message"] = true;
    efilter["error"]["message"] = true;
    JsonDocument& useFilter = (code >= 400) ? efilter : filter;

    if (len == 0) {
        // Corps vide : rien à parser, `out` reste vide.
    } else if (len > 0) {
        // Enveloppe identity : parse en flux, borné par Content-Length.
        NetSource src(*http.getStreamPtr(), timeoutMs);
        BoundedStream bs(src, (size_t)len, timeoutMs);
        e = deserializeJson(out, bs, DeserializationOption::Filter(useFilter));
        if (e) src.dump("bornee", len);
        src.shorten(3000);  // le reliquat ne mérite pas le budget complet
        clean = bs.drain(3000);
    } else {
        // Longueur inconnue. Deux enveloppes réelles possibles :
        //   chunked (le corps commence par une taille hexa), ou identity sans
        //   Content-Length (corps délimité par la fermeture de connexion).
        // Traiter la seconde comme du chunked faisait lire "{"status"… comme
        // une taille de chunk → « EmptyInput » (2026-08-16).
        WiFiClient* sp = http.getStreamPtr();
        // Le budget d'attente du premier octet suit celui de la requête : un
        // envoi peut légitimement rester 60 s sans réponse (confirmation Apple).
        int first = peekFirstByte(*sp, timeoutMs);
        if (first == '{' || first == '[') {
            // Identity délimitée par la fermeture : borne de sûreté seulement.
            NetSource src(*sp, timeoutMs);
            BoundedStream bs(src, (size_t)LEN_SANITY, timeoutMs);
            e = deserializeJson(out, bs, DeserializationOption::Filter(useFilter));
            if (e && e != DeserializationError::IncompleteInput) src.dump("identity", -1);
            clean = false;  // longueur invérifiable : jamais de réutilisation
        } else if (first < 0) {
            e = DeserializationError::EmptyInput;
            clean = false;
        } else {
            NetSource src(*sp, timeoutMs);
            ChunkedStream cs(src, LEN_SANITY, timeoutMs);
            e = deserializeJson(out, cs, DeserializationOption::Filter(useFilter));
            if (e) src.dump("chunked", -1);
            src.shorten(3000);
            clean = cs.drain(3000) && cs.clean();
        }
    }

    if (e) {
        // Diagnostic complet : ce genre de panne doit se lire en une ligne.
        Serial.printf("[bb] json '%s' code=%d len=%d clean=%d\n",
                      e.c_str(), code, len, clean ? 1 : 0);
    }
    if (!clean) raw->stop();  // reliquat non lu : jamais de keep-alive sale
    http.end();

    if (code >= 400) {
        // Le message du serveur (anglais, technique) va en série ; l'écran
        // reçoit un code stable, documenté dans le README.
        const char* msg = out["error"]["message"] | (const char*)(out["message"] | "");
        if (strlen(msg)) Serial.printf("[bb] http %d : %s\n", code, msg);
        if (code == 401 || code == 403) err = bbErr(BB_E20_AUTH);
        else if (code == 404) err = bbErr(BB_E22_NOT_FOUND);
        else if (code >= 500) err = bbErr(BB_E21_SERVER, code);
        else err = bbErr(BB_E23_HTTP, code);
        out.clear();
        return false;
    }
    if (e) {
        err = bbErr(BB_E31_RESPONSE_BAD);  // le détail du parse est déjà en série
        return false;
    }
    if (out.overflowed()) {
        Serial.println("[bb] document JSON hors budget");
        err = bbErr(BB_E32_RESPONSE_MEMORY);
        return false;
    }
    return true;
}

bool BBClient::ping(String& err) {
    JsonDocument filter;
    filter["message"] = true;
    JsonDocument doc;
    return requestJson("GET", "/api/v1/ping", "", filter, doc, err);
}

bool BBClient::fetchMessagesPage(int64_t beforeMs, int64_t afterMs, uint8_t limit,
                                 std::vector<BBRecent>& out, size_t& rawOut,
                                 int64_t& rawOldestOut, String& err) {
    rawOut = 0;
    rawOldestOut = 0;

    JsonDocument filter;
    JsonObject f = filter["data"].add<JsonObject>();
    f["guid"] = true;
    f["text"] = true;
    f["dateCreated"] = true;
    f["isFromMe"] = true;
    f["associatedMessageGuid"] = true;
    f["associatedMessageType"] = true;
    f["itemType"] = true;
    f["dateRetracted"] = true;
    f["attachments"][0]["guid"] = true;
    JsonObject fc = f["chats"].add<JsonObject>();
    fc["guid"] = true;
    fc["displayName"] = true;
    fc["chatIdentifier"] = true;

    // Sans "attachment" dans with, le serveur n'embarque pas le tableau et
    // « [pièce jointe] » ne pourrait jamais s'afficher.
    String body = String("{\"limit\":") + limit +
                  ",\"offset\":0,\"with\":[\"chats\",\"attachment\"],\"sort\":\"DESC\"";
    if (beforeMs > 0) body += ",\"before\":" + i64(beforeMs);
    if (afterMs > 0) body += ",\"after\":" + i64(afterMs);
    body += "}";

    JsonDocument doc;
    if (!requestJson("POST", "/api/v1/message/query", body, filter, doc, err)) return false;

    out.clear();
    for (JsonObject m : doc["data"].as<JsonArray>()) {
        rawOut++;
        int64_t rawDate = m["dateCreated"] | (int64_t)0;
        if (rawDate) rawOldestOut = rawDate;  // page triée DESC : la dernière vue est la + ancienne
        BBRecent r;
        r.msgGuid = (const char*)(m["guid"] | "");
        JsonObject ch = m["chats"][0];
        r.chatGuid = (const char*)(ch["guid"] | "");
        if (!r.msgGuid.length() || !r.chatGuid.length()) continue;
        r.title = srvText(ch["displayName"] | "");
        if (!r.title.length()) r.title = srvText(ch["chatIdentifier"] | "?");
        r.text = srvText(m["text"] | "");
        r.date = rawDate;
        r.fromMe = m["isFromMe"] | false;
        r.isReaction = !m["associatedMessageGuid"].isNull() &&
                       isTapbackType(m["associatedMessageType"] | "");
        r.isEvent = (m["itemType"] | 0) != 0;
        r.hasAttachment = m["attachments"].as<JsonArray>().size() > 0;
        if (!m["dateRetracted"].isNull()) {
            r.text = T(S_RETRACTED);
            r.hasAttachment = false;
        }
        out.push_back(r);
    }
    return true;
}

bool BBClient::fetchMessages(const String& chatGuid, std::vector<BBMsg>& out,
                             std::vector<BBTap>& taps, uint8_t limit, int64_t afterMs,
                             bool& pageFull, String& err) {
    pageFull = false;
    taps.clear();
    JsonDocument filter;
    JsonObject f = filter["data"].add<JsonObject>();
    f["guid"] = true;
    f["text"] = true;
    f["dateCreated"] = true;
    f["isFromMe"] = true;
    f["itemType"] = true;
    f["associatedMessageGuid"] = true;
    f["associatedMessageType"] = true;
    f["dateRetracted"] = true;
    f["dateEdited"] = true;
    f["attachments"][0]["guid"] = true;
    f["handle"]["address"] = true;

    // Sur-lecture volontaire : les tapbacks et événements de groupe sont des
    // lignes de message comme les autres — sans marge, une conversation dont
    // les dernières lignes sont surtout des réactions s'afficherait vide.
    uint8_t fetchLimit = (limit > 20) ? 30 : limit + 10;
    String path = "/api/v1/chat/" + urlEncode(chatGuid) +
                  "/message?with=handle,attachment&sort=DESC&offset=0&limit=" + fetchLimit;
    if (afterMs > 0) path += "&after=" + i64(afterMs);

    JsonDocument doc;
    if (!requestJson("GET", path, "", filter, doc, err)) return false;

    size_t raw = 0;
    out.clear();
    for (JsonObject m : doc["data"].as<JsonArray>()) {
        raw++;
        // Une réaction n'est pas une bulle : elle est collectée pour être
        // affichée en badge sur son message cible. Les stickers passent
        // (associatedMessageGuid mais pas un type de tapback).
        if (!m["associatedMessageGuid"].isNull()) {
            bool remove = false;
            int type = bbTapParseType(m["associatedMessageType"] | "", &remove);
            if (type >= 0) {
                BBTap t;
                t.guid = (const char*)(m["guid"] | "");
                t.target = bbTapTarget(m["associatedMessageGuid"] | "");
                t.type = (int8_t)type;
                t.remove = remove;
                if (t.guid.length() && t.target.length()) taps.push_back(t);
                continue;
            }
        }
        if ((m["itemType"] | 0) != 0) continue;

        BBMsg msg;
        msg.guid = (const char*)(m["guid"] | "");
        msg.text = srvText(m["text"] | "");
        msg.date = m["dateCreated"] | (int64_t)0;
        msg.fromMe = m["isFromMe"] | false;
        msg.sender = (const char*)(m["handle"]["address"] | "");
        msg.hasAttachment = m["attachments"].as<JsonArray>().size() > 0;
        if (!msg.text.length())
            msg.text = T(msg.hasAttachment ? S_ATTACHMENT : S_NO_TEXT);
        if (!m["dateRetracted"].isNull()) {
            msg.text = T(S_RETRACTED);  // comme iMessage : la bulle disparaît au profit d'une mention
            msg.hasAttachment = false;
        } else if (!m["dateEdited"].isNull()) {
            msg.text += T(S_EDITED);
        }
        if (msg.guid.length()) out.push_back(msg);
    }
    pageFull = raw >= fetchLimit;  // il en reste peut-être : l'appelant recharge en entier
    if (afterMs == 0)
        Serial.printf("[bb] conv : %u lignes, %u messages gardes\n",
                      (unsigned)raw, (unsigned)out.size());
    // La sur-lecture peut ramener plus que demandé. En chargement complet on
    // garde les plus récents ; en incrémental on garde TOUT (l'appelant borne
    // sa vue lui-même) — tronquer perdrait les messages collés au curseur,
    // que le prochain `after` ne redemanderait jamais.
    if (afterMs == 0 && out.size() > limit) out.resize(limit);
    // Le serveur renvoie du plus récent au plus ancien ; on remet en ordre
    // chronologique pour l'affichage — les réactions aussi (un retrait doit
    // s'appliquer APRÈS l'ajout qu'il annule).
    std::reverse(out.begin(), out.end());
    std::reverse(taps.begin(), taps.end());
    return true;
}

BBClient::SendResult BBClient::sendText(const String& chatGuid, const String& text, String& err) {
    JsonDocument doc;
    doc["chatGuid"] = chatGuid;
    doc["tempGuid"] = String("cardputer-") + millis();
    doc["message"] = text;
    doc["method"] = gConfig.sendMethod;
    String body;
    serializeJson(doc, body);

    JsonDocument filter;
    filter["status"] = true;
    JsonDocument resp;
    // Le serveur attend la confirmation d'Apple avant de répondre : jusqu'à
    // ~60 s en private-api. Un timeout court ferait croire à un échec (et
    // renvoyer le brouillon) alors que le message est parti.
    int httpCode = 0;
    if (requestJson("POST", "/api/v1/message/text", body, filter, resp, err, 60000, &httpCode))
        return SEND_OK;
    // Échec CERTAIN : le serveur a répondu un code d'erreur (il a traité et
    // refusé), ou la connexion n'a même pas pu s'établir (rien n'est parti).
    if (httpCode >= 400) return SEND_FAILED;
    if (httpCode == 0 && err.indexOf("refused") >= 0) return SEND_FAILED;
    if (httpCode == 0 && err.indexOf("connection") >= 0 && err.indexOf("lost") < 0)
        return SEND_FAILED;
    // Tout le reste (timeout de lecture, réponse illisible, connexion perdue
    // APRÈS l'envoi) : la requête est très probablement arrivée — ne jamais
    // restaurer le brouillon, le polling confirmera.
    return SEND_UNCONFIRMED;
}
