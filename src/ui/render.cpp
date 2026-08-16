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

static void drawTopBar(const String& title) {
    C->fillRect(0, 0, SCREEN_W, BAR_H, C_INK800);
    C->setTextColor(C_BLUE400, C_INK800);
    C->setCursor(5, 2);
    C->print(fitText(title, 168));

    int batt = M->battery;
    String right = String(batt) + "%";
    C->setTextColor(batt < 20 ? C_RED400 : C_SLATE300, C_INK800);
    C->setCursor(SCREEN_W - C->textWidth(right) - 5, 2);
    C->print(right);

    // Point d'état réseau : bleu quand la synchro est fraîche, ambre pendant
    // le rattrapage, rouge hors ligne. Trois pixels valent une phrase.
    uint16_t dot = !M->wifiOk ? C_RED400 : (M->synced ? C_BLUE400 : C_AMBER400);
    C->fillCircle(SCREEN_W - C->textWidth(right) - 12, 8, 2, dot);
}

// Barre d'aide : les raccourcis, toujours au même endroit.
static void drawHintBar(const String& hint) {
    int y = SCREEN_H - HINT_H;
    C->fillRect(0, y, SCREEN_W, HINT_H, C_INK800);
    C->setTextColor(C_SLATE300, C_INK800);
    C->setCursor(5, y + 1);
    C->print(fitText(hint, SCREEN_W - 10));
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
    C->fillRect(0, y, SCREEN_W, HINT_H, C_INK800);
    C->fillRect(0, y, 2, HINT_H, col);
    C->setTextColor(col, C_INK800);
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
    if (w > 0) C->fillRoundRect(50, 82, w, 5, 2, C_BLUE400);

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
    C->fillRoundRect(x, y, w, h, 5, C_INK800);
    C->drawRoundRect(x, y, w, h, 5, C_BLUE400);

    C->setTextColor(C_BLUE400, C_INK800);
    String t = T(S_CALIBRATION);
    C->setCursor(x + (w - C->textWidth(t)) / 2, y + 8);
    C->print(t);

    int total = M->calibTotal ? M->calibTotal : 1;
    int done = min((int)M->calibPage, total);
    C->fillRoundRect(x + 22, y + 27, w - 44, 5, 2, C_INK600);
    int bw = (w - 44) * done / total;
    if (bw > 0) C->fillRoundRect(x + 22, y + 27, bw, 5, 2, C_BLUE400);

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
        drawRich(nx, y + 2, fitText(c.title, SCREEN_W - nx - C->textWidth(ts) - 12),
                 unread ? C_WHITE : C_SLATE200, bg);

        C->setTextColor(C_SLATE300, bg);
        C->setCursor(SCREEN_W - C->textWidth(ts) - 5, y + 2);
        C->print(ts);

        String preview = (c.lastFromMe ? String(T(S_ME_PREFIX)) : String("")) + c.lastText;
        preview.replace("\n", " ");
        drawRich(nx, y + 14, fitText(preview, SCREEN_W - nx - 6), C_SLATE300, bg);

        if (i < visible - 1 && idx + 1 < (int)M->chats.size() && !sel)
            C->drawFastHLine(EDGE, y + rowH - 1, SCREEN_W - EDGE * 2, C_INK700);
    }
    drawHintBar(T(S_HINT_CHATS));
    drawStatusLine(M->status);
}

// Une bulle : rectangle arrondi + ergot de 3 px sur le coin bas extérieur.
// C'est l'ergot qui fait « messagerie » plutôt que « liste ».
static void drawBubble(int x, int y, int w, int h, bool sent, uint16_t fill) {
    C->fillRoundRect(x, y, w, h, BUB_R, fill);
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

        if (b.separator) {
            C->setTextColor(C_SLATE300, C_INK900);
            C->setCursor((SCREEN_W - C->textWidth(b.sepText)) / 2, y + 1);
            C->print(b.sepText);
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
        C->setTextColor(C_SLATE300, C_INK900);
        C->setCursor(6, y);
        C->print(k);
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
        C->setTextColor(sel ? C_WHITE : C_SLATE300, bg);
        C->setCursor(7, y);
        C->print(T(setLabel(i)));
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

