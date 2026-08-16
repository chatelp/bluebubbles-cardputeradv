// BlueBubbles Cardputer — client iMessage léger pour M5Stack Cardputer ADV.
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
#include "i18n.h"
#include "config_portal.h"
#include "sound.h"

static const char* APP_VERSION = "0.3.0";

// ---------------------------------------------------------------------------
// État global
// ---------------------------------------------------------------------------

enum Screen { SCR_SETUP, SCR_CHATS, SCR_MESSAGES, SCR_COMPOSE, SCR_INFO, SCR_SETTINGS };

// Construit sans parent et créé tardivement dans setup() : objet global,
// M5Cardputer n'est pas encore initialisé ici (même précaution que Daoa Mini).
static M5Canvas sCanvas;
static bool sSpriteOk = false;
static BBClient sClient;
static Screen sScreen = SCR_CHATS;

static std::vector<BBChat> sChats;
static std::vector<BBMsg> sMsgs;
static std::map<String, int64_t> sSeen;  // clé de fusion -> date du dernier msg vu

static int sChatSel = 0;        // index sélectionné dans la liste
static int sChatTop = 0;        // premier index affiché
static int sMsgScroll = 0;      // index d'arrêt de défilement (0 = bas, dernier message)
static int sMsgStops = 1;       // nombre d'arrêts, recalculé à chaque rendu
static String sCurChatGuid;
static String sCurChatKey;
static String sCurChatTitle;
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
static bool sSynced = false;     // un poll a abouti depuis le démarrage
static volatile bool sCalibrating = false;  // écran plein de calibration
static volatile bool sCalibPending = false; // calibration en file OU en cours
static volatile int sCalibPage = 0;
static volatile int sCalibTotal = 0;
static bool sListChanged = false;  // liste à repersister (entrée ou aperçu modifié)
static String sCompose;
static String sStatus;          // état de la LISTE (poll, calibration, ping)
static String sStatusView;      // état de la CONVERSATION ouverte ("Envoi…", erreurs)
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

// Palette « encre et bulle » — voir le design system (design/foundations).
// Valeurs RGB565 : ce sont exactement les teintes que l'écran affiche.
static const uint16_t C_INK900   = 0x0883;  // fond
static const uint16_t C_INK800   = 0x10C5;  // barres
static const uint16_t C_INK700   = 0x1928;  // bulle reçue, surface
static const uint16_t C_INK600   = 0x21A9;  // ligne sélectionnée
static const uint16_t C_SLATE300 = 0x8D17;  // texte secondaire
static const uint16_t C_SLATE200 = 0xBE3B;  // aperçu
static const uint16_t C_WHITE    = 0xFFFF;
static const uint16_t C_BLUE500  = 0x2BFD;  // bulle envoyée
static const uint16_t C_BLUE400  = 0x4D1F;  // accent
static const uint16_t C_GREEN400 = 0x3631;
static const uint16_t C_AMBER400 = 0xFD84;  // pastille non lu
static const uint16_t C_RED400   = 0xFACB;  // erreur

static const int SCREEN_W = 240;
static const int SCREEN_H = 135;
static const int BAR_H = 16;    // barre supérieure
static const int HINT_H = 13;   // barre d'aide

// Géométrie des bulles (design/foundations/geometry)
static const int BUB_MAXW  = 176;  // 73 % de l'écran
static const int BUB_PADX  = 4;
static const int BUB_PADY  = 3;
static const int BUB_R     = 5;
static const int BUB_TAIL  = 3;
static const int LINE_H    = 13;
static const int GAP_SAME  = 3;   // même auteur
static const int GAP_TURN  = 7;   // changement d'auteur
static const int TAP_PAD   = 10;  // réserve au-dessus d'une bulle à réactions
static const int TAP_H     = 16;  // hauteur du badge (chevauche la bulle de 6 px)
static const int EDGE      = 6;   // marge d'écran

// ---------------------------------------------------------------------------
// Aides rendu — « texte riche » : la police efont pour le texte, les glyphes
// pixel de emoji_art.h pour les émojis (12 px dans la ligne de 13).
// ---------------------------------------------------------------------------

static const int EMOJI_ADV = 13;  // 12 px de glyphe + 1 d'espacement

static int richWidth(const String& s) {
    size_t i = 0;
    BbSeg seg;
    int w = 0;
    while (bbNextSeg(s.c_str(), s.length(), &i, &seg)) {
        if (seg.glyph < 0) w += sCanvas.textWidth(s.substring(seg.start, seg.end));
        else w += EMOJI_ADV;
    }
    return w;
}

static void drawEmoji(int x, int y, int glyph) {
    const uint8_t* art = kEmojiArt[glyph];
    for (int dy = 0; dy < kEmojiPx; dy++)
        for (int dx = 0; dx < kEmojiPx; dx++) {
            uint8_t v = art[dy * kEmojiPx + dx];
            if (v) sCanvas.drawPixel(x + dx, y + dy, kEmojiPalette[v]);
        }
}

static void drawRich(int x, int y, const String& s, uint16_t fg, uint16_t bg) {
    size_t i = 0;
    BbSeg seg;
    sCanvas.setTextColor(fg, bg);
    while (bbNextSeg(s.c_str(), s.length(), &i, &seg)) {
        if (seg.glyph < 0) {
            String run = s.substring(seg.start, seg.end);
            sCanvas.setCursor(x, y);
            sCanvas.print(run);
            x += sCanvas.textWidth(run);
        } else {
            drawEmoji(x, y, seg.glyph);
            x += EMOJI_ADV;
        }
    }
}

// Tronque une chaîne UTF-8 à une largeur en pixels, sans couper un caractère
// multi-octets, avec "…" si besoin.
static String fitText(const String& s, int maxW) {
    if (richWidth(s) <= maxW) return s;
    String out;
    for (size_t i = 0; i < s.length();) {
        size_t n = 1;
        uint8_t c = s[i];
        if ((c & 0xE0) == 0xC0) n = 2;
        else if ((c & 0xF0) == 0xE0) n = 3;
        else if ((c & 0xF8) == 0xF0) n = 4;
        String next = out + s.substring(i, i + n);
        if (richWidth(next + "…") > maxW) break;
        out = next;
        i += n;
    }
    return out + "…";
}

// Coupe un texte en lignes tenant dans maxW pixels (coupure aux espaces
// quand c'est possible, sinon au caractère, frontières UTF-8 respectées).
static void wrapText(const String& s, int maxW, std::vector<String>& lines) {
    String line;
    String word;
    auto flushWord = [&]() {
        if (!word.length()) return;
        String cand = line.length() ? line + " " + word : word;
        if (richWidth(cand) <= maxW) {
            line = cand;
        } else {
            if (line.length()) lines.push_back(line);
            // Mot plus large que l'écran : coupe au caractère.
            while (richWidth(word) > maxW) {
                String part;
                for (size_t i = 0; i < word.length();) {
                    size_t n = 1;
                    uint8_t c = word[i];
                    if ((c & 0xE0) == 0xC0) n = 2;
                    else if ((c & 0xF0) == 0xE0) n = 3;
                    else if ((c & 0xF8) == 0xF0) n = 4;
                    if (richWidth(part + word.substring(i, i + n)) > maxW) break;
                    part += word.substring(i, i + n);
                    i += n;
                }
                if (!part.length()) break;
                lines.push_back(part);
                word = word.substring(part.length());
            }
            line = word;
        }
        word = "";
    };
    for (size_t i = 0; i < s.length(); i++) {
        char c = s[i];
        if (c == ' ') flushWord();
        else if (c == '\n') { flushWord(); lines.push_back(line); line = ""; }
        else word += c;
    }
    flushWord();
    if (line.length()) lines.push_back(line);
    if (lines.empty()) lines.push_back("");
}

static String timeShort(int64_t msEpoch) {
    if (!msEpoch) return "";
    time_t t = msEpoch / 1000;
    struct tm tmv;
    localtime_r(&t, &tmv);
    char buf[6];
    snprintf(buf, sizeof(buf), "%02d:%02d", tmv.tm_hour, tmv.tm_min);
    return buf;
}

static void drawTopBar(const String& title) {
    sCanvas.fillRect(0, 0, SCREEN_W, BAR_H, C_INK800);
    sCanvas.setTextColor(C_BLUE400, C_INK800);
    sCanvas.setCursor(5, 2);
    sCanvas.print(fitText(title, 168));

    int batt = M5Cardputer.Power.getBatteryLevel();
    String right = String(batt) + "%";
    sCanvas.setTextColor(batt < 20 ? C_RED400 : C_SLATE300, C_INK800);
    sCanvas.setCursor(SCREEN_W - sCanvas.textWidth(right) - 5, 2);
    sCanvas.print(right);

    // Point d'état réseau : bleu quand la synchro est fraîche, ambre pendant
    // le rattrapage, rouge hors ligne. Trois pixels valent une phrase.
    uint16_t dot = WiFi.status() != WL_CONNECTED ? C_RED400
                                                 : (sSynced ? C_BLUE400 : C_AMBER400);
    sCanvas.fillCircle(SCREEN_W - sCanvas.textWidth(right) - 12, 8, 2, dot);
}

// Barre d'aide : les raccourcis, toujours au même endroit.
static void drawHintBar(const String& hint) {
    int y = SCREEN_H - HINT_H;
    sCanvas.fillRect(0, y, SCREEN_W, HINT_H, C_INK800);
    sCanvas.setTextColor(C_SLATE300, C_INK800);
    sCanvas.setCursor(5, y + 1);
    sCanvas.print(fitText(hint, SCREEN_W - 10));
}

// Bandeau d'erreur : il recouvre la barre d'aide, jamais le contenu — la
// liste en cache reste lisible pendant que l'appareil se rattrape.
static void drawStatusLine(const String& st) {
    if (!st.length()) return;
    int y = SCREEN_H - HINT_H;
    // Heuristique bilingue : les deux langues doivent virer au rouge — un
    // mot-clé oublié laisserait une erreur en gris, indistinguable d'un état.
    static const char* kErr[] = {"Erreur", "Echec", "Error", "failed", "injoignable",
                                 "Occupe", "Busy",  "HTTP",  "Delai",  "imeout",
                                 "Memoire", "memory", "WiFi", "not found"};
    bool err = false;
    for (const char* k : kErr)
        if (st.indexOf(k) >= 0) { err = true; break; }
    uint16_t col = err ? C_RED400 : (st.indexOf("OK") >= 0 ? C_GREEN400 : C_SLATE300);
    sCanvas.fillRect(0, y, SCREEN_W, HINT_H, C_INK800);
    sCanvas.fillRect(0, y, 2, HINT_H, col);
    sCanvas.setTextColor(col, C_INK800);
    sCanvas.setCursor(7, y + 1);
    sCanvas.print(fitText(st, SCREEN_W - 12));
}

// ---------------------------------------------------------------------------
// Écrans
// ---------------------------------------------------------------------------

static void drawSetup() {
    sCanvas.fillSprite(C_INK900);
    drawTopBar(T(S_SETUP_REQUIRED));
    int y = 24;
    auto step = [&](const char* n, const String& main, const String& sub) {
        sCanvas.setTextColor(C_BLUE400, C_INK900);
        sCanvas.setCursor(8, y);
        sCanvas.print(n);
        sCanvas.setTextColor(C_WHITE, C_INK900);
        sCanvas.setCursor(21, y);
        sCanvas.print(fitText(main, SCREEN_W - 27));
        y += 12;
        if (sub.length()) {
            sCanvas.setTextColor(C_SLATE300, C_INK900);
            sCanvas.setCursor(21, y);
            sCanvas.print(fitText(sub, SCREEN_W - 27));
            y += 12;
        }
        y += 4;
    };
    step("1", "Reseau WiFi  " + ConfigPortal::apSsid(), "mot de passe  " + ConfigPortal::apPass());
    step("2", "http://192.168.4.1", "");
    step("3", "Renseignez WiFi et serveur", "l'appareil redemarre ensuite");
    drawHintBar(T(S_PORTAL_OPEN));
    drawStatusLine(sStatus);
}

// Plein écran de calibration : le seul moment où il n'y a effectivement rien
// à montrer. Progression déterministe (le nombre de pages est connu).
static void drawCalibrating() {
    sCanvas.fillSprite(C_INK900);
    drawTopBar(T(S_CONVERSATIONS));
    sCanvas.setTextColor(C_BLUE400, C_INK900);
    String t = T(S_CALIBRATION);
    sCanvas.setCursor((SCREEN_W - sCanvas.textWidth(t)) / 2, 48);
    sCanvas.print(t);
    sCanvas.setTextColor(C_SLATE300, C_INK900);
    String s = T(S_CALIB_SUB);
    sCanvas.setCursor((SCREEN_W - sCanvas.textWidth(s)) / 2, 64);
    sCanvas.print(s);

    sCanvas.fillRoundRect(50, 82, 140, 5, 2, C_INK700);
    int done = sCalibPage, total = sCalibTotal ? sCalibTotal : 1;
    int w = 140 * min(done, total) / total;
    if (w > 0) sCanvas.fillRoundRect(50, 82, w, 5, 2, C_BLUE400);

    String p = String(T(S_PAGE)) + " " + done + " / " + total;
    sCanvas.setCursor((SCREEN_W - sCanvas.textWidth(p)) / 2, 94);
    sCanvas.print(p);
    drawHintBar(T(S_PLEASE_WAIT));
}

// Modal de recalibration : la liste reste visible dessous (un écran d'état ne
// remplace jamais du contenu déjà chargé — docs/05), mais le modal reste
// affiché pendant TOUT le balayage et le clavier est ignoré : plus de liste
// partielle commise par une calibration interrompue.
static void drawCalibModal() {
    const int w = 184, h = 56;
    const int x = (SCREEN_W - w) / 2, y = 36;
    sCanvas.fillRoundRect(x, y, w, h, 5, C_INK800);
    sCanvas.drawRoundRect(x, y, w, h, 5, C_BLUE400);

    sCanvas.setTextColor(C_BLUE400, C_INK800);
    String t = T(S_CALIBRATION);
    sCanvas.setCursor(x + (w - sCanvas.textWidth(t)) / 2, y + 8);
    sCanvas.print(t);

    int total = sCalibTotal ? sCalibTotal : 1;
    int done = min((int)sCalibPage, total);
    sCanvas.fillRoundRect(x + 22, y + 27, w - 44, 5, 2, C_INK600);
    int bw = (w - 44) * done / total;
    if (bw > 0) sCanvas.fillRoundRect(x + 22, y + 27, bw, 5, 2, C_BLUE400);

    sCanvas.setTextColor(C_SLATE300, C_INK800);
    String p = String(T(S_PAGE)) + " " + done + " / " + total;
    sCanvas.setCursor(x + (w - sCanvas.textWidth(p)) / 2, y + 38);
    sCanvas.print(p);
    drawHintBar(T(S_PLEASE_WAIT));
}

static void drawChats() {
    sCanvas.fillSprite(C_INK900);
    drawTopBar(T(S_CONVERSATIONS));
    const int rowH = 26;
    const int visible = 4;
    if (sChatSel < sChatTop) sChatTop = sChatSel;
    if (sChatSel >= sChatTop + visible) sChatTop = sChatSel - visible + 1;

    if (sChats.empty()) {
        sCanvas.setTextColor(C_SLATE300, C_INK900);
        String e = "Aucune conversation";
        sCanvas.setCursor((SCREEN_W - sCanvas.textWidth(e)) / 2, 60);
        sCanvas.print(e);
    }
    for (int i = 0; i < visible; i++) {
        int idx = sChatTop + i;
        if (idx >= (int)sChats.size()) break;
        const BBChat& c = sChats[idx];
        int y = BAR_H + i * rowH;
        bool sel = (idx == sChatSel);
        uint16_t bg = sel ? C_INK600 : C_INK900;
        if (sel) {
            sCanvas.fillRect(0, y, SCREEN_W, rowH, C_INK600);
            sCanvas.fillRect(0, y, 2, rowH, C_BLUE400);  // filet : le fond seul est trop discret
        }

        bool unread = c.lastDate > 0 && c.lastDate > sSeen[c.key] && !c.lastFromMe;
        int nx = EDGE;
        if (unread) {
            sCanvas.fillCircle(9, y + 8, 2, C_AMBER400);
            nx = 15;
        }
        String ts = timeShort(c.lastDate);
        drawRich(nx, y + 2, fitText(c.title, SCREEN_W - nx - sCanvas.textWidth(ts) - 12),
                 unread ? C_WHITE : C_SLATE200, bg);

        sCanvas.setTextColor(C_SLATE300, bg);
        sCanvas.setCursor(SCREEN_W - sCanvas.textWidth(ts) - 5, y + 2);
        sCanvas.print(ts);

        String preview = (c.lastFromMe ? "moi : " : "") + c.lastText;
        preview.replace("\n", " ");
        drawRich(nx, y + 14, fitText(preview, SCREEN_W - nx - 6), C_SLATE300, bg);

        if (i < visible - 1 && idx + 1 < (int)sChats.size() && !sel)
            sCanvas.drawFastHLine(EDGE, y + rowH - 1, SCREEN_W - EDGE * 2, C_INK700);
    }
    drawHintBar(T(S_HINT_CHATS));
    drawStatusLine(sStatus);
}

// Une bulle : rectangle arrondi + ergot de 3 px sur le coin bas extérieur.
// C'est l'ergot qui fait « messagerie » plutôt que « liste ».
static void drawBubble(int x, int y, int w, int h, bool sent, uint16_t fill) {
    sCanvas.fillRoundRect(x, y, w, h, BUB_R, fill);
    if (sent) {
        sCanvas.fillTriangle(x + w - BUB_R, y + h - 1, x + w + BUB_TAIL - 1, y + h - 1,
                             x + w - 1, y + h - 5, fill);
    } else {
        sCanvas.fillTriangle(x + BUB_R, y + h - 1, x - BUB_TAIL + 1, y + h - 1,
                             x + 1, y + h - 5, fill);
    }
}

// Badge de réactions : pilule à cheval sur le coin haut de la bulle, décalée
// vers le centre de l'écran (comme iMessage). Jusqu'à 3 types de réaction en
// glyphes (❤ 👍 👎 😂) ou en texte (!! ?), et un « xN » si le total dépasse
// ce qui est montré.
static void drawTapBadge(int bx, int bw, int y, bool sent, const uint8_t* taps) {
    static const uint32_t kCp[BB_TAP_TYPES] = {0x2764, 0x1F44D, 0x1F44E, 0x1F602, 0, 0};
    static const char* kTxt[BB_TAP_TYPES] = {nullptr, nullptr, nullptr, nullptr, "!!", "?"};

    int total = 0, shown = 0, w = 0;
    int items[3];
    for (int i = 0; i < BB_TAP_TYPES; i++) {
        if (!taps[i]) continue;
        total += taps[i];
        if (shown < 3) {
            items[shown++] = i;
            w += kTxt[i] ? sCanvas.textWidth(kTxt[i]) : EMOJI_ADV;
        }
    }
    if (!shown) return;
    char cnt[8] = "";
    if (total > shown) {
        snprintf(cnt, sizeof(cnt), "x%d", total);
        w += sCanvas.textWidth(cnt) + 2;
    }
    w += 8;  // marges internes

    // À cheval sur le coin de la bulle, débordant vers le centre de l'écran.
    int x = sent ? bx - w + 12 : bx + bw - 12;
    x = max(2, min(x, SCREEN_W - 2 - w));

    sCanvas.fillRoundRect(x, y, w, TAP_H, TAP_H / 2, C_INK800);
    sCanvas.drawRoundRect(x, y, w, TAP_H, TAP_H / 2, C_INK600);
    int cx = x + 4;
    for (int k = 0; k < shown; k++) {
        int i = items[k];
        if (kTxt[i]) {
            sCanvas.setTextColor(C_WHITE, C_INK800);
            sCanvas.setCursor(cx, y + 2);
            sCanvas.print(kTxt[i]);
            cx += sCanvas.textWidth(kTxt[i]);
        } else {
            drawEmoji(cx, y + 2, bbEmojiGlyph(kCp[i]));
            cx += EMOJI_ADV;
        }
    }
    if (cnt[0]) {
        sCanvas.setTextColor(C_SLATE300, C_INK800);
        sCanvas.setCursor(cx + 2, y + 2);
        sCanvas.print(cnt);
    }
}

static void drawMessages(bool composeMode) {
    sCanvas.fillSprite(C_INK900);
    drawTopBar(sCurChatTitle);

    // Mise en page préalable : chaque message devient un bloc (bulle) dont on
    // connaît la hauteur, plus d'éventuels séparateurs temporels.
    struct Block {
        std::vector<String> lines;
        int h = 0, w = 0;
        int pad = 0;              // réserve au-dessus de la bulle (badge réactions)
        const uint8_t* taps = nullptr;  // compteurs de réactions du message
        bool sent = false;
        bool separator = false;   // horodatage centré
        bool senderHdr = false;   // nom de l'expéditeur (groupes)
        String sepText;
    };
    std::vector<Block> blocks;
    const int contentW = BUB_MAXW - BUB_PADX * 2;
    const bool isGroup = sCurChatKey.startsWith("g:");
    int64_t prevDate = 0;
    bool prevFromMe = false;
    String prevSender;
    bool first = true;

    for (const BBMsg& m : sMsgs) {
        // Séparateur temporel centré au-delà d'un quart d'heure de silence —
        // moins coûteux qu'un horodatage dans chaque bulle (design/geometry).
        if (!first && m.date && prevDate && m.date - prevDate > 15 * 60000LL) {
            Block s;
            s.separator = true;
            s.sepText = timeShort(m.date);
            s.h = 12;
            blocks.push_back(s);
        }
        // Dans un groupe, l'alignement ne dit que « pas moi » : le nom de
        // l'expéditeur s'affiche au changement de voix (comme l'app officielle).
        if (isGroup && !m.fromMe && m.sender.length() && m.sender != prevSender) {
            Block h;
            h.senderHdr = true;
            h.sepText = m.sender;
            h.h = 11;
            blocks.push_back(h);
        }
        prevSender = m.fromMe ? String() : m.sender;
        Block b;
        b.sent = m.fromMe;
        wrapText(m.text, contentW, b.lines);
        int tw = 0;
        for (const String& l : b.lines) tw = max(tw, richWidth(l));
        b.w = tw + BUB_PADX * 2;
        // Un message avec réactions réserve la place du badge au-dessus de sa
        // bulle (le badge la chevauche de quelques pixels, comme iMessage).
        for (int k = 0; k < BB_TAP_TYPES; k++)
            if (m.taps[k]) { b.pad = TAP_PAD; b.taps = m.taps; break; }
        b.h = (int)b.lines.size() * LINE_H + BUB_PADY * 2 + b.pad;
        // L'espacement dit qui parle : serré dans un même tour, aéré au
        // changement d'interlocuteur.
        if (!first && !blocks.empty() && !blocks.back().separator && !blocks.back().senderHdr)
            blocks.back().h += (m.fromMe == prevFromMe) ? GAP_SAME : GAP_TURN;
        blocks.push_back(b);
        prevDate = m.date;
        prevFromMe = m.fromMe;
        first = false;
    }

    int composeH = composeMode ? 24 : 0;
    int areaTop = BAR_H + 3;
    int areaBot = SCREEN_H - HINT_H - composeH;
    int areaH = areaBot - areaTop;

    int total = 0;
    for (const Block& b : blocks) total += b.h;
    int maxScrollPx = max(0, total - areaH);

    // Défilement par ARRÊTS DE BULLE (bb_scroll.h, logique testée en natif) :
    // un pas en lignes laissait la bulle du haut coupée, et l'arrondi de la
    // borne rendait ses derniers pixels définitivement inaccessibles.
    const int STOPS_MAX = 64;  // 256 o de pile ; débordement → dernier arrêt = borne
    int stops[STOPS_MAX];
    {
        // Vue légère des blocs (8 o chacun, ~0,7 Ko au pire) : bb_scroll.h ne
        // connaît que des hauteurs, ce qui le rend testable en natif.
        std::vector<BbBlockInfo> info(blocks.size());
        for (size_t i = 0; i < blocks.size(); i++) {
            info[i].h = blocks[i].h;
            info[i].header = blocks[i].separator || blocks[i].senderHdr;
        }
        sMsgStops = bbScrollStops(info.data(), (int)info.size(), areaH, stops, STOPS_MAX);
    }
    if (sMsgScroll >= sMsgStops) sMsgScroll = sMsgStops - 1;
    if (sMsgScroll < 0) sMsgScroll = 0;
    int scrollPx = stops[sMsgScroll];

    // Le contenu est ancré en bas ; défiler le POUSSE VERS LE BAS pour
    // découvrir l'historique au-dessus (un `-` ici ne faisait que vider
    // l'écran par le haut sans jamais révéler un message plus ancien).
    int y = areaBot + scrollPx;
    for (int i = (int)blocks.size() - 1; i >= 0; i--) {
        const Block& b = blocks[i];
        y -= b.h;
        if (y > areaBot) continue;
        // Hauteur VISIBLE du bloc : sans l'écart d'après coup ni la réserve
        // du badge — la bulle elle-même commence à y + pad.
        int bodyH = (b.separator || b.senderHdr)
                        ? b.h
                        : (int)b.lines.size() * LINE_H + BUB_PADY * 2;
        int by = y + b.pad;
        if (by + bodyH < areaTop) break;

        if (b.separator) {
            sCanvas.setTextColor(C_SLATE300, C_INK900);
            sCanvas.setCursor((SCREEN_W - sCanvas.textWidth(b.sepText)) / 2, y + 1);
            sCanvas.print(b.sepText);
            continue;
        }
        if (b.senderHdr) {
            drawRich(EDGE + 2, y, fitText(b.sepText, BUB_MAXW), C_SLATE300, C_INK900);
            continue;
        }
        int x = b.sent ? SCREEN_W - EDGE - b.w : EDGE;
        uint16_t fill = b.sent ? C_BLUE500 : C_INK700;
        // Découpe verticale : une bulle à cheval sur le bord est tronquée par
        // le sprite, pas dessinée à moitié.
        if (by + bodyH > areaTop && by < areaBot) {
            drawBubble(x, by, b.w, bodyH, b.sent, fill);
            for (size_t li = 0; li < b.lines.size(); li++) {
                int ly = by + BUB_PADY + (int)li * LINE_H;
                if (ly < areaTop - LINE_H || ly > areaBot) continue;
                drawRich(x + BUB_PADX, ly, b.lines[li], C_WHITE, fill);
            }
        }
        // Le badge vit dans la réserve, à cheval sur le coin haut de la bulle.
        if (b.taps && y + TAP_H > areaTop && y < areaBot)
            drawTapBadge(x, b.w, y, b.sent, b.taps);
    }
    // Masque ce qui déborde des barres — jusqu'à areaTop inclus, sinon une
    // bande de 3 px sous la barre garde des fragments de glyphes.
    sCanvas.fillRect(0, BAR_H, SCREEN_W, areaTop - BAR_H, C_INK900);
    sCanvas.fillRect(0, 0, SCREEN_W, BAR_H, C_INK800);
    drawTopBar(sCurChatTitle);

    if (blocks.empty()) {
        sCanvas.setTextColor(C_SLATE300, C_INK900);
        String e = sStatusView.length() ? "" : T(S_EMPTY_CHAT);
        if (e.length()) {
            sCanvas.setCursor((SCREEN_W - sCanvas.textWidth(e)) / 2, 60);
            sCanvas.print(e);
        }
    }

    if (composeMode) {
        // Le champ est surélevé et cerné de bleu : il pousse la conversation
        // vers le haut au lieu de la recouvrir — on voit à qui l'on répond.
        int cy = SCREEN_H - HINT_H - composeH;
        sCanvas.fillRect(0, cy, SCREEN_W, composeH, C_INK800);
        sCanvas.fillRoundRect(4, cy + 3, SCREEN_W - 8, 18, 6, C_INK700);
        sCanvas.drawRoundRect(4, cy + 3, SCREEN_W - 8, 18, 6, C_BLUE400);
        String shown = sCompose;
        while (richWidth(shown) > SCREEN_W - 24 && shown.length())
            shown = shown.substring(1);
        drawRich(9, cy + 6, shown, C_WHITE, C_INK700);
        sCanvas.fillRect(9 + richWidth(shown) + 1, cy + 6, 1, 12, C_BLUE400);
    }

    drawHintBar(composeMode ? T(S_HINT_COMPOSE) : T(S_HINT_MSGS));
    drawStatusLine(sStatusView);
}

static void drawInfo() {
    sCanvas.fillSprite(C_INK900);
    drawTopBar(T(S_INFO));
    int y = BAR_H + 4;
    auto row = [&](const String& k, const String& v, uint16_t col = C_WHITE) {
        sCanvas.setTextColor(C_SLATE300, C_INK900);
        sCanvas.setCursor(6, y);
        sCanvas.print(k);
        sCanvas.setTextColor(col, C_INK900);
        sCanvas.setCursor(78, y);
        sCanvas.print(fitText(v, SCREEN_W - 84));
        y += 13;
    };
    row(T(S_VERSION), APP_VERSION);
    row("WiFi", WiFi.SSID() + "  " + WiFi.RSSI() + " dBm");
    row("IP", WiFi.localIP().toString());
    row(T(S_CONFIG), "cardputer.local");
    row(T(S_SERVER), gConfig.serverUrl);
    row(T(S_SYNC), sMarker ? timeShort(sMarker) : "—", sSynced ? C_GREEN400 : C_AMBER400);
    row(T(S_HISTORY), String(gConfig.histDepth) + " " + T(S_MESSAGES_UNIT));
    row(T(S_TLS), gConfig.tlsVerify ? T(S_TLS_ON) : T(S_TLS_OFF),
        gConfig.tlsVerify ? C_GREEN400 : C_AMBER400);
    drawHintBar(T(S_HINT_INFO));
    drawStatusLine(sStatus);
}

// ---------------------------------------------------------------------------
// Réglages sur l'appareil. Périmètre volontairement restreint aux champs
// NUMÉRIQUES/BOOLÉENS : la tâche réseau (cœur 0) lit gConfig en permanence, et
// réaffecter une String depuis le cœur 1 la ferait planter (piège déjà
// rencontré côté portail — voir handleSave). WiFi, URL, mot de passe et TLS
// restent donc l'apanage du portail web, qui écrit une copie puis redémarre.
// ---------------------------------------------------------------------------

enum SetField : uint8_t {
    SET_LANG, SET_VOL, SET_KEYS, SET_SEND, SET_RECV, SET_NOTIF,
    SET_POLL, SET_HIST, SET_COUNT
};
static int sSetSel = 0;
static bool sSetDirty = false;  // une valeur a changé : à enregistrer en sortant

static String setValueText(uint8_t f) {
    switch (f) {
        case SET_LANG:  return gConfig.lang == LANG_FR ? "Francais" : "English";
        case SET_VOL:   return String(gConfig.sndVolume) + " %";
        case SET_KEYS:  return T(gConfig.sndKeys ? S_ON : S_OFF);
        case SET_SEND:  return T(gConfig.sndSend ? S_ON : S_OFF);
        case SET_RECV:  return T(gConfig.sndRecv ? S_ON : S_OFF);
        case SET_NOTIF: return T(gConfig.sndNotif ? S_ON : S_OFF);
        case SET_POLL:  return String(gConfig.pollSec) + " s";
        case SET_HIST:  return String(gConfig.histDepth) + " " + T(S_MESSAGES_UNIT);
    }
    return "";
}

static StrId setLabel(uint8_t f) {
    switch (f) {
        case SET_LANG:  return S_LANGUAGE;
        case SET_VOL:   return S_VOLUME;
        case SET_KEYS:  return S_SND_KEYS;
        case SET_SEND:  return S_SND_SENT;
        case SET_RECV:  return S_SND_RECV;
        case SET_NOTIF: return S_SND_NOTIF;
        case SET_POLL:  return S_POLL;
        case SET_HIST:  return S_HISTORY;
    }
    return S_SETTINGS;
}

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

static void drawSettings() {
    sCanvas.fillSprite(C_INK900);
    drawTopBar(T(S_SETTINGS));

    const int rowH = 13;
    const int visible = 6;
    int top = 0;
    if (sSetSel >= visible) top = sSetSel - visible + 1;
    int y = BAR_H + 3;
    for (int i = top; i < (int)SET_COUNT && i < top + visible; i++) {
        bool sel = (i == sSetSel);
        if (sel) {
            sCanvas.fillRect(0, y - 1, SCREEN_W, rowH, C_INK600);
            sCanvas.fillRect(0, y - 1, 2, rowH, C_BLUE400);
        }
        uint16_t bg = sel ? C_INK600 : C_INK900;
        sCanvas.setTextColor(sel ? C_WHITE : C_SLATE300, bg);
        sCanvas.setCursor(7, y);
        sCanvas.print(T(setLabel(i)));
        sCanvas.setTextColor(sel ? C_BLUE400 : C_SLATE200, bg);
        String v = setValueText(i);
        sCanvas.setCursor(SCREEN_W - 7 - sCanvas.textWidth(v), y);
        sCanvas.print(v);
        y += rowH;
    }
    // Rappel de la frontière avec le portail : ce qui ne se règle pas ici.
    sCanvas.setTextColor(C_SLATE300, C_INK900);
    String note = fitText(T(S_PORTAL_ONLY), SCREEN_W - 14);
    sCanvas.setCursor(7, SCREEN_H - HINT_H - 13);
    sCanvas.print(note);
    drawHintBar(T(S_HINT_SETTINGS));
    drawStatusLine(sStatus);
}

static void render() {
    DataLock l;  // la tâche réseau peut modifier chats/messages/statut
    switch (sScreen) {
        case SCR_SETUP:    drawSetup(); break;
        // Plein écran de calibration si la liste est vide (synchro initiale) ;
        // sinon la liste en cache reste visible sous le modal de progression.
        case SCR_CHATS:    if (sCalibrating && sChats.empty()) drawCalibrating();
                           else { drawChats(); if (sCalibrating) drawCalibModal(); }
                           break;
        case SCR_MESSAGES: drawMessages(false); break;
        case SCR_COMPOSE:  drawMessages(true); break;
        case SCR_INFO:     drawInfo(); break;
        case SCR_SETTINGS: drawSettings(); break;
    }
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
    std::vector<BBChat> byScore = sChats;
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
    sChats = loaded;
    for (const BBChat& c : sChats) sSeen[c.key] = c.lastDate;  // rien de « nouveau » au boot
    return !sChats.empty();
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
// Appelée sous DataLock quand list == sChats.
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
        if (!sSeen.count(key)) {
            if (r.fromMe) sSeen[key] = r.date;
            else { sSeen[key] = 0; sNewIncoming = true; }
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
            auto it = sSeen.find(key);
            if (it == sSeen.end() || r.date > it->second) sNewIncoming = true;
        }
    }
}

// Sous DataLock : intègre dans la liste affichée.
static void absorb(const BBRecent& r, int64_t nowMs, bool quiet) {
    absorbInto(sChats, r, nowMs, quiet);
}

// Tri par activité + éviction — mais jamais d'une épinglée (top-15 par
// score) : une conversation importante silencieuse reste listée. Sous DataLock.
static void sortAndTrim() {
    std::sort(sChats.begin(), sChats.end(),
              [](const BBChat& a, const BBChat& b) { return a.lastDate > b.lastDate; });
    if (sChats.size() > LIST_MAX) {
        // Ensemble explicite des épinglées (top PINNED_MAX par score, ex æquo
        // départagés par la date) : un seuil scalaire laisserait le repli
        // final tronquer une épinglée en cas d'égalité de score.
        std::vector<int> idx(sChats.size());
        for (size_t i = 0; i < idx.size(); i++) idx[i] = (int)i;
        std::sort(idx.begin(), idx.end(), [](int a, int b) {
            if (sChats[a].score != sChats[b].score) return sChats[a].score > sChats[b].score;
            return sChats[a].lastDate > sChats[b].lastDate;
        });
        std::vector<bool> pinned(sChats.size(), false);
        for (size_t i = 0; i < idx.size() && i < PINNED_MAX; i++) pinned[idx[i]] = true;
        for (int i = (int)sChats.size() - 1; i >= 0 && sChats.size() > LIST_MAX; i--)
            if (!pinned[i]) {
                sChats.erase(sChats.begin() + i);
                pinned.erase(pinned.begin() + i);
            }
    }
    if (sChatSel >= (int)sChats.size()) sChatSel = max(0, (int)sChats.size() - 1);
}

// Décroissance quotidienne des scores, cadencée par le marqueur (horloge
// serveur). Sous DataLock.
static void maybeDecay() {
    int32_t day = (int32_t)(sMarker / 86400000LL);
    if (sDecayDay == 0) { sDecayDay = day; return; }
    bool changed = false;
    while (day > sDecayDay) {
        for (BBChat& c : sChats) c.score *= 0.95f;
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
    // Pages de 10 (~18 Ko de corps) : c'est le plus gros bloc contigu que le
    // tas puisse fournir en fonctionnement (~31 Ko mesurés). Voir bb_client.cpp.
    const uint8_t PAGE = 10;
    const uint8_t MAX_PAGES = 30;  // ~300 messages (~25 s)
    const int64_t WINDOW_MS = 30LL * 86400000LL;

    sCalibrating = true;
    sCalibPage = 1;
    sCalibTotal = MAX_PAGES;
    {
        DataLock l;
        sStatus = "";
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
                sStatus = err;  // liste intacte
                sCalibrating = false;
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
        sCalibPage = min((int)page + 2, (int)MAX_PAGES);
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
        if (uxQueueMessagesWaiting(sNetQueue) > 0) { aborted = true; break; }  // l'utilisateur attend
    }
    if (built.empty()) {
        DataLock l;
        sStatus = T(S_NO_CHATS);
        sCalibrating = false;
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
        for (const BBChat& c : sChats) {
            bool present = false;
            for (const BBChat& b : built)
                if (b.key == c.key) { present = true; break; }
            if (!present) built.push_back(c);
        }
    }
    sChats = built;
    sSeen.clear();
    for (const BBChat& c : sChats) sSeen[c.key] = c.lastDate;  // calibration = tout lu
    sortAndTrim();
    sChatSel = 0;
    sChatTop = 0;
    if (newest > sMarker) sMarker = newest;
    sPollBefore = 0;
    sPendingNewest = 0;
    sPollRounds = 0;
    sDecayDay = (int32_t)(sMarker / 86400000LL);
    storeSave();
    sSynced = true;
    sCalibrating = false;
    sCalibPending = false;
    sStatus = "";
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
            sStatus = err;
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
        sStatus = "";
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
            sStatus = T(S_CATCHUP_RECALIB);
            requestCalibration();
        } else {
            sStatus = T(S_CATCHUP);
        }
    }
    maybeDecay();
    if (sListChanged) storeSave();  // entrée ou aperçu modifié : blob à jour
    sSynced = true;
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
        for (BBMsg& m : sMsgs)
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
        if (incremental && sCurChatGuid == guid && !sMsgs.empty())
            after = sMsgs.back().date - 1;
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
            if (err.startsWith("HTTP 404")) {
                sMsgs.clear();
                sStatusView = T(S_CHAT_DELETED);
            } else {
                sStatusView = err;
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
        grew = msgs.size() != sMsgs.size() ||
               (msgs.size() && sMsgs.size() && msgs.back().guid != sMsgs.back().guid);
        sMsgs = msgs;
        sTapSeen.clear();  // vue reconstruite : les compteurs repartent de zéro
    } else {
        for (const BBMsg& m : msgs) {
            bool dup = false;
            for (BBMsg& e : sMsgs)
                if (e.guid == m.guid) {
                    dup = true;
                    // Message réapparu avec un texte différent : édition ou
                    // retrait — mise à jour en place, sans toucher au défilement.
                    if (e.text != m.text) { e.text = m.text; sDirty = true; }
                    break;
                }
            if (!dup) { sMsgs.push_back(m); grew = true; added++; }
        }
        const size_t CAP = 30;  // borne mémoire de la vue
        if (sMsgs.size() > CAP) sMsgs.erase(sMsgs.begin(), sMsgs.begin() + (sMsgs.size() - CAP));
    }
    // Recalage sur le dernier message SEULEMENT si l'on y était déjà : arracher
    // le lecteur au milieu de l'historique parce qu'un message arrive serait
    // hostile. Sinon on décale l'index d'autant d'arrêts que de bulles ajoutées
    // en bas, pour garder la même bulle sous les yeux.
    if (grew) sMsgScroll = (sMsgScroll > 0) ? sMsgScroll + added : 0;
    applyTaps(taps);
    if (!sMsgs.empty()) sSeen[sCurChatKey] = sMsgs.back().date;
    sStatusView = "";
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
        sStatusView = T(S_SEND_UNCONFIRMED);
        sDirty = true;
    } else {
        Snd::play(Snd::ERROR);
        DataLock l;
        sStatusView = String(T(S_SEND_FAILED)) + err;
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
    if (index < 0 || index >= (int)sChats.size())
        return "{\"queued\":false,\"raison\":\"index hors liste\"}";
    if (!netEnqueue(NET_DEBUG, sChats[index].guid))
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
                sStatus = ok ? String(T(S_SERVER_OK)) : (String(T(S_ERROR_PREFIX)) + err);
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
    bool full = (sMsgScroll == 0) && (++sMsgPollN >= 6);
    if (full) sMsgPollN = 0;
    netEnqueue(full ? NET_MSGS : NET_MSGS_INC, sCurChatGuid);
}

// Appelé sous DataLock (depuis handleKeys).
static void openChat(int idx) {
    if (idx < 0 || idx >= (int)sChats.size()) return;
    sCurChatGuid = sChats[idx].guid;
    sCurChatKey = sChats[idx].key;
    sCurChatTitle = sChats[idx].title;
    sMsgs.clear();
    sMsgScroll = 0;
    sMsgPollN = 0;  // le cycle de rechargement complet suit la conversation affichée
    sChatEpoch++;  // toute réponse réseau d'une ouverture précédente est périmée
    sScreen = SCR_MESSAGES;
    sSeen[sCurChatKey] = sChats[idx].lastDate;
    sLastPollMsgs = millis();
    sStatusView = netEnqueue(NET_MSGS, sCurChatGuid) ? T(S_LOADING)
                                                     : "Occupe : quittez et rouvrez";
    if (sMarker != sMarkerSaved) storeSaveMarker();  // « événement visible » : on persiste
    sDirty = true;
}

// Appelé sous DataLock (depuis handleKeys). L'envoi part en tâche de fond ;
// le brouillon est restauré si l'envoi échoue.
static void sendCompose() {
    if (!sCompose.length()) return;
    if (!netEnqueue(NET_SEND, sCurChatGuid, sCompose)) {
        sStatusView = T(S_BUSY_RETRY);  // le brouillon reste à l'écran
        sDirty = true;
        return;
    }
    sSendBackup = sCompose;
    sSendBackupGuid = sCurChatGuid;  // le brouillon appartient à CETTE conversation
    sCompose = "";
    sScreen = SCR_MESSAGES;
    sStatusView = T(S_SENDING);
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

    if (sScreen == SCR_COMPOSE) {
        if (ks.enter) { sendCompose(); return; }
        if (ks.del && sCompose.length()) {
            // Retire un caractère UTF-8 complet.
            int i = sCompose.length() - 1;
            while (i > 0 && (sCompose[i] & 0xC0) == 0x80) i--;
            sCompose = sCompose.substring(0, i);
            return;
        }
        for (char c : ks.word) {
            if (c == '`') { sScreen = SCR_MESSAGES; sStatusView = ""; return; }
            sCompose += c;
        }
        return;
    }

    for (char c : ks.word) {
        switch (sScreen) {
            case SCR_CHATS:
                if (sCalibrating) break;  // modal : clavier ignoré le temps du balayage
                if (c == ';' && sChatSel > 0) sChatSel--;
                else if (c == '.' && sChatSel < (int)sChats.size() - 1) sChatSel++;
                else if (c == '`') sScreen = SCR_INFO;
                else if (c == 'r')
                    sStatus = requestCalibration() ? T(S_CALIBRATING) : T(S_BUSY_RETRY);
                break;
            case SCR_MESSAGES:
                if (c == ';') sMsgScroll = min(sMsgScroll + 1, sMsgStops - 1);
                else if (c == '.') sMsgScroll = max(0, sMsgScroll - 1);
                else if (c == '`') { sScreen = SCR_CHATS; pollChats(true); }
                break;
            case SCR_INFO:
                if (c == '`') sScreen = SCR_CHATS;
                else if (c == 'p')
                    sStatus = netEnqueue(NET_PING) ? T(S_TESTING_SERVER) : T(S_BUSY_RETRY);
                else if (c == 's') { sScreen = SCR_SETTINGS; sSetSel = 0; sStatus = ""; }
                break;
            case SCR_SETTINGS:
                // ; / . parcourent les champs, , et / changent la valeur.
                if (c == ';' && sSetSel > 0) sSetSel--;
                else if (c == '.' && sSetSel < (int)SET_COUNT - 1) sSetSel++;
                else if (c == ',') setAdjust(sSetSel, -1);
                else if (c == '/') setAdjust(sSetSel, +1);
                else if (c == '`') {
                    // Enregistrement à la sortie : une écriture NVS par visite,
                    // pas une par appui de touche (usure).
                    if (sSetDirty) {
                        gConfig.save();
                        sSetDirty = false;
                        sStatus = T(S_SAVED);
                    }
                    sScreen = SCR_INFO;
                }
                break;
            default: break;
        }
    }
    if (ks.enter) {
        if (sScreen == SCR_CHATS) { if (!sCalibrating) openChat(sChatSel); }
        else if (sScreen == SCR_MESSAGES) { sScreen = SCR_COMPOSE; sStatusView = ""; }
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

    if (!gConfig.hasWifi()) {
        ConfigPortal::startAP();
        sScreen = SCR_SETUP;
        render();
        return;
    }

    showBootMessage(String(T(S_CONNECTING_TO)) + gConfig.wifiSsid + "…");
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(true);  // modem-sleep entre les polls : économise la batterie
    WiFi.begin(gConfig.wifiSsid.c_str(), gConfig.wifiPass.c_str());
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) delay(200);

    if (WiFi.status() != WL_CONNECTED) {
        ConfigPortal::startAP();
        sScreen = SCR_SETUP;
        sStatus = T(S_WIFI_NOT_FOUND);
        render();
        return;
    }

    configTzTime(gConfig.tz.c_str(), "pool.ntp.org", "time.cloudflare.com");
    ConfigPortal::startSTA();

    if (!gConfig.hasServer()) {
        sScreen = SCR_INFO;
        sStatus = String(T(S_SERVER_NOT_CONFIGURED)) + WiFi.localIP().toString();
        render();
        return;
    }

    sScreen = SCR_CHATS;
    // storeLoad() renseigne sMarker : l'âge se juge après.
    bool haveList = storeLoad();
    // Au-delà de 7 jours de retard, le rattrapage incrémental serait plus
    // coûteux qu'une calibration (docs/04, point 10).
    time_t nowSec = time(nullptr);
    bool tooOld = sMarker > 0 && nowSec > 1700000000 &&
                  ((int64_t)nowSec * 1000 - sMarker) > 7LL * 86400000LL;
    if (haveList && !tooOld) {
        // Liste immédiate depuis la NVS ; le premier poll fait le rattrapage.
        sStatus = T(S_UPDATING);
        netEnqueue(NET_POLL);
    } else {
        sStatus = T(S_CALIBRATING);
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
        if (sScreen == SCR_MESSAGES || sScreen == SCR_COMPOSE) pollMessages();
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
        Snd::play((sScreen == SCR_MESSAGES || sScreen == SCR_COMPOSE) ? Snd::RECEIVED
                                                                      : Snd::NOTIF);
    }
    if (sSendFailed) {
        sSendFailed = false;
        DataLock l;
        // On ne rend le brouillon que si l'utilisateur est toujours dans la
        // conversation visée — sinon il serait réexpédié au mauvais
        // destinataire à la frappe suivante.
        if (sCurChatGuid == sSendBackupGuid) {
            sCompose = sSendBackup;
            sScreen = SCR_COMPOSE;
        }
        sSendBackup = "";
        sSendBackupGuid = "";
        sDirty = true;
    }
    if (sDirty) render();
    delay(10);
}
