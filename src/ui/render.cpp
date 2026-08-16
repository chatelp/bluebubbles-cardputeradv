// Rendu de l'interface — voir render.h. Ce fichier ne connaît ni le réseau,
// ni la NVS, ni le matériel : il dessine le modèle (model.h) dans un canvas
// lgfx. Il compile tel quel pour l'appareil (M5GFX) et pour le simulateur
// SDL (LovyanGFX) — c'est ce qui permet d'itérer sur l'interface sans
// flasher, et de capturer des écrans au pixel près.
#include "render.h"

#include <vector>

#include "app_config.h"
#include "bb_emoji.h"
#include "bb_scroll.h"
#include "emoji_art.h"
#include "i18n.h"
#include "theme.h"

// État de frame : posés par uiRender, lus par les fonctions de dessin.
static BbCanvas* C = nullptr;
static UiModel* M = nullptr;

// ---------------------------------------------------------------------------
// Aides rendu — « texte riche » : la police efont pour le texte, les glyphes
// pixel de emoji_art.h pour les émojis (12 px dans la ligne de 13).
// ---------------------------------------------------------------------------

static int richWidth(const String& s) {
    size_t i = 0;
    BbSeg seg;
    int w = 0;
    while (bbNextSeg(s.c_str(), s.length(), &i, &seg)) {
        if (seg.glyph < 0) w += C->textWidth(s.substring(seg.start, seg.end));
        else w += EMOJI_ADV;
    }
    return w;
}

static void drawEmoji(int x, int y, int glyph) {
    const uint8_t* art = kEmojiArt[glyph];
    for (int dy = 0; dy < kEmojiPx; dy++)
        for (int dx = 0; dx < kEmojiPx; dx++) {
            uint8_t v = art[dy * kEmojiPx + dx];
            if (v) C->drawPixel(x + dx, y + dy, kEmojiPalette[v]);
        }
}

static void drawRich(int x, int y, const String& s, uint16_t fg, uint16_t bg) {
    size_t i = 0;
    BbSeg seg;
    C->setTextColor(fg, bg);
    while (bbNextSeg(s.c_str(), s.length(), &i, &seg)) {
        if (seg.glyph < 0) {
            String run = s.substring(seg.start, seg.end);
            C->setCursor(x, y);
            C->print(run);
            x += C->textWidth(run);
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

// Étiquette machine : capitales ASCII (Font0 n'a pas d'accents) — É→E.
// Réservé aux textes « appareil » : titres de barres, QUI/QUAND, compteurs.
static String upperLabel(const String& s) {
    String o;
    o.reserve(s.length());
    for (size_t i = 0; i < s.length(); i++) {
        uint8_t c = s[i];
        if (c < 0x80) {
            o += (char)((c >= 'a' && c <= 'z') ? c - 32 : c);
            continue;
        }
        if (c == 0xC3 && i + 1 < s.length()) {
            uint8_t d = (uint8_t)s[i + 1] | 0x20;  // plie majuscules/minuscules
            i++;
            char r = 0;
            if (d >= 0xA0 && d <= 0xA5) r = 'A';
            else if (d == 0xA7) r = 'C';
            else if (d >= 0xA8 && d <= 0xAB) r = 'E';
            else if (d >= 0xAC && d <= 0xAF) r = 'I';
            else if (d == 0xB1) r = 'N';
            else if (d >= 0xB2 && d <= 0xB6) r = 'O';
            else if (d >= 0xB9 && d <= 0xBC) r = 'U';
            if (r) o += r;
        } else {  // émoji ou autre : pas sa place dans une étiquette machine
            int n = (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 2;
            i += (size_t)n - 1;
        }
    }
    return o;
}

// Panneau biseauté : arête claire en haut, sombre en bas — c'est lui qui
// fait « appareil » plutôt qu'« application ».
static void drawBevelPanel(int x, int y, int w, int h) {
    C->fillRect(x, y, w, h, C_PANEL);
    C->drawFastHLine(x, y, w, C_EDGE_L);
    C->drawFastHLine(x, y + h - 1, w, C_EDGE_D);
}

static void drawTopBar(const String& title) {
    drawBevelPanel(0, 0, SCREEN_W, BAR_H);

    // LED : rouge hors ligne, ambre s'il reste du non-lu, bleue au repos.
    bool unread = false;
    for (const BBChat& c : M->chats) {
        auto it = M->seen.find(c.key);
        if (c.lastDate > 0 && !c.lastFromMe &&
            (it == M->seen.end() || c.lastDate > it->second)) { unread = true; break; }
    }
    uint16_t led = !M->wifiOk ? C_RED400 : (unread ? C_AMBER400 : C_BLUE500);
    C->fillCircle(9, 8, 3, led);
    C->drawCircle(9, 8, 4, C_EDGE_D);

    C->setTextColor(C_WHITE, C_PANEL);
    C->setCursor(20, 2);
    C->print(fitText(upperLabel(title), 148));

    // À droite, des instruments DESSINÉS : % batterie (Font0), barres de
    // signal (échelle en creux toujours visible), pile avec son remplissage.
    int batt = M->battery;
    C->drawRect(216, 4, 18, 9, C_SLATE300);
    C->fillRect(234, 6, 2, 5, C_SLATE300);
    int fw = (14 * batt) / 100;
    if (fw > 0) C->fillRect(218, 6, fw, 5, batt < 20 ? C_RED400 : C_SLATE300);

    int bars = !M->wifiOk           ? 0
               : (M->rssi >= -55)   ? 4
               : (M->rssi >= -65)   ? 3
               : (M->rssi >= -75)   ? 2
               : (M->rssi >= -85)   ? 1 : 0;
    for (int b = 0; b < 4; b++) {
        int h = 3 + b * 2;                       // 3, 5, 7, 9 px
        uint16_t col = b < bars ? C_SLATE300 : C_INK600;
        C->fillRect(196 + b * 4, 12 - h, 2, h, col);
    }

    C->setFont(&fonts::Font0);
    String pct = String(batt) + "%";
    C->setTextColor(batt < 20 ? C_RED400 : C_SLATE300, C_PANEL);
    C->setCursor(192 - C->textWidth(pct), 5);
    C->print(pct);
    C->setFont(&fonts::efontJA_12);
}

// Barre d'aide : les raccourcis, toujours au même endroit.
static void drawHintBar(const String& hint, int rightReserve = 0) {
    int y = SCREEN_H - HINT_H;
    drawBevelPanel(0, y, SCREEN_W, HINT_H);
    C->setFont(&fonts::Font0);
    C->setTextColor(C_SLATE300, C_PANEL);
    C->setCursor(5, y + 3);
    C->print(fitText(upperLabel(hint), SCREEN_W - 10 - rightReserve));
    C->setFont(&fonts::efontJA_12);
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
    drawBevelPanel(0, y, SCREEN_W, HINT_H);
    C->fillRect(0, y, 2, HINT_H, col);
    C->setTextColor(col, C_PANEL);
    C->setCursor(7, y + 1);
    C->print(fitText(st, SCREEN_W - 12));
}

// ---------------------------------------------------------------------------
// Écrans
// ---------------------------------------------------------------------------

static void drawSetup() {
    C->fillSprite(C_INK900);
    drawTopBar(T(S_SETUP_REQUIRED));
    int y = 24;
    auto step = [&](const char* n, const String& main, const String& sub) {
        C->setTextColor(C_BLUE400, C_INK900);
        C->setCursor(8, y);
        C->print(n);
        C->setTextColor(C_WHITE, C_INK900);
        C->setCursor(21, y);
        C->print(fitText(main, SCREEN_W - 27));
        y += 12;
        if (sub.length()) {
            C->setTextColor(C_SLATE300, C_INK900);
            C->setCursor(21, y);
            C->print(fitText(sub, SCREEN_W - 27));
            y += 12;
        }
        y += 4;
    };
    step("1", T(S_SETUP_WIFI) + M->apSsid, T(S_SETUP_PASS) + M->apPass);
    step("2", "http://192.168.4.1", "");
    step("3", T(S_SETUP_FILL), T(S_SETUP_REBOOT));
    drawHintBar(T(S_PORTAL_OPEN));
    drawStatusLine(M->status);
}

// Écran d'accueil : la marque, puis les trois étapes du premier contact —
// WiFi, serveur, synchro. Il vit du démarrage jusqu'à la première liste ;
// les états se déduisent du modèle, rien n'est mis en scène.
static void drawSplash() {
    C->fillSprite(C_INK900);

    // La bulle « en train d'écrire » : le logo est le produit lui-même.
    const int bw = 46, bh = 30;
    const int bx = (SCREEN_W - bw) / 2, by = 14;
    C->fillRoundRect(bx, by, bw, bh, 10, C_BLUE500);
    C->fillTriangle(bx + 7, by + bh - 1, bx + 1, by + bh + 6, bx + 17, by + bh - 1, C_BLUE500);
    int phase = (int)((millis() / 280) % 3);
    for (int d = 0; d < 3; d++)
        C->fillCircle(bx + 13 + d * 10, by + bh / 2, d == phase ? 3 : 2,
                      d == phase ? C_WHITE : 0xB5FE);

    C->setTextColor(C_WHITE, C_INK900);
    String t1 = "BlueBubbles Cardputer";
    C->setCursor((SCREEN_W - C->textWidth(t1)) / 2, 58);
    C->print(t1);

    // Les trois étapes, séparées par des chevrons. Fait = bleu, en cours =
    // blanc (avec les points animés de la bulle), à venir = gris.
    bool wifiDone = M->wifiOk;
    bool serverDone = M->calibPage > 1 || M->synced;
    bool syncDone = M->synced || (!M->calibrating && !M->chats.empty());
    int cur = !wifiDone ? 0 : !serverDone ? 1 : 2;
    const char* labels[3] = {"WiFi", T(S_SERVER), T(S_SYNC)};
    bool done[3] = {wifiDone, serverDone, syncDone};
    int total = C->textWidth(labels[0]) + C->textWidth(labels[1]) + C->textWidth(labels[2]) + 2 * 14;
    int x = (SCREEN_W - total) / 2;
    for (int i = 0; i < 3; i++) {
        uint16_t col = done[i] ? C_AMBER400 : (i == cur ? C_WHITE : C_SLATE300);
        C->setTextColor(col, C_INK900);
        C->setCursor(x, 78);
        C->print(labels[i]);
        x += C->textWidth(labels[i]);
        if (i < 2) {
            C->setTextColor(C_SLATE300, C_INK900);
            C->setCursor(x + 4, 78);
            C->print(">");
            x += 14;
        }
    }

    // Pendant la calibration : la même barre honnête que partout ailleurs.
    if (M->calibrating) {
        C->fillRoundRect(50, 98, 140, 5, 2, C_INK700);
        int tot = M->calibTotal ? M->calibTotal : 1;
        int w = 140 * min((int)M->calibPage, tot) / tot;
        if (w > 0) C->fillRoundRect(50, 98, w, 5, 2, C_AMBER400);
        String p = String(T(S_PAGE)) + " " + (int)M->calibPage + " / " + tot;
        C->setTextColor(C_SLATE300, C_INK900);
        C->setCursor((SCREEN_W - C->textWidth(p)) / 2, 106);
        C->print(p);
    }

    String v = String("v") + M->version;
    C->setTextColor(C_INK600, C_INK900);
    C->setCursor((SCREEN_W - C->textWidth(v)) / 2, SCREEN_H - 13);
    C->print(v);
    drawStatusLine(M->status);
}

// Plein écran de calibration : le seul moment où il n'y a effectivement rien
// à montrer. Progression déterministe (le nombre de pages est connu).
static void drawCalibrating() {
    C->fillSprite(C_INK900);
    drawTopBar(T(S_CONVERSATIONS));
    C->setTextColor(C_BLUE400, C_INK900);
    String t = T(S_CALIBRATION);
    C->setCursor((SCREEN_W - C->textWidth(t)) / 2, 48);
    C->print(t);
    C->setTextColor(C_SLATE300, C_INK900);
    String s = T(S_CALIB_SUB);
    C->setCursor((SCREEN_W - C->textWidth(s)) / 2, 64);
    C->print(s);

    C->fillRoundRect(50, 82, 140, 5, 2, C_INK700);
    int done = M->calibPage, total = M->calibTotal ? M->calibTotal : 1;
    int w = 140 * min(done, total) / total;
    if (w > 0) C->fillRoundRect(50, 82, w, 5, 2, C_AMBER400);

    String p = String(T(S_PAGE)) + " " + done + " / " + total;
    C->setCursor((SCREEN_W - C->textWidth(p)) / 2, 94);
    C->print(p);
    drawHintBar(T(S_PLEASE_WAIT));
}

// Modal de recalibration : la liste reste visible dessous (un écran d'état ne
// remplace jamais du contenu déjà chargé — docs/05), mais le modal reste
// affiché pendant TOUT le balayage et le clavier est ignoré : plus de liste
// partielle commise par une calibration interrompue.
static void drawCalibModal() {
    const int w = 184, h = 56;
    const int x = (SCREEN_W - w) / 2, y = 36;
    C->fillRoundRect(x + 1, y + 2, w, h, 5, C_SHADOW);
    C->fillRoundRect(x, y, w, h, 5, C_PANEL);
    C->drawRoundRect(x, y, w, h, 5, C_EDGE_L);

    C->setTextColor(C_BLUE400, C_INK800);
    String t = T(S_CALIBRATION);
    C->setCursor(x + (w - C->textWidth(t)) / 2, y + 8);
    C->print(t);

    int total = M->calibTotal ? M->calibTotal : 1;
    int done = min((int)M->calibPage, total);
    C->fillRoundRect(x + 22, y + 27, w - 44, 5, 2, C_INK600);
    int bw = (w - 44) * done / total;
    if (bw > 0) C->fillRoundRect(x + 22, y + 27, bw, 5, 2, C_AMBER400);

    C->setTextColor(C_SLATE300, C_INK800);
    String p = String(T(S_PAGE)) + " " + done + " / " + total;
    C->setCursor(x + (w - C->textWidth(p)) / 2, y + 38);
    C->print(p);
    drawHintBar(T(S_PLEASE_WAIT));
}

static void drawChats() {
    C->fillSprite(C_INK900);
    drawTopBar(T(S_CONVERSATIONS));
    const int rowH = 26;
    const int visible = 4;
    if (M->chatSel < M->chatTop) M->chatTop = M->chatSel;
    if (M->chatSel >= M->chatTop + visible) M->chatTop = M->chatSel - visible + 1;

    if (M->chats.empty()) {
        C->setTextColor(C_SLATE300, C_INK900);
        String e = T(S_NO_CHATS_YET);
        C->setCursor((SCREEN_W - C->textWidth(e)) / 2, 60);
        C->print(e);
    }
    for (int i = 0; i < visible; i++) {
        int idx = M->chatTop + i;
        if (idx >= (int)M->chats.size()) break;
        const BBChat& c = M->chats[idx];
        int y = BAR_H + i * rowH;
        bool sel = (idx == M->chatSel);
        uint16_t bg = sel ? C_INK600 : C_INK900;
        if (sel) {
            C->fillRect(0, y, SCREEN_W, rowH, C_INK600);
            C->fillRect(0, y, 2, rowH, C_BLUE400);  // filet : le fond seul est trop discret
        }

        bool unread = c.lastDate > 0 && c.lastDate > M->seen[c.key] && !c.lastFromMe;
        int nx = EDGE;
        if (unread) {
            C->fillCircle(9, y + 8, 2, C_AMBER400);
            nx = 15;
        }
        String ts = timeShort(c.lastDate);
        C->setFont(&fonts::Font0);
        int tsW = C->textWidth(ts);
        C->setFont(&fonts::efontJA_12);
        drawRich(nx, y + 2, fitText(c.title, SCREEN_W - nx - tsW - 12),
                 unread ? C_WHITE : C_SLATE200, bg);

        C->setFont(&fonts::Font0);
        C->setTextColor(C_SLATE300, bg);
        C->setCursor(SCREEN_W - tsW - 5, y + 4);
        C->print(ts);
        C->setFont(&fonts::efontJA_12);

        String preview = (c.lastFromMe ? String(T(S_ME_PREFIX)) : String("")) + c.lastText;
        preview.replace("\n", " ");
        drawRich(nx, y + 14, fitText(preview, SCREEN_W - nx - 6), C_SLATE300, bg);

        if (i < visible - 1 && idx + 1 < (int)M->chats.size() && !sel)
            C->drawFastHLine(EDGE, y + rowH - 1, SCREEN_W - EDGE * 2, C_EDGE_D);
    }
    // Compteur machine, en ambre, à droite de la barre d'aide — qui lui
    // réserve sa place au lieu de le chevaucher.
    int newCount = 0;
    for (const BBChat& c : M->chats) {
        auto it = M->seen.find(c.key);
        if (c.lastDate > 0 && !c.lastFromMe &&
            (it == M->seen.end() || c.lastDate > it->second)) newCount++;
    }
    String n = String(T(S_NEW_COUNT)) + newCount;
    C->setFont(&fonts::Font0);
    int nW = C->textWidth(n);
    C->setFont(&fonts::efontJA_12);
    drawHintBar(T(S_HINT_CHATS), newCount > 0 ? nW + 8 : 0);
    if (newCount > 0 && !M->status.length()) {
        C->setFont(&fonts::Font0);
        C->setTextColor(C_AMBER400, C_PANEL);
        C->setCursor(SCREEN_W - nW - 5, SCREEN_H - HINT_H + 3);
        C->print(n);
        C->setFont(&fonts::efontJA_12);
    }
    drawStatusLine(M->status);
}

// Une bulle : rectangle arrondi + ergot de 3 px sur le coin bas extérieur.
// C'est l'ergot qui fait « messagerie » plutôt que « liste ».
static void drawBubble(int x, int y, int w, int h, bool sent, uint16_t fill) {
    C->fillRoundRect(x + 1, y + 2, w, h, BUB_R, C_SHADOW);  // ombre portée
    C->fillRoundRect(x, y, w, h, BUB_R, fill);
    C->drawFastHLine(x + BUB_R, y + 1, w - BUB_R * 2, sent ? C_HI_SENT : C_HI_RECV);
    if (sent) {
        C->fillTriangle(x + w - BUB_R, y + h - 1, x + w + BUB_TAIL - 1, y + h - 1,
                             x + w - 1, y + h - 5, fill);
    } else {
        C->fillTriangle(x + BUB_R, y + h - 1, x - BUB_TAIL + 1, y + h - 1,
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
            w += kTxt[i] ? C->textWidth(kTxt[i]) : EMOJI_ADV;
        }
    }
    if (!shown) return;
    char cnt[8] = "";
    if (total > shown) {
        snprintf(cnt, sizeof(cnt), "x%d", total);
        w += C->textWidth(cnt) + 2;
    }
    w += 8;  // marges internes

    // À cheval sur le coin de la bulle, débordant vers le centre de l'écran.
    int x = sent ? bx - w + 12 : bx + bw - 12;
    x = max(2, min(x, SCREEN_W - 2 - w));

    C->fillRoundRect(x + 1, y + 1, w, TAP_H, TAP_H / 2, C_SHADOW);
    C->fillRoundRect(x, y, w, TAP_H, TAP_H / 2, C_INK800);
    C->drawRoundRect(x, y, w, TAP_H, TAP_H / 2, C_INK600);
    int cx = x + 4;
    for (int k = 0; k < shown; k++) {
        int i = items[k];
        if (kTxt[i]) {
            C->setTextColor(C_WHITE, C_INK800);
            C->setCursor(cx, y + 2);
            C->print(kTxt[i]);
            cx += C->textWidth(kTxt[i]);
        } else {
            drawEmoji(cx, y + 2, bbEmojiGlyph(kCp[i]));
            cx += EMOJI_ADV;
        }
    }
    if (cnt[0]) {
        C->setTextColor(C_SLATE300, C_INK800);
        C->setCursor(cx + 2, y + 2);
        C->print(cnt);
    }
}

static void drawMessages(bool composeMode) {
    C->fillSprite(C_INK900);
    drawTopBar(M->curChatTitle);

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
    const bool isGroup = M->curChatKey.startsWith("g:");
    int64_t prevDate = 0;
    bool prevFromMe = false;
    String prevSender;
    bool first = true;

    for (const BBMsg& m : M->msgs) {
        // Étiquette machine « QUI HH:MM » (direction D) : une seule mécanique
        // remplace le séparateur horaire ET l'en-tête d'expéditeur — posée au
        // changement de voix, ou après un quart d'heure de silence.
        bool voiceChange = first || m.fromMe != prevFromMe ||
                           (isGroup && !m.fromMe && m.sender != prevSender);
        bool longGap = !first && m.date && prevDate && m.date - prevDate > 15 * 60000LL;
        if (voiceChange || longGap) {
            Block h;
            h.senderHdr = true;
            h.sent = m.fromMe;  // aligne l'étiquette du côté de sa bulle
            String who = m.fromMe ? String(T(S_ME_CAPS))
                                  : (isGroup && m.sender.length() ? m.sender : M->curChatTitle);
            h.sepText = upperLabel(who) + " " + timeShort(m.date);
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
    // 4 px de respiration avant le chrome du bas : la dernière bulle ne
    // touche jamais le panneau, et son ombre (2 px) vit dans cet espace.
    int areaBot = SCREEN_H - HINT_H - composeH - 4;
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
        M->msgStops = bbScrollStops(info.data(), (int)info.size(), areaH, stops, STOPS_MAX);
    }
    if (M->msgScroll >= M->msgStops) M->msgScroll = M->msgStops - 1;
    if (M->msgScroll < 0) M->msgScroll = 0;
    int scrollPx = stops[M->msgScroll];

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

        if (b.senderHdr) {
            C->setFont(&fonts::Font0);
            String lbl = fitText(b.sepText, BUB_MAXW);
            int lx = b.sent ? SCREEN_W - EDGE - C->textWidth(lbl) : EDGE + 1;
            C->setTextColor(C_SLATE300, C_INK900);
            C->setCursor(lx, y + 2);
            C->print(lbl);
            C->setFont(&fonts::efontJA_12);
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
    C->fillRect(0, BAR_H, SCREEN_W, areaTop - BAR_H, C_INK900);
    C->fillRect(0, 0, SCREEN_W, BAR_H, C_INK800);
    drawTopBar(M->curChatTitle);

    if (blocks.empty()) {
        C->setTextColor(C_SLATE300, C_INK900);
        String e = M->statusView.length() ? "" : T(S_EMPTY_CHAT);
        if (e.length()) {
            C->setCursor((SCREEN_W - C->textWidth(e)) / 2, 60);
            C->print(e);
        }
    }

    if (composeMode) {
        // Le champ est surélevé et cerné de bleu : il pousse la conversation
        // vers le haut au lieu de la recouvrir — on voit à qui l'on répond.
        int cy = SCREEN_H - HINT_H - composeH;
        C->fillRect(0, cy, SCREEN_W, composeH, C_INK800);
        C->fillRoundRect(4, cy + 3, SCREEN_W - 8, 18, 6, C_INK700);
        C->drawRoundRect(4, cy + 3, SCREEN_W - 8, 18, 6, C_BLUE400);
        String shown = M->compose;
        while (richWidth(shown) > SCREEN_W - 24 && shown.length())
            shown = shown.substring(1);
        drawRich(9, cy + 6, shown, C_WHITE, C_INK700);
        C->fillRect(9 + richWidth(shown) + 1, cy + 6, 1, 12, C_BLUE400);
    }

    drawHintBar(composeMode ? T(S_HINT_COMPOSE) : T(S_HINT_MSGS));
    drawStatusLine(M->statusView);
}

static void drawInfo() {
    C->fillSprite(C_INK900);
    drawTopBar(T(S_INFO));
    int y = BAR_H + 4;
    auto row = [&](const String& k, const String& v, uint16_t col = C_WHITE) {
        C->setFont(&fonts::Font0);
        C->setTextColor(C_SLATE300, C_INK900);
        C->setCursor(6, y + 3);
        C->print(upperLabel(k));
        C->setFont(&fonts::efontJA_12);
        C->setTextColor(col, C_INK900);
        C->setCursor(78, y);
        C->print(fitText(v, SCREEN_W - 84));
        y += 13;
    };
    row(T(S_VERSION), M->version);
    row("WiFi", M->ssid + "  " + String(M->rssi) + " dBm");
    row("IP", M->ip);
    row(T(S_CONFIG), "cardputer.local");
    row(T(S_SERVER), gConfig.serverUrl);
    row(T(S_SYNC), M->marker ? timeShort(M->marker) : "—", M->synced ? C_GREEN400 : C_AMBER400);
    row(T(S_HISTORY), String(gConfig.histDepth) + " " + T(S_MESSAGES_UNIT));
    row(T(S_TLS), gConfig.tlsVerify ? T(S_TLS_ON) : T(S_TLS_OFF),
        gConfig.tlsVerify ? C_GREEN400 : C_AMBER400);
    drawHintBar(T(S_HINT_INFO));
    drawStatusLine(M->status);
}

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

static void drawSettings() {
    C->fillSprite(C_INK900);
    drawTopBar(T(S_SETTINGS));

    const int rowH = 13;
    const int visible = 6;
    int top = 0;
    if (M->setSel >= visible) top = M->setSel - visible + 1;
    int y = BAR_H + 3;
    for (int i = top; i < (int)SET_COUNT && i < top + visible; i++) {
        bool sel = (i == M->setSel);
        if (sel) {
            C->fillRect(0, y - 1, SCREEN_W, rowH, C_INK600);
            C->fillRect(0, y - 1, 2, rowH, C_BLUE400);
        }
        uint16_t bg = sel ? C_INK600 : C_INK900;
        C->setFont(&fonts::Font0);
        C->setTextColor(sel ? C_WHITE : C_SLATE300, bg);
        C->setCursor(7, y + 3);
        C->print(upperLabel(T(setLabel(i))));
        C->setFont(&fonts::efontJA_12);
        C->setTextColor(sel ? C_BLUE400 : C_SLATE200, bg);
        String v = setValueText(i);
        C->setCursor(SCREEN_W - 7 - C->textWidth(v), y);
        C->print(v);
        y += rowH;
    }
    // Rappel de la frontière avec le portail : ce qui ne se règle pas ici.
    C->setTextColor(C_SLATE300, C_INK900);
    String note = fitText(T(S_PORTAL_ONLY), SCREEN_W - 14);
    C->setCursor(7, SCREEN_H - HINT_H - 13);
    C->print(note);
    drawHintBar(T(S_HINT_SETTINGS));
    drawStatusLine(M->status);
}

void uiRender(BbCanvas& canvas, UiModel& m) {
    C = &canvas;
    M = &m;
    C->setTextFont(&fonts::efontJA_12);
    C->setTextSize(1);
    switch (M->screen) {
        case SCR_SPLASH:   drawSplash(); break;
        case SCR_SETUP:    drawSetup(); break;
        // Plein écran de calibration si la liste est vide (synchro initiale) ;
        // sinon la liste en cache reste visible sous le modal de progression.
        case SCR_CHATS:    if (M->calibrating && M->chats.empty()) drawCalibrating();
                           else { drawChats(); if (M->calibrating) drawCalibModal(); }
                           break;
        case SCR_MESSAGES: drawMessages(false); break;
        case SCR_COMPOSE:  drawMessages(true); break;
        case SCR_INFO:     drawInfo(); break;
        case SCR_SETTINGS: drawSettings(); break;
    }
}

