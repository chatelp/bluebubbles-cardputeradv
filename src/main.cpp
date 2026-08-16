// Silicon Bubbles — client iMessage léger pour M5Stack Cardputer ADV.
// Lecture/envoi de messages via l'API REST d'un serveur BlueBubbles, par
// polling. Configuration par portail web embarqué (voir config_portal.cpp).

#include <M5Cardputer.h>
#include <WiFi.h>
#include <Preferences.h>
#include <time.h>
#include <map>
#include <vector>

#include "app_config.h"
#include "bb_client.h"
#include "bb_emoji.h"
#include "bb_scroll.h"
#include "bb_errors.h"
#include "sd_backup.h"
#include "i18n.h"
#include "ui/render.h"
#include "ui/theme.h"
#include "config_portal.h"
#include "sound.h"

static const char* APP_VERSION = "0.3.0";

// ---------------------------------------------------------------------------
// État global
// ---------------------------------------------------------------------------


// Construit sans parent et créé tardivement dans setup() : objet global,
// M5Cardputer n'est pas encore initialisé ici (même précaution que Daoa Mini).
static M5Canvas sCanvas;
static bool sSpriteOk = false;
static BBClient sClient;


static String sCurChatGuid;
static uint32_t sChatEpoch = 0;  // incrémenté à chaque ouverture : périme les réponses en vol

// Marqueur de rattrapage : date serveur max observée dans les réponses —
// jamais l'horloge du Cardputer (docs/04). Tenu en RAM, persisté avec
// parcimonie (usure NVS).
static int64_t sMarker = 0;
static int64_t sMarkerSaved = 0;
static uint32_t sMarkerSaveMs = 0;
static int64_t sPollBefore = 0;     // curseur de reprise (rattrapage inachevé)
static int64_t sPendingNewest = 0;  // date la plus récente vue pendant ce rattrapage
static uint8_t sPollRounds = 0;     // tours de rattrapage consécutifs incomplets
static int32_t sDecayDay = 0;    // jour (marqueur/86400000) de la dernière décroissance
static volatile bool sCalibPending = false; // calibration en file OU en cours
static bool sListChanged = false;  // liste à repersister (entrée ou aperçu modifié)
// Deux variables séparées : sans ça, un poll global qui réussit effaçait
// l'erreur de la conversation ouverte et laissait un faux « Conversation
// vide » (revue du 2026-08-15).
static uint32_t sLastPollChats = 0;
static uint32_t sLastPollMsgs = 0;
static bool sDirty = true;      // l'écran doit être redessiné

// --- Tâche réseau -----------------------------------------------------------
// Toutes les requêtes HTTP tournent dans une tâche FreeRTOS dédiée : la
// boucle UI ne bloque jamais (une requête TLS prend 0,5 à 3 s). Les
// résultats sont publiés dans les variables partagées sous sDataMux, et la
// boucle principale les affiche au tick suivant.
enum NetCmdType : uint8_t { NET_CALIBRATE, NET_POLL, NET_MSGS, NET_MSGS_INC, NET_SEND, NET_PING,
                            NET_DEBUG };
struct NetCmd {
    NetCmdType type;
    // String de contenu (guid/texte) passée par pointeur : une file FreeRTOS
    // copie des octets bruts, pas des objets C++.
    String* guid = nullptr;
    String* text = nullptr;
};
static QueueHandle_t sNetQueue = nullptr;
static SemaphoreHandle_t sDataMux = nullptr;
static volatile bool sNetBusy = false;
static volatile bool sSendFailed = false;   // l'envoi a échoué : restaurer le brouillon
static volatile bool sNewIncoming = false;  // nouveau message entrant : bip
static String sSendBackup;                  // brouillon en cours d'envoi
static String sSendBackupGuid;              // et sa conversation de destination

// Renvoie false si la file est pleine : l'appelant doit le dire à
// l'utilisateur plutôt que de laisser croire que l'action est partie.
static bool netEnqueue(NetCmdType type, const String& guid = "", const String& text = "") {
    NetCmd cmd;
    cmd.type = type;
    if (guid.length()) cmd.guid = new String(guid);
    if (text.length()) cmd.text = new String(text);
    if (xQueueSend(sNetQueue, &cmd, 0) != pdTRUE) {
        delete cmd.guid;
        delete cmd.text;
        return false;
    }
    return true;
}

// Point d'entrée UNIQUE de la calibration : une seule à la fois. Un double
// appui sur « r » (rebond clavier) mettait deux calibrations en file — la
// première, avortée par la présence de la seconde, remplaçait la liste par
// une page partielle (2026-08-16).
static bool requestCalibration() {
    if (sCalibPending) return true;  // déjà en file ou en cours
    if (!netEnqueue(NET_CALIBRATE)) return false;
    sCalibPending = true;
    return true;
}

struct DataLock {  // verrou RAII sur les données partagées UI <-> réseau
    DataLock() { xSemaphoreTake(sDataMux, portMAX_DELAY); }
    ~DataLock() { xSemaphoreGive(sDataMux); }
};

// Le rendu vit dans src/ui/ (portable appareil/simulateur). Ici : l'état,
// le réseau, la persistance, le clavier — et le modèle qu'on donne au rendu.
static UiModel sUi;
static bool sSetDirty = false;  // une valeur a changé : à enregistrer en sortant

// delta = +1 / -1. Les bornes sont celles du portail : les deux chemins de
// configuration doivent produire exactement les mêmes valeurs valides.
static void setAdjust(uint8_t f, int delta) {
    switch (f) {
        case SET_LANG:
            gConfig.lang = (gConfig.lang == LANG_FR) ? LANG_EN : LANG_FR;
            gLang = gConfig.lang;
            break;
        case SET_VOL: {
            int v = (int)gConfig.sndVolume + delta * 10;
            gConfig.sndVolume = (uint8_t)max(0, min(100, v));
            Snd::applyConfig();  // volume audible immédiatement
            break;
        }
        case SET_KEYS:  gConfig.sndKeys  = !gConfig.sndKeys;  Snd::applyConfig(); break;
        case SET_SEND:  gConfig.sndSend  = !gConfig.sndSend;  Snd::applyConfig(); break;
        case SET_RECV:  gConfig.sndRecv  = !gConfig.sndRecv;  Snd::applyConfig(); break;
        case SET_NOTIF: gConfig.sndNotif = !gConfig.sndNotif; Snd::applyConfig(); break;
        case SET_POLL: {
            int v = (int)gConfig.pollSec + delta * 5;
            gConfig.pollSec = (uint16_t)max(3, min(120, v));
            break;
        }
        case SET_HIST: {
            int v = (int)gConfig.histDepth + delta;
            gConfig.histDepth = (uint8_t)max(4, min(15, v));
            break;
        }
    }
    sSetDirty = true;
}

// ---------------------------------------------------------------------------
// Alias de contacts : le serveur ne fournit pas de noms (l'API contacts est
// facultative côté Mac) — l'utilisateur nomme lui-même une conversation
// (touche « n »), et le nom vit en NVS, prioritaire sur le titre serveur.
// ---------------------------------------------------------------------------
static std::map<String, String> sAlias;

static void aliasLoad() {
    Preferences p;
    if (!p.begin("bbnames", true)) return;
    size_t len = p.getBytesLength("v1");
    if (!len || len > 4096) { p.end(); return; }
    std::vector<uint8_t> b(len);
    p.getBytes("v1", b.data(), len);
    p.end();
    size_t i = 0;
    while (i + 2 <= len) {
        uint8_t kn = b[i++];
        if (i + kn > len) break;
        String k;
        for (uint8_t j = 0; j < kn; j++) k += (char)b[i + j];
        i += kn;
        if (i >= len) break;
        uint8_t vn = b[i++];
        if (i + vn > len) break;
        String v;
        for (uint8_t j = 0; j < vn; j++) v += (char)b[i + j];
        i += vn;
        if (k.length() && v.length()) sAlias[k] = v;
    }
}

static void aliasSave() {
    std::vector<uint8_t> b;
    for (const auto& kv : sAlias) {
        if (kv.first.length() > 40 || !kv.second.length()) continue;
        uint8_t vn = kv.second.length() > 32 ? 32 : (uint8_t)kv.second.length();
        while (vn > 0 && ((uint8_t)kv.second[vn] & 0xC0) == 0x80) vn--;  // frontière UTF-8
        b.push_back((uint8_t)kv.first.length());
        for (size_t j = 0; j < kv.first.length(); j++) b.push_back((uint8_t)kv.first[j]);
        b.push_back(vn);
        for (uint8_t j = 0; j < vn; j++) b.push_back((uint8_t)kv.second[j]);
    }
    Preferences p;
    p.begin("bbnames", false);
    if (b.empty()) p.remove("v1");
    else p.putBytes("v1", b.data(), b.size());
    p.end();
}

static void aliasApply() {
    for (BBChat& c : sUi.chats) {
        auto it = sAlias.find(c.key);
        c.alias = (it != sAlias.end()) ? it->second : String();
    }
}

// Cibles de l'éditeur de texte générique (SCR_TEXT_INPUT).
enum EditTarget : uint8_t { EDIT_NONE, EDIT_WIFI_PASS, EDIT_SRV_URL, EDIT_SRV_PASS, EDIT_ALIAS };
static EditTarget sEditTarget = EDIT_NONE;
static String sEditSsid;    // réseau choisi au scan
static String sEditChatKey; // conversation à renommer
static Screen sEditBack = SCR_SETTINGS;

static void showBootMessage(const String& msg);  // définie plus bas

// Modification réseau/serveur : le chemin du portail — copie, NVS,
// redémarrage. Jamais d'écriture de String dans gConfig à chaud.
static void applyNetworkChange() {
    AppConfig next = gConfig;
    if (sEditTarget == EDIT_WIFI_PASS) {
        next.wifiSsid = sEditSsid;
        next.wifiPass = sUi.editValue;
    } else if (sEditTarget == EDIT_SRV_URL) {
        String u = sUi.editValue;
        while (u.endsWith("/")) u.remove(u.length() - 1);
        if (!u.startsWith("http")) {
            sUi.status = bbErr(BB_E50_URL);
            return;
        }
        next.serverUrl = u;
    } else if (sEditTarget == EDIT_SRV_PASS) {
        next.serverPass = sUi.editValue;
    }
    next.save();
    showBootMessage(T(S_REBOOTING));
    delay(600);
    ESP.restart();
}

static void commitTextInput() {
    if (sEditTarget == EDIT_ALIAS) {
        if (sUi.editValue.length()) sAlias[sEditChatKey] = sUi.editValue;
        else sAlias.erase(sEditChatKey);
        aliasSave();
        sUi.screen = SCR_CHATS;
        return;
    }
    applyNetworkChange();  // ne revient que sur erreur de validation
}

static void openTextInput(EditTarget t, const String& label, const String& value,
                          bool mask, Screen back) {
    sEditTarget = t;
    sEditBack = back;
    sUi.editLabel = label;
    sUi.editValue = value;
    sUi.editMask = mask;
    sUi.status = "";
    sUi.screen = SCR_TEXT_INPUT;
}

static void render() {
    DataLock l;  // la tâche réseau peut modifier chats/messages/statut
    aliasApply();  // bon marché (≤ 20 × ≤ 16 entrées), et toujours juste
    sUi.battery = M5Cardputer.Power.getBatteryLevel();
    sUi.wifiOk = WiFi.status() == WL_CONNECTED;
    sUi.rssi = (int)WiFi.RSSI();
    sUi.ssid = WiFi.SSID();
    sUi.ip = WiFi.localIP().toString();
    sUi.marker = sMarker;
    sUi.version = APP_VERSION;
    sUi.apSsid = ConfigPortal::apSsid();
    sUi.apPass = ConfigPortal::apPass();
    uiRender(sCanvas, sUi);
    if (sSpriteOk) sCanvas.pushSprite(&M5Cardputer.Display, 0, 0);
    sDirty = false;
}

// ---------------------------------------------------------------------------
// Données
// ---------------------------------------------------------------------------

// Travaux exécutés DANS la tâche réseau (jamais dans loop()).

// ---------------------------------------------------------------------------
// Persistance NVS : blob unique des ~15 conversations épinglées (par score) +
// marqueur de rattrapage. Drapeau "ok" remis à vrai en dernier : une coupure
// pendant l'écriture laisse simplement l'appareil « non calibré ».
// ---------------------------------------------------------------------------

static const char* STORE_NS = "bbstore";
static const uint8_t STORE_VER = 3;  // v3 : clés de fusion sans troncature aveugle
// 12 entrées × ~233 o = ~2,8 Ko : sous la limite d'un blob NVS (4 Ko).
static const uint8_t PINNED_MAX = 12;
static const uint8_t LIST_MAX = 20;

// Sous DataLock.
static void storeSave() {
    std::vector<BBChat> byScore = sUi.chats;
    std::sort(byScore.begin(), byScore.end(),
              [](const BBChat& a, const BBChat& b) { return a.score > b.score; });
    if (byScore.size() > PINNED_MAX) byScore.resize(PINNED_MAX);

    std::vector<uint8_t> b;
    b.reserve(3600);
    b.push_back(STORE_VER);
    b.push_back(0);  // nombre d'entrées : renseigné après la boucle
    uint8_t written = 0;
    auto putStr = [&](const String& s, uint8_t maxB) {
        uint8_t n = s.length() > maxB ? maxB : (uint8_t)s.length();
        while (n > 0 && ((uint8_t)s[n] & 0xC0) == 0x80) n--;  // frontière UTF-8
        b.push_back(n);
        for (uint8_t i = 0; i < n; i++) b.push_back((uint8_t)s[i]);
    };
    for (const BBChat& c : byScore) {
        // Un guid tronqué serait inutilisable (chemin d'API, cible d'envoi) :
        // on préfère ne pas persister l'entrée.
        if (c.guid.length() > 96) continue;
        putStr(c.guid, 96);
        putStr(c.key, 40);
        putStr(c.title, 32);
        putStr(c.lastText, 48);
        putStr(c.lastGuid, 48);
        uint64_t d = (uint64_t)c.lastDate;
        for (int i = 0; i < 8; i++) b.push_back((uint8_t)((d >> (8 * i)) & 0xFF));
        b.push_back(c.lastFromMe ? 1 : 0);
        uint32_t sc;
        memcpy(&sc, &c.score, 4);
        for (int i = 0; i < 4; i++) b.push_back((uint8_t)((sc >> (8 * i)) & 0xFF));
        written++;
    }
    b[1] = written;

    Preferences p;
    p.begin(STORE_NS, false);
    p.putBool("ok", false);
    p.putBytes("chats", b.data(), b.size());
    p.putLong64("marker", sMarker);
    p.putBool("ok", true);
    p.end();
    sMarkerSaved = sMarker;
    sMarkerSaveMs = millis();
    sListChanged = false;
}

static void storeSaveMarker() {
    Preferences p;
    p.begin(STORE_NS, false);
    p.putLong64("marker", sMarker);
    p.end();
    sMarkerSaved = sMarker;
    sMarkerSaveMs = millis();
}

// Au boot, avant la tâche réseau : pas de verrou nécessaire.
static bool storeLoad() {
    Preferences p;
    if (!p.begin(STORE_NS, true)) return false;
    bool ok = p.getBool("ok", false);
    size_t len = ok ? p.getBytesLength("chats") : 0;
    if (!ok || len < 2 || len > 4096) {
        p.end();
        return false;
    }
    std::vector<uint8_t> b(len);
    p.getBytes("chats", b.data(), len);
    sMarker = p.getLong64("marker", 0);
    p.end();
    sMarkerSaved = sMarker;
    sDecayDay = (int32_t)(sMarker / 86400000LL);

    size_t i = 0;
    if (b[i++] != STORE_VER) return false;
    uint8_t count = b[i++];
    auto getStr = [&](String& out) -> bool {
        if (i >= b.size()) return false;
        uint8_t n = b[i++];
        if (i + n > b.size()) return false;
        out = "";
        out.reserve(n);
        for (uint8_t k = 0; k < n; k++) out += (char)b[i + k];
        i += n;
        return true;
    };
    std::vector<BBChat> loaded;
    for (uint8_t c = 0; c < count; c++) {
        BBChat chat;
        if (!getStr(chat.guid) || !getStr(chat.key) || !getStr(chat.title) ||
            !getStr(chat.lastText) || !getStr(chat.lastGuid))
            return false;
        if (i + 13 > b.size()) return false;
        uint64_t d = 0;
        for (int k = 0; k < 8; k++) d |= ((uint64_t)b[i + k]) << (8 * k);
        i += 8;
        chat.lastDate = (int64_t)d;
        chat.lastFromMe = b[i++] != 0;
        uint32_t sc = 0;
        for (int k = 0; k < 4; k++) sc |= ((uint32_t)b[i + k]) << (8 * k);
        i += 4;
        memcpy(&chat.score, &sc, 4);
        loaded.push_back(chat);
    }
    std::sort(loaded.begin(), loaded.end(),
              [](const BBChat& a, const BBChat& b2) { return a.lastDate > b2.lastDate; });
    sUi.chats = loaded;
    for (const BBChat& c : sUi.chats) sUi.seen[c.key] = c.lastDate;  // rien de « nouveau » au boot
    return !sUi.chats.empty();
}

// ---------------------------------------------------------------------------
// Fusion et score (docs/04) : une personne = une entrée (clé bbChatKey),
// score = fréquence pondérée par récence, guid canonique = fil du message le
// plus récent (jamais vide à l'ouverture, bonne cible de réponse).
// ---------------------------------------------------------------------------

static float recencyWeight(int64_t dateMs, int64_t nowMs) {
    float days = (float)((nowMs - dateMs) / 86400000.0);
    if (days < 0) days = 0;
    return 1.0f / (1.0f + days / 7.0f);  // 1 aujourd'hui, ~0,5 à J+7, ~0,2 à J+30
}

static String previewOf(const BBRecent& r) {
    if (r.text.length()) {
        String t = r.text;
        t.replace("\n", " ");
        return t;
    }
    return T(r.hasAttachment ? S_ATTACHMENT : S_NO_TEXT);
}

// Intègre un message récent dans `list`. `track` : met à jour l'état non-lu
// et le bip (faux pendant la calibration, qui construit une liste détachée).
// Appelée sous DataLock quand list == sUi.chats.
static void absorbInto(std::vector<BBChat>& list, const BBRecent& r, int64_t nowMs,
                       bool detached) {
    if (r.isEvent) return;  // les événements n'alimentent que le marqueur

    String key = bbChatKey(r.chatGuid);
    BBChat* entry = nullptr;
    for (BBChat& c : list)
        if (c.key == key) { entry = &c; break; }

    // Réactions : ni entrée, ni remontée, ni aperçu, ni score (docs/04,
    // point 8). Leur date sert uniquement à faire avancer le marqueur, ce
    // dont s'occupe l'appelant.
    if (r.isReaction) return;

    if (!entry) {
        BBChat c;
        c.key = key;
        c.guid = r.chatGuid;
        c.title = r.title;
        c.lastText = previewOf(r);
        c.lastGuid = r.msgGuid;
        c.lastDate = r.date;
        c.lastFromMe = r.fromMe;
        c.score = recencyWeight(r.date, nowMs);
        list.push_back(c);
        if (detached) return;
        sListChanged = true;
        if (!sUi.seen.count(key)) {
            if (r.fromMe) sUi.seen[key] = r.date;
            else { sUi.seen[key] = 0; sNewIncoming = true; }
        }
        return;
    }

    // Le poll interroge avec after = marqueur - 1 : le message qui a fixé le
    // marqueur revient à CHAQUE cycle. Sans cette garde, son score enflerait
    // indéfiniment et cette conversation resterait épinglée sans rien vivre.
    bool isNew = r.date > entry->lastDate ||
                 (r.date == entry->lastDate && r.msgGuid != entry->lastGuid);
    if (!isNew) return;

    entry->score += recencyWeight(r.date, nowMs);
    if (r.date >= entry->lastDate) {  // garde : la date ne recule jamais
        entry->lastDate = r.date;
        entry->lastGuid = r.msgGuid;
        entry->guid = r.chatGuid;    // fil canonique = le plus récemment actif
        entry->lastText = previewOf(r);
        entry->lastFromMe = r.fromMe;
        if (r.title.length() && r.title != "?") entry->title = r.title;
        // L'aperçu a changé : le blob NVS doit suivre, sinon le prochain
        // démarrage réaffiche l'aperçu figé à la dernière calibration alors
        // que la conversation a vécu depuis (2026-08-16).
        if (!detached) sListChanged = true;
        if (!detached && !r.fromMe) {
            auto it = sUi.seen.find(key);
            if (it == sUi.seen.end() || r.date > it->second) sNewIncoming = true;
        }
    }
}

// Sous DataLock : intègre dans la liste affichée.
static void absorb(const BBRecent& r, int64_t nowMs, bool quiet) {
    absorbInto(sUi.chats, r, nowMs, quiet);
}

// Tri par activité + éviction — mais jamais d'une épinglée (top-15 par
// score) : une conversation importante silencieuse reste listée. Sous DataLock.
static void sortAndTrim() {
    std::sort(sUi.chats.begin(), sUi.chats.end(),
              [](const BBChat& a, const BBChat& b) { return a.lastDate > b.lastDate; });
    if (sUi.chats.size() > LIST_MAX) {
        // Ensemble explicite des épinglées (top PINNED_MAX par score, ex æquo
        // départagés par la date) : un seuil scalaire laisserait le repli
        // final tronquer une épinglée en cas d'égalité de score.
        std::vector<int> idx(sUi.chats.size());
        for (size_t i = 0; i < idx.size(); i++) idx[i] = (int)i;
        std::sort(idx.begin(), idx.end(), [](int a, int b) {
            if (sUi.chats[a].score != sUi.chats[b].score) return sUi.chats[a].score > sUi.chats[b].score;
            return sUi.chats[a].lastDate > sUi.chats[b].lastDate;
        });
        std::vector<bool> pinned(sUi.chats.size(), false);
        for (size_t i = 0; i < idx.size() && i < PINNED_MAX; i++) pinned[idx[i]] = true;
        for (int i = (int)sUi.chats.size() - 1; i >= 0 && sUi.chats.size() > LIST_MAX; i--)
            if (!pinned[i]) {
                sUi.chats.erase(sUi.chats.begin() + i);
                pinned.erase(pinned.begin() + i);
            }
    }
    if (sUi.chatSel >= (int)sUi.chats.size()) sUi.chatSel = max(0, (int)sUi.chats.size() - 1);
}

// Décroissance quotidienne des scores, cadencée par le marqueur (horloge
// serveur). Sous DataLock.
static void maybeDecay() {
    int32_t day = (int32_t)(sMarker / 86400000LL);
    if (sDecayDay == 0) { sDecayDay = day; return; }
    bool changed = false;
    while (day > sDecayDay) {
        for (BBChat& c : sUi.chats) c.score *= 0.95f;
        sDecayDay++;
        changed = true;
    }
    if (changed) storeSave();
}

// ---------------------------------------------------------------------------
// Calibration : balayage des ~500 derniers messages (ou 30 jours) par
// curseurs `before` — stables face aux insertions, contrairement à offset.
// La liste se construit à l'écran page après page.
// ---------------------------------------------------------------------------

static void netWorkCalibrate() {
    // Pages de 10 (~18 Ko de corps), parsées en flux : la profondeur du
    // balayage ne coûte AUCUNE mémoire, seulement du temps. La fenêtre
    // s'arrête donc au premier critère atteint : liste PLEINE (LIST_MAX
    // conversations distinctes — c'est ce que l'utilisateur veut), ou
    // 1000 messages, ou 90 jours. L'ancienne fenêtre de 300 messages datait
    // d'un bug de transport corrigé depuis (docs/02, piège readBytes) et
    // laissait des listes de 4 conversations (constat PO 2026-08-16).
    const uint8_t PAGE = 10;
    const uint8_t MAX_PAGES = 100;
    const int64_t WINDOW_MS = 90LL * 86400000LL;

    sUi.calibrating = true;
    sUi.calibPage = 1;
    sUi.calibTotal = MAX_PAGES;
    {
        DataLock l;
        sUi.status = "";
        sDirty = true;
    }

    // La calibration travaille sur une liste NEUVE et ne remplace la liste
    // affichée qu'une fois réussie : un échec réseau ne doit jamais laisser
    // l'appareil sans conversations.
    std::vector<BBChat> built;
    String err;
    int64_t before = 0;
    int64_t prevBefore = -1;  // détecte un curseur immobile (page sans dates)
    int64_t newest = 0;
    bool aborted = false;  // balayage écourté (erreur, action utilisateur)
    std::vector<String> prevGuids;  // dédup des lignes frontière entre pages

    for (uint8_t page = 0; page < MAX_PAGES; page++) {
        std::vector<BBRecent> batch;
        size_t raw = 0;
        int64_t rawOldest = 0;
        if (!sClient.fetchMessagesPage(before, 0, PAGE, batch, raw, rawOldest, err)) {
            if (page == 0) {
                DataLock l;
                sUi.status = err;  // liste intacte
                sUi.calibrating = false;
                sCalibPending = false;
                sDirty = true;
                return;
            }
            aborted = true;
            break;  // on calibre avec ce qu'on a
        }
        if (raw == 0) break;
        if (!newest && !batch.empty()) newest = batch.front().date;

        for (const BBRecent& r : batch) {
            bool dup = false;
            for (const String& g : prevGuids)
                if (g == r.msgGuid) { dup = true; break; }
            if (!dup) absorbInto(built, r, newest, true);
        }
        sUi.calibPage = min((int)page + 2, (int)MAX_PAGES);
        { DataLock l; sDirty = true; }
        prevGuids.clear();
        for (const BBRecent& r : batch) prevGuids.push_back(r.msgGuid);

        // +1 ms : le serveur tronque les dates à la milliseconde ; une borne
        // exacte exclurait les messages partageant la milliseconde frontière
        // (même marge que after = marqueur - 1). La dédup par guid absorbe
        // le chevauchement.
        if (rawOldest) before = rawOldest + 1;
        else if (!batch.empty()) before = batch.back().date + 1;
        if (raw < PAGE) break;
        if (before == prevBefore) break;  // curseur immobile : on relirait la même fenêtre
        prevBefore = before;
        if (newest && before && newest - before > WINDOW_MS) break;
        if ((int)built.size() >= LIST_MAX) break;  // liste pleine : mission accomplie
        if (uxQueueMessagesWaiting(sNetQueue) > 0) { aborted = true; break; }  // l'utilisateur attend
    }
    if (built.empty()) {
        DataLock l;
        sUi.status = T(S_NO_CHATS);
        sUi.calibrating = false;
        sCalibPending = false;
        sDirty = true;
        return;
    }

    DataLock l;
    if (aborted) {
        // Balayage écourté : FUSION avec la liste existante, jamais de
        // remplacement — une calibration interrompue ne doit pas faire
        // disparaître des conversations déjà connues (liste réduite à une
        // page, 2026-08-16).
        for (const BBChat& c : sUi.chats) {
            bool present = false;
            for (const BBChat& b : built)
                if (b.key == c.key) { present = true; break; }
            if (!present) built.push_back(c);
        }
    }
    sUi.chats = built;
    sUi.seen.clear();
    for (const BBChat& c : sUi.chats) sUi.seen[c.key] = c.lastDate;  // calibration = tout lu
    sortAndTrim();
    sUi.chatSel = 0;
    sUi.chatTop = 0;
    if (newest > sMarker) sMarker = newest;
    sPollBefore = 0;
    sPendingNewest = 0;
    sPollRounds = 0;
    sDecayDay = (int32_t)(sMarker / 86400000LL);
    storeSave();
    sUi.synced = true;
    sUi.calibrating = false;
    sCalibPending = false;
    sUi.status = "";
    sDirty = true;
}

// ---------------------------------------------------------------------------
// Polling incrémental : tout ce qui est plus récent que le marqueur, paginé
// jusqu'à la borne (une rafale ne perd rien) — après quoi seulement le
// marqueur avance. after = marqueur - 1 + dédup par guid : les égalités de
// milliseconde ne perdent ni ne dupliquent rien.
// ---------------------------------------------------------------------------

static void netWorkPoll() {
    const uint8_t PAGE = 10;
    const uint8_t MAX_PAGES = (sMarker > 0) ? 5 : 1;

    String err;
    int64_t after = sMarker > 0 ? sMarker - 1 : 0;
    int64_t before = sPollBefore;      // reprise d'un rattrapage inachevé
    int64_t prevBefore = -1;
    int64_t newest = sPendingNewest;   // date la plus récente vue depuis son début
    bool complete = false;
    std::vector<String> prevGuids;     // dédup des lignes frontière entre pages

    for (uint8_t page = 0; page < MAX_PAGES; page++) {
        std::vector<BBRecent> batch;
        size_t raw = 0;
        int64_t rawOldest = 0;
        if (!sClient.fetchMessagesPage(before, after, PAGE, batch, raw, rawOldest, err)) {
            DataLock l;
            sUi.status = err;
            sDirty = true;
            return;
        }
        // `raw` = lignes réellement renvoyées par le serveur (batch peut être
        // plus court après filtrage) : c'est lui qui dit si la page est pleine.
        bool last = raw < PAGE;
        if (!batch.empty() && batch.front().date > newest) newest = batch.front().date;
        // Le curseur suit la dernière ligne BRUTE : une page entièrement
        // filtrée doit quand même faire progresser la descente.
        if (rawOldest) before = rawOldest + 1;       // +1 ms : cf. calibration
        else if (!batch.empty()) before = batch.back().date + 1;
        else if (!last) break;  // rien d'exploitable et pas de curseur : on s'arrête
        if (before == prevBefore) break;  // curseur immobile
        prevBefore = before;

        {
            // Absorption page par page : jamais plus d'une page en mémoire.
            // absorb() a une garde « la date ne recule jamais » : réabsorber
            // un message déjà vu est sans effet sur la liste.
            DataLock l;
            for (const BBRecent& r : batch) {
                bool dup = false;
                for (const String& g : prevGuids)
                    if (g == r.msgGuid) { dup = true; break; }
                if (!dup) absorb(r, newest ? newest : sMarker, false);
            }
            sortAndTrim();
            sDirty = true;
        }
        prevGuids.clear();
        for (const BBRecent& r : batch) prevGuids.push_back(r.msgGuid);

        if (last) { complete = true; break; }
    }
    if (MAX_PAGES == 1) complete = true;  // premier contact : la page 1 fait référence

    DataLock l;
    if (complete) {
        // La borne est atteinte : seulement maintenant le marqueur avance.
        if (newest > sMarker) sMarker = newest;
        sPollBefore = 0;
        sPendingNewest = 0;
        sPollRounds = 0;
        sUi.status = "";
    } else {
        // Rafale plus large que 5 pages : on garde le marqueur en arrière et on
        // reprendra au curseur au prochain poll. Au-delà de ~300 messages de
        // retard, une recalibration coûte moins cher que la descente.
        sPollBefore = before;
        sPendingNewest = newest;
        if (++sPollRounds >= 4) {
            sPollBefore = 0;
            sPendingNewest = 0;
            sPollRounds = 0;
            sUi.status = T(S_CATCHUP_RECALIB);
            requestCalibration();
        } else {
            sUi.status = T(S_CATCHUP);
        }
    }
    maybeDecay();
    if (sListChanged) storeSave();  // entrée ou aperçu modifié : blob à jour
    sUi.synced = true;
    sDirty = true;
}

// incremental : ne rapatrie que les messages plus récents que le dernier
// affiché (réponse minuscule quand rien de neuf) ; sinon charge la
// conversation à la profondeur configurée dans le portail.
// Réactions déjà appliquées à la vue (guids des lignes réaction) : le curseur
// incrémental ne dépasse jamais la dernière BULLE, donc une réaction plus
// récente qu'elle est re-servie à chaque poll — sans cette mémoire, chaque
// rafraîchissement regonflerait les compteurs.
static std::vector<String> sTapSeen;

static void applyTaps(const std::vector<BBTap>& taps) {
    for (const BBTap& t : taps) {
        bool seen = false;
        for (const String& g : sTapSeen)
            if (g == t.guid) { seen = true; break; }
        if (seen) continue;
        sTapSeen.push_back(t.guid);
        if (sTapSeen.size() > 48) sTapSeen.erase(sTapSeen.begin());
        for (BBMsg& m : sUi.msgs)
            if (m.guid == t.target) {
                bbTapApply(m.taps, t.type, t.remove);
                sDirty = true;
                break;
            }
    }
}

static void netWorkMsgs(const String& guid, bool incremental) {
    std::vector<BBMsg> msgs;
    std::vector<BBTap> taps;
    String err;
    int64_t after = 0;
    uint8_t limit;
    uint32_t epoch;
    {
        DataLock l;
        epoch = sChatEpoch;
        if (incremental && sCurChatGuid == guid && !sUi.msgs.empty())
            after = sUi.msgs.back().date - 1;
        else
            incremental = false;
        limit = incremental ? 20 : gConfig.histDepth;
    }
    bool pageFull = false;
    bool ok = sClient.fetchMessages(guid, msgs, taps, limit, after, pageFull, err);
    if (ok && incremental && pageFull) {
        // Rafale plus large que la page : l'incrémental laisserait un trou.
        // On recharge la conversation en entier (fenêtre glissante, docs/06).
        incremental = false;
        ok = sClient.fetchMessages(guid, msgs, taps, gConfig.histDepth, 0, pageFull, err);
    }

    DataLock l;
    if (!ok) {
        if (sChatEpoch == epoch) {
            // Le fil n'existe plus côté Mac : le dire en français, et ne pas
            // laisser l'ancien contenu passer pour vivant.
            if (err.startsWith("E22")) {
                sUi.msgs.clear();
                sUi.statusView = T(S_CHAT_DELETED);
            } else {
                sUi.statusView = err;
            }
        }
        sDirty = true;
        return;
    }
    // L'époque protège du cas « on ressort puis on rouvre la même
    // conversation » : le guid seul ne suffirait pas à détecter la péremption.
    if (sChatEpoch != epoch || sCurChatGuid != guid) return;
    bool grew = false;
    int added = 0;  // bulles ajoutées en bas (décalage des arrêts de défilement)
    if (!incremental) {
        grew = msgs.size() != sUi.msgs.size() ||
               (msgs.size() && sUi.msgs.size() && msgs.back().guid != sUi.msgs.back().guid);
        sUi.msgs = msgs;
        sTapSeen.clear();  // vue reconstruite : les compteurs repartent de zéro
    } else {
        for (const BBMsg& m : msgs) {
            bool dup = false;
            for (BBMsg& e : sUi.msgs)
                if (e.guid == m.guid) {
                    dup = true;
                    // Message réapparu avec un texte différent : édition ou
                    // retrait — mise à jour en place, sans toucher au défilement.
                    if (e.text != m.text) { e.text = m.text; sDirty = true; }
                    break;
                }
            if (!dup) { sUi.msgs.push_back(m); grew = true; added++; }
        }
        const size_t CAP = 30;  // borne mémoire de la vue
        if (sUi.msgs.size() > CAP) sUi.msgs.erase(sUi.msgs.begin(), sUi.msgs.begin() + (sUi.msgs.size() - CAP));
    }
    // Recalage sur le dernier message SEULEMENT si l'on y était déjà : arracher
    // le lecteur au milieu de l'historique parce qu'un message arrive serait
    // hostile. Sinon on décale l'index d'autant d'arrêts que de bulles ajoutées
    // en bas, pour garder la même bulle sous les yeux.
    if (grew) sUi.msgScroll = (sUi.msgScroll > 0) ? sUi.msgScroll + added : 0;
    applyTaps(taps);
    if (!sUi.msgs.empty()) sUi.seen[sUi.curChatKey] = sUi.msgs.back().date;
    sUi.statusView = "";
    sDirty = true;
}

static void netWorkSend(const String& guid, const String& text) {
    String err;
    BBClient::SendResult r = sClient.sendText(guid, text, err);
    if (r == BBClient::SEND_OK) {
        Snd::play(Snd::SENT);  // à la confirmation du serveur, pas à la frappe
        netWorkMsgs(guid, true);
    } else if (r == BBClient::SEND_UNCONFIRMED) {
        // La requête est très probablement arrivée : restaurer le brouillon
        // risquerait un double envoi. Le polling confirmera.
        DataLock l;
        sUi.statusView = T(S_SEND_UNCONFIRMED);
        sDirty = true;
    } else {
        Snd::play(Snd::ERROR);
        DataLock l;
        sUi.statusView = String(T(S_SEND_FAILED)) + err;
        sSendFailed = true;  // échec certain : on rend son brouillon
        sDirty = true;
    }
}

// Diagnostic LAN (voir config_portal.h) : résultat du dernier test.
static String sDbgResult;

static void netWorkDebug(const String& guid) {
    std::vector<BBMsg> msgs;
    std::vector<BBTap> taps;
    String err;
    bool pageFull = false;
    uint32_t t0 = millis();
    bool ok = sClient.fetchMessages(guid, msgs, taps, gConfig.histDepth, 0, pageFull, err);
    uint32_t ms = millis() - t0;
    err.replace("\"", "'");
    DataLock l;
    sDbgResult = String("{\"ok\":") + (ok ? "true" : "false") +
                 ",\"messages\":" + msgs.size() + ",\"taps\":" + taps.size() +
                 ",\"ms\":" + ms + ",\"err\":\"" + err + "\"}";
}

String appDebugConvRun(int index) {
    DataLock l;
    if (index < 0 || index >= (int)sUi.chats.size())
        return "{\"queued\":false,\"raison\":\"index hors liste\"}";
    if (!netEnqueue(NET_DEBUG, sUi.chats[index].guid))
        return "{\"queued\":false,\"raison\":\"file pleine\"}";
    sDbgResult = "{\"etat\":\"en cours\"}";
    return String("{\"queued\":true,\"index\":") + index + "}";
}

String appDebugConvResult() {
    DataLock l;
    return sDbgResult.length() ? sDbgResult : "{\"etat\":\"aucun test\"}";
}

// Déclenche une recalibration complète (équivalent de la touche « r »).
String appDebugCalibRun() {
    if (!requestCalibration())
        return "{\"queued\":false,\"raison\":\"file pleine\"}";
    return "{\"queued\":true,\"type\":\"calibration\"}";
}

static void netTask(void*) {
    NetCmd cmd;
    for (;;) {
        if (xQueueReceive(sNetQueue, &cmd, portMAX_DELAY) != pdTRUE) continue;
        sNetBusy = true;
        switch (cmd.type) {
            case NET_CALIBRATE: netWorkCalibrate(); break;
            case NET_POLL:      netWorkPoll(); break;
            case NET_MSGS:      if (cmd.guid) netWorkMsgs(*cmd.guid, false); break;
            case NET_MSGS_INC:  if (cmd.guid) netWorkMsgs(*cmd.guid, true); break;
            case NET_DEBUG:     if (cmd.guid) netWorkDebug(*cmd.guid); break;
            case NET_SEND:      if (cmd.guid && cmd.text) netWorkSend(*cmd.guid, *cmd.text); break;
            case NET_PING: {
                String err;
                bool ok = sClient.ping(err);
                DataLock l;
                sUi.status = ok ? String(T(S_SERVER_OK)) : (String(T(S_ERROR_PREFIX)) + err);
                sDirty = true;
                break;
            }
        }
        delete cmd.guid;
        delete cmd.text;
        sNetBusy = false;
    }
}

// Côté UI : de simples mises en file, jamais bloquantes.

static void pollChats(bool force = false) {
    if (!force && millis() - sLastPollChats < (uint32_t)gConfig.pollSec * 1000) return;
    if (sNetBusy || uxQueueMessagesWaiting(sNetQueue) > 0) return;  // pas de backlog
    sLastPollChats = millis();
    netEnqueue(NET_POLL);
}

static uint8_t sMsgPollN = 0;
static void pollMessages(bool force = false) {
    if (!force && millis() - sLastPollMsgs < (uint32_t)gConfig.pollSec * 1000) return;
    if (!force && (sNetBusy || uxQueueMessagesWaiting(sNetQueue) > 0)) return;
    sLastPollMsgs = millis();
    // Une édition ou un retrait ne change pas dateCreated : l'incrémental ne
    // les revoit jamais. Un rechargement complet périodique (~1/min), et
    // seulement quand la vue est calée en bas, les rattrape sans à-coup.
    bool full = (sUi.msgScroll == 0) && (++sMsgPollN >= 6);
    if (full) sMsgPollN = 0;
    netEnqueue(full ? NET_MSGS : NET_MSGS_INC, sCurChatGuid);
}

// Appelé sous DataLock (depuis handleKeys).
static void openChat(int idx) {
    if (idx < 0 || idx >= (int)sUi.chats.size()) return;
    sCurChatGuid = sUi.chats[idx].guid;
    sUi.curChatKey = sUi.chats[idx].key;
    // L'alias local prime partout — barre de titre et étiquettes QUI compris.
    sUi.curChatTitle = sUi.chats[idx].alias.length() ? sUi.chats[idx].alias
                                                     : sUi.chats[idx].title;
    sUi.msgs.clear();
    sUi.msgScroll = 0;
    sMsgPollN = 0;  // le cycle de rechargement complet suit la conversation affichée
    sChatEpoch++;  // toute réponse réseau d'une ouverture précédente est périmée
    sUi.screen = SCR_MESSAGES;
    sUi.seen[sUi.curChatKey] = sUi.chats[idx].lastDate;
    sLastPollMsgs = millis();
    sUi.statusView = netEnqueue(NET_MSGS, sCurChatGuid) ? T(S_LOADING)
                                                     : T(S_BUSY_RETRY);
    if (sMarker != sMarkerSaved) storeSaveMarker();  // « événement visible » : on persiste
    sDirty = true;
}

// Appelé sous DataLock (depuis handleKeys). L'envoi part en tâche de fond ;
// le brouillon est restauré si l'envoi échoue.
static void sendCompose() {
    if (!sUi.compose.length()) return;
    if (!netEnqueue(NET_SEND, sCurChatGuid, sUi.compose)) {
        sUi.statusView = T(S_BUSY_RETRY);  // le brouillon reste à l'écran
        sDirty = true;
        return;
    }
    sSendBackup = sUi.compose;
    sSendBackupGuid = sCurChatGuid;  // le brouillon appartient à CETTE conversation
    sUi.compose = "";
    sUi.screen = SCR_MESSAGES;
    sUi.statusView = T(S_SENDING);
    if (sMarker != sMarkerSaved) storeSaveMarker();
    sDirty = true;
}

// ---------------------------------------------------------------------------
// Clavier
// ---------------------------------------------------------------------------

static void handleKeys() {
    if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) return;
    Keyboard_Class::KeysState ks = M5Cardputer.Keyboard.keysState();
    Snd::play(Snd::KEY);  // avant tout traitement : le retour doit être immédiat
    DataLock l;  // navigation et composition touchent les données partagées
    sDirty = true;

    if (sUi.screen == SCR_TEXT_INPUT) {
        if (ks.enter) { commitTextInput(); return; }
        if (ks.del && sUi.editValue.length()) {
            int i = sUi.editValue.length() - 1;
            while (i > 0 && (sUi.editValue[i] & 0xC0) == 0x80) i--;
            sUi.editValue = sUi.editValue.substring(0, i);
            return;
        }
        for (char c : ks.word) {
            if (c == '`') { sUi.screen = sEditBack; sUi.status = ""; return; }
            sUi.editValue += c;
        }
        return;
    }

    if (sUi.screen == SCR_COMPOSE) {
        if (ks.enter) { sendCompose(); return; }
        if (ks.del && sUi.compose.length()) {
            // Retire un caractère UTF-8 complet.
            int i = sUi.compose.length() - 1;
            while (i > 0 && (sUi.compose[i] & 0xC0) == 0x80) i--;
            sUi.compose = sUi.compose.substring(0, i);
            return;
        }
        for (char c : ks.word) {
            if (c == '`') { sUi.screen = SCR_MESSAGES; sUi.statusView = ""; return; }
            sUi.compose += c;
        }
        return;
    }

    for (char c : ks.word) {
        switch (sUi.screen) {
            case SCR_CHATS:
                if (sUi.calibrating) break;  // modal : clavier ignoré le temps du balayage
                if (c == ';' && sUi.chatSel > 0) sUi.chatSel--;
                else if (c == '.' && sUi.chatSel < (int)sUi.chats.size() - 1) sUi.chatSel++;
                else if (c == '`') sUi.screen = SCR_INFO;
                else if (c == 'n' && !sUi.chats.empty()) {
                    // Nom local : le serveur ne fournit pas de contacts.
                    const BBChat& cc = sUi.chats[sUi.chatSel];
                    sEditChatKey = cc.key;
                    openTextInput(EDIT_ALIAS, String(T(S_CONTACT_NAME)) + " - " + cc.title,
                                  cc.alias, false, SCR_CHATS);
                }
                else if (c == 'r')
                    sUi.status = requestCalibration() ? T(S_CALIBRATING) : T(S_BUSY_RETRY);
                break;
            case SCR_MESSAGES:
                if (c == ';') sUi.msgScroll = min(sUi.msgScroll + 1, sUi.msgStops - 1);
                else if (c == '.') sUi.msgScroll = max(0, sUi.msgScroll - 1);
                else if (c == '`') { sUi.screen = SCR_CHATS; pollChats(true); }
                break;
            case SCR_ABOUT:
                if (c == '`') sUi.screen = SCR_INFO;
                break;
            case SCR_INFO:
                if (c == '`') sUi.screen = SCR_CHATS;
                else if (c == 'a') { sUi.screen = SCR_ABOUT; sUi.status = ""; }
                else if (c == 'p')
                    sUi.status = netEnqueue(NET_PING) ? T(S_TESTING_SERVER) : T(S_BUSY_RETRY);
                else if (c == 's') { sUi.screen = SCR_SETTINGS; sUi.setSel = 0; sUi.status = ""; }
                break;
            case SCR_QR:
                if (c == '`') sUi.screen = sEditBack;
                break;
            case SCR_WIFI_SCAN:
                if (c == ';' && sUi.scanSel > 0) sUi.scanSel--;
                else if (c == '.' && sUi.scanSel < (int)sUi.nets.size() - 1) sUi.scanSel++;
                else if (c == 'r' && !sUi.scanning) {
                    sUi.nets.clear();
                    sUi.scanning = true;
                    WiFi.scanNetworks(true);
                } else if (c == '`') {
                    WiFi.scanDelete();
                    sUi.scanning = false;
                    sUi.screen = SCR_SETTINGS;
                }
                break;
            case SCR_SETUP:
                if (c == 'b' && sUi.sdBackup) {
                    String err;
                    if (sdBackupRestore(err)) {
                        showBootMessage(T(S_REBOOTING));
                        delay(600);
                        ESP.restart();
                    }
                    sUi.status = err;
                }
                break;
            case SCR_SETTINGS:
                // ; / . parcourent les champs, , et / changent la valeur.
                if (c == ';' && sUi.setSel > 0) sUi.setSel--;
                else if (c == '.' && sUi.setSel < (int)SET_COUNT - 1) sUi.setSel++;
                else if (c == ',') setAdjust(sUi.setSel, -1);
                else if (c == '/') setAdjust(sUi.setSel, +1);
                else if (c == '`') {
                    // Enregistrement à la sortie : une écriture NVS par visite,
                    // pas une par appui de touche (usure).
                    if (sSetDirty) {
                        gConfig.save();
                        sSetDirty = false;
                        sUi.status = T(S_SAVED);
                    }
                    sUi.screen = SCR_INFO;
                }
                break;
            default: break;
        }
    }
    if (ks.enter) {
        if (sUi.screen == SCR_SETUP) {
            sEditBack = SCR_SETUP;
            sUi.qrPayload = "WIFI:T:WPA;S:" + ConfigPortal::apSsid() + ";P:" +
                            ConfigPortal::apPass() + ";;";
            sUi.qrTitle = T(S_QR_JOIN_T);
            sUi.qrSub = T(S_QR_JOIN_S);
            sUi.screen = SCR_QR;
            return;
        }
        if (sUi.screen == SCR_WIFI_SCAN && !sUi.scanning && !sUi.nets.empty()) {
            const UiNet& n = sUi.nets[sUi.scanSel];
            sEditSsid = n.ssid;
            if (n.secure) {
                openTextInput(EDIT_WIFI_PASS, String(T(S_PASS_FOR)) + n.ssid, "",
                              true, SCR_WIFI_SCAN);
            } else {
                sEditTarget = EDIT_WIFI_PASS;
                sUi.editValue = "";
                applyNetworkChange();
            }
            return;
        }
        if (sUi.screen == SCR_SETTINGS) {
            String err;
            switch (sUi.setSel) {
                case SET_WIFI:
                    sUi.nets.clear();
                    sUi.scanSel = 0;
                    sUi.scanning = true;
                    sUi.screen = SCR_WIFI_SCAN;
                    WiFi.scanNetworks(true);
                    break;
                case SET_SERVER:
                    openTextInput(EDIT_SRV_URL, T(S_SET_SERVER), gConfig.serverUrl,
                                  false, SCR_SETTINGS);
                    break;
                case SET_SPASS:
                    openTextInput(EDIT_SRV_PASS, T(S_SET_SPASS), "", true, SCR_SETTINGS);
                    break;
                case SET_QR:
                    sEditBack = SCR_SETTINGS;
                    sUi.qrPayload = "http://" + WiFi.localIP().toString() + "/";
                    sUi.qrTitle = T(S_QR_PORTAL_T);
                    sUi.qrSub = T(S_QR_PORTAL_S);
                    sUi.screen = SCR_QR;
                    break;
                case SET_BACKUP:
                    sUi.status = sdBackupSave(err) ? T(S_SD_SAVED) : err;
                    break;
                case SET_RESTORE:
                    if (sdBackupRestore(err)) {
                        showBootMessage(T(S_REBOOTING));
                        delay(600);
                        ESP.restart();
                    }
                    sUi.status = err;
                    break;
            }
            return;
        }
        if (sUi.screen == SCR_CHATS) { if (!sUi.calibrating) openChat(sUi.chatSel); }
        else if (sUi.screen == SCR_MESSAGES) { sUi.screen = SCR_COMPOSE; sUi.statusView = ""; }
    }
}

// ---------------------------------------------------------------------------
// Démarrage
// ---------------------------------------------------------------------------

static void showBootMessage(const String& msg) {
    sCanvas.fillSprite(C_INK900);
    sCanvas.setTextColor(C_BLUE400, C_INK900);
    String brand = "BlueBubbles";
    sCanvas.setCursor((SCREEN_W - sCanvas.textWidth(brand)) / 2, 48);
    sCanvas.print(brand);
    sCanvas.setTextColor(C_SLATE300, C_INK900);
    sCanvas.setCursor((SCREEN_W - sCanvas.textWidth(msg)) / 2, 70);
    sCanvas.print(msg);
    if (sSpriteOk) sCanvas.pushSprite(&M5Cardputer.Display, 0, 0);
}

void setup() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.setBrightness(70);

    Serial.begin(115200);
    Serial.printf("[bb] boot, heap libre : %u\n", (unsigned)ESP.getFreeHeap());

    sCanvas.setColorDepth(16);
    sSpriteOk = sCanvas.createSprite(SCREEN_W, SCREEN_H) != nullptr;
    if (!sSpriteOk) {
        // Repli : 8 bits (32 Ko au lieu de 64) si le heap est trop fragmenté.
        sCanvas.setColorDepth(8);
        sSpriteOk = sCanvas.createSprite(SCREEN_W, SCREEN_H) != nullptr;
    }
    Serial.printf("[bb] sprite : %s, ecran %dx%d, heap : %u\n",
                  sSpriteOk ? "ok" : "ECHEC", M5Cardputer.Display.width(),
                  M5Cardputer.Display.height(), (unsigned)ESP.getFreeHeap());
    if (!sSpriteOk) {
        M5Cardputer.Display.setTextFont(&fonts::efontJA_12);
        M5Cardputer.Display.setCursor(8, 58);
        M5Cardputer.Display.print(T(S_LOW_MEMORY));
    }
    // efont couvre l'ASCII + latin accentué (+ kana/kanji), indispensable
    // pour des messages en français sur M5GFX.
    sCanvas.setTextFont(&fonts::efontJA_12);
    sCanvas.setTextWrap(false);

    gConfig.load();
    Snd::begin();
    Serial.printf("[bb] sons : %u octets\n", (unsigned)Snd::bytesUsed());

    // Tâche réseau : file de commandes + mutex sur les données partagées.
    sDataMux = xSemaphoreCreateMutex();
    sNetQueue = xQueueCreate(6, sizeof(NetCmd));
    xTaskCreatePinnedToCore(netTask, "net", 16384, nullptr, 1, nullptr, 0);

    aliasLoad();
    if (!gConfig.hasWifi()) {
        ConfigPortal::startAP();
        sUi.screen = SCR_SETUP;
        sUi.sdBackup = sdBackupExists();  // une sauvegarde ? proposer « b »
        render();
        return;
    }

    // Splash : la marque + les étapes WiFi > serveur > synchro. Il reste
    // affiché jusqu'à la première liste (ou passe la main aussitôt si la
    // NVS en fournit une).
    sUi.screen = SCR_SPLASH;
    sUi.status = String(T(S_CONNECTING_TO)) + gConfig.wifiSsid + "…";
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(true);  // modem-sleep entre les polls : économise la batterie
    WiFi.begin(gConfig.wifiSsid.c_str(), gConfig.wifiPass.c_str());
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) {
        render();  // anime les points de la bulle pendant l'attente
        delay(120);
    }
    sUi.status = "";

    if (WiFi.status() != WL_CONNECTED) {
        ConfigPortal::startAP();
        sUi.screen = SCR_SETUP;
        sUi.status = T(S_WIFI_NOT_FOUND);
        render();
        return;
    }

    configTzTime(gConfig.tz.c_str(), "pool.ntp.org", "time.cloudflare.com");
    ConfigPortal::startSTA();

    if (!gConfig.hasServer()) {
        sUi.screen = SCR_INFO;
        sUi.status = String(T(S_SERVER_NOT_CONFIGURED)) + WiFi.localIP().toString();
        render();
        return;
    }

    // storeLoad() renseigne sMarker : l'âge se juge après.
    bool haveList = storeLoad();
    // Au-delà de 7 jours de retard, le rattrapage incrémental serait plus
    // coûteux qu'une calibration (docs/04, point 10).
    time_t nowSec = time(nullptr);
    bool tooOld = sMarker > 0 && nowSec > 1700000000 &&
                  ((int64_t)nowSec * 1000 - sMarker) > 7LL * 86400000LL;
    if (haveList && !tooOld) {
        // Liste immédiate depuis la NVS : le splash n'a rien à raconter.
        sUi.screen = SCR_CHATS;
        sUi.status = T(S_UPDATING);
        netEnqueue(NET_POLL);
    } else {
        // Première synchro : le splash reste et montre la progression.
        requestCalibration();
    }
    render();
}

void loop() {
    M5Cardputer.update();
    ConfigPortal::handle();
    if (ConfigPortal::rebootRequested()) {
        showBootMessage(T(S_REBOOTING));
        delay(800);
        ESP.restart();
    }

    handleKeys();

    if (WiFi.status() == WL_CONNECTED && gConfig.hasServer()) {
        // Le poll global tourne sur tous les écrans : il entretient la liste,
        // le marqueur et les bips. La conversation ouverte se rafraîchit en plus.
        pollChats();
        if (sUi.screen == SCR_MESSAGES || sUi.screen == SCR_COMPOSE) pollMessages();
    }

    // Persistance du marqueur au plus toutes les 5 minutes (usure NVS).
    // La comparaison des int64 se fait SOUS le verrou : sur ce cœur 32 bits,
    // une lecture non protégée peut voir une moitié de valeur.
    if (millis() - sMarkerSaveMs > 300000) {
        DataLock l;
        if (sMarker != sMarkerSaved) storeSaveMarker();
        else sMarkerSaveMs = millis();
    }

    if (sNewIncoming) {
        sNewIncoming = false;
        // Un message arrivé ailleurs se signale plus franchement qu'un
        // message de la conversation qu'on a sous les yeux.
        Snd::play((sUi.screen == SCR_MESSAGES || sUi.screen == SCR_COMPOSE) ? Snd::RECEIVED
                                                                      : Snd::NOTIF);
    }
    if (sSendFailed) {
        sSendFailed = false;
        DataLock l;
        // On ne rend le brouillon que si l'utilisateur est toujours dans la
        // conversation visée — sinon il serait réexpédié au mauvais
        // destinataire à la frappe suivante.
        if (sCurChatGuid == sSendBackupGuid) {
            sUi.compose = sSendBackup;
            sUi.screen = SCR_COMPOSE;
        }
        sSendBackup = "";
        sSendBackupGuid = "";
        sDirty = true;
    }
    if (sUi.screen == SCR_WIFI_SCAN && sUi.scanning) {
        int n = WiFi.scanComplete();
        if (n >= 0) {
            DataLock l;
            sUi.nets.clear();
            for (int i = 0; i < n && (int)sUi.nets.size() < 20; i++) {
                String ssid = WiFi.SSID(i);
                if (!ssid.length()) continue;
                bool dup = false;
                for (UiNet& e : sUi.nets)
                    if (e.ssid == ssid) {  // même nom : garde le plus fort
                        if (WiFi.RSSI(i) > e.rssi) e.rssi = WiFi.RSSI(i);
                        dup = true;
                        break;
                    }
                if (dup) continue;
                UiNet un;
                un.ssid = ssid;
                un.rssi = WiFi.RSSI(i);
                un.secure = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
                sUi.nets.push_back(un);
            }
            std::sort(sUi.nets.begin(), sUi.nets.end(),
                      [](const UiNet& a, const UiNet& b) { return a.rssi > b.rssi; });
            WiFi.scanDelete();
            sUi.scanning = false;
            sUi.scanSel = 0;
            sDirty = true;
        }
    }
    if (sUi.screen == SCR_ABOUT) {
        static uint32_t lastPulse = 0;
        if (millis() - lastPulse > 300) { lastPulse = millis(); sDirty = true; }
    }
    if (sUi.screen == SCR_SPLASH) {
        // Fin de la première synchro : place aux conversations. Et tant que
        // le splash est là, on anime ses points à ~8 Hz.
        static uint32_t lastAnim = 0;
        DataLock l;
        if (!sUi.calibrating && !sUi.chats.empty()) {
            sUi.screen = SCR_CHATS;
            sDirty = true;
        } else if (millis() - lastAnim > 120) {
            lastAnim = millis();
            sDirty = true;
        }
    }
    if (sDirty) render();
    delay(10);
}
