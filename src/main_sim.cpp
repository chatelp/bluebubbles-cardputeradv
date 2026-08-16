// Main simulateur macOS — LovyanGFX + SDL2 (Panel_sdl), fenêtre ×3.
// Même patron que geek-casino_cardputeradv :
//
//   (aucune option)              fenêtre interactive
//   --screens <dir>              une image BMP par écran, sans fenêtre
//   --lang fr|en                 langue des captures (défaut : en)
//
// Le simulateur ne parle à AUCUN serveur : il rend un jeu de données de
// démonstration (contacts inventés) — c'est aussi ce qui garantit que les
// captures ne contiennent jamais une donnée personnelle.
//
// Clavier interactif : flèches = ; . , /   Entrée = OK   Échap = `
// r recalibrer (factice), s réglages, p infos, texte libre en composition.
#ifdef SIM_BUILD

#include <SDL.h>

#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>

#include <lgfx/v1/platforms/sdl/Panel_sdl.hpp>

#include "app_config.h"
#include "i18n.h"
#include "ui/render.h"
#include "ui/theme.h"
#include "bb_emoji.h"
#include "emoji_art.h"

// Globals que le firmware définit ailleurs (app_config.cpp, non compilé ici).
AppConfig gConfig;
uint8_t gLang = LANG_EN;

uint32_t millis() { return SDL_GetTicks(); }

namespace {

class SimDisplay : public lgfx::LGFX_Device {
    lgfx::Panel_sdl _panel;

    bool init_impl(bool /*use_reset*/, bool use_clear) override {
        return LGFX_Device::init_impl(false, use_clear);
    }

public:
    SimDisplay() {
        auto cfg = _panel.config();
        cfg.memory_width = SCREEN_W;
        cfg.panel_width = SCREEN_W;
        cfg.memory_height = SCREEN_H;
        cfg.panel_height = SCREEN_H;
        _panel.config(cfg);
        _panel.setScaling(3, 3);
        _panel.setWindowTitle("BlueBubbles Cardputer — sim");
        setPanel(&_panel);
    }
};

// ---------------------------------------------------------------- démo
// Un même instant de référence pour toutes les dates : les captures sont
// déterministes (l'heure affichée dépend du fuseau, fixé dans main()).
int64_t demoNow() {
    struct tm t = {};
    t.tm_year = 2026 - 1900; t.tm_mon = 7; t.tm_mday = 16;
    t.tm_hour = 18; t.tm_min = 42;
    t.tm_isdst = -1;  // laisse mktime trancher l'heure d'été
    return (int64_t)mktime(&t) * 1000;
}

BBChat mkChat(const char* key, const char* title, const char* last, int64_t date,
              bool fromMe) {
    BBChat c;
    c.guid = String("iMessage;-;") + key;
    c.key = String("p:") + key;
    c.title = title;
    c.lastText = last;
    c.lastDate = date;
    c.lastFromMe = fromMe;
    return c;
}

BBMsg mkMsg(const char* text, int64_t date, bool fromMe, const char* sender = "") {
    BBMsg m;
    m.guid = String("demo-") + String((long long)date);
    m.text = text;
    m.date = date;
    m.fromMe = fromMe;
    m.sender = sender;
    return m;
}

void fillDemo(UiModel& m) {
    const int64_t now = demoNow();
    const int64_t MIN = 60000, H = 3600000, D = 86400000;

    m.chats.clear();
    m.chats.push_back(mkChat("621043678", "Camille", "On dit 20h au Vieux Port ? 🎉", now - 4 * MIN, false));
    m.chats.push_back(mkChat("633728194", "Papa", "Bien recu, merci fiston 👍", now - 52 * MIN, false));
    m.chats.push_back(mkChat("677301582", "Lucie", "J'apporte le dessert 🍰", now - 3 * H, true));
    m.chats.push_back(mkChat("606459217", "Antoine B.", "Le PCB v2 est parti en prod 🔥", now - 26 * H, false));
    m.chats.push_back(mkChat("688112430", "Maman", "Appelle-moi quand tu peux ❤️", now - 2 * D, false));
    m.chats[2].lastFromMe = true;
    // Non-lus : Camille (récente) et Maman.
    for (auto& c : m.chats) m.seen[c.key] = c.lastDate;
    m.seen[m.chats[0].key] = 0;
    m.seen[m.chats[4].key] = 0;

    m.msgs.clear();
    m.msgs.push_back(mkMsg("Tu fais quoi ce soir ?", now - 61 * MIN, false));
    m.msgs.push_back(mkMsg("Rien de prevu, pourquoi ?", now - 58 * MIN, true));
    m.msgs.push_back(mkMsg("On se fait une bouillabaisse avec Lucie et Antoine 😄", now - 24 * MIN, false));
    m.msgs.push_back(mkMsg("Grosse journee, j'ai flashe le firmware 12 fois 😅", now - 22 * MIN, true));
    m.msgs.back().taps[3] = 1;  // 😂 sur le message envoyé
    m.msgs.push_back(mkMsg("Raison de plus pour venir !", now - 20 * MIN, false));
    m.msgs.push_back(mkMsg("Ok je passe vers 19h30 🍷", now - 6 * MIN, true));
    m.msgs.back().taps[0] = 2;  // ❤️ ×2
    m.msgs.push_back(mkMsg("On dit 20h au Vieux Port ? 🎉", now - 4 * MIN, false));

    m.curChatTitle = "Camille";
    m.curChatKey = "p:621043678";
    m.compose = "J'arrive vers 19h30 🎉";

    m.battery = 84;
    m.wifiOk = true;
    m.rssi = -48;
    m.ssid = "HomeWiFi";
    m.ip = "192.168.1.30";
    m.apSsid = "CardputerBB";
    m.apPass = "bluebubbles";
    m.marker = now - 4 * MIN;
    m.synced = true;
    m.version = "0.12.0";

    gConfig.serverUrl = "https://my-server.example.com";
    gConfig.histDepth = 10;
    gConfig.pollSec = 10;
    gConfig.tlsVerify = true;
    gConfig.sndVolume = 60;
    gConfig.sndKeys = gConfig.sndSend = gConfig.sndRecv = gConfig.sndNotif = true;
}

// ------------------------------------------------------------- captures
// BMP 24 bits. readRect rend du RGB565 à convertir octet par octet.
bool writeBmp(lgfx::LGFX_Sprite& g, const std::string& path) {
    const int w = g.width(), h = g.height();
    static uint16_t px[SCREEN_W * SCREEN_H];
    g.readRect(0, 0, w, h, px);

    const uint32_t rowBytes = ((w * 3 + 3) / 4) * 4;
    uint8_t hdr[54] = {'B', 'M'};
    auto put32 = [&](int off, uint32_t v) {
        hdr[off] = v; hdr[off + 1] = v >> 8; hdr[off + 2] = v >> 16; hdr[off + 3] = v >> 24;
    };
    put32(2, 54 + rowBytes * h); put32(10, 54); put32(14, 40);
    put32(18, (uint32_t)w); put32(22, (uint32_t)h);
    hdr[26] = 1; hdr[28] = 24;
    put32(34, rowBytes * h);

    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fwrite(hdr, 1, 54, f);
    static uint8_t row[((SCREEN_W * 3 + 3) / 4) * 4];
    for (int y = h - 1; y >= 0; y--) {
        std::memset(row, 0, sizeof(row));
        for (int x = 0; x < w; x++) {
            uint16_t v = px[y * w + x];
            v = (uint16_t)((v >> 8) | (v << 8));  // octets inversés (cf. geek-casino)
            row[x * 3 + 0] = (uint8_t)((v & 0x1F) << 3);         // B
            row[x * 3 + 1] = (uint8_t)(((v >> 5) & 0x3F) << 2);  // G
            row[x * 3 + 2] = (uint8_t)(((v >> 11) & 0x1F) << 3); // R
        }
        std::fwrite(row, 1, rowBytes, f);
    }
    std::fclose(f);
    return true;
}

int runScreens(const std::string& dir) {
    lgfx::LGFX_Sprite canvas;  // sans parent : aucun écran nécessaire
    canvas.setColorDepth(16);
    if (!canvas.createSprite(SCREEN_W, SCREEN_H)) return 1;

    UiModel m;
    fillDemo(m);
    auto shot = [&](const char* name) {
        uiRender(canvas, m);
        std::string path = dir + "/" + name + ".bmp";
        if (!writeBmp(canvas, path)) { std::fprintf(stderr, "echec: %s\n", path.c_str()); return; }
        std::printf("%s\n", path.c_str());
    };

    m.screen = SCR_CHATS;    shot("01-chats");
    m.screen = SCR_MESSAGES; shot("02-messages");
    m.msgScroll = 2;         shot("03-messages-scrolled");
    m.msgScroll = 0;
    m.screen = SCR_COMPOSE;  shot("04-compose");
    m.screen = SCR_INFO;     shot("05-info");
    m.screen = SCR_SETTINGS; m.setSel = 1; shot("06-settings");
    m.screen = SCR_CHATS;
    m.calibrating = true; m.calibPage = 9; m.calibTotal = 30; shot("07-calibrating-modal");
    m.chats.clear(); shot("08-calibrating-initial");
    m.calibrating = false;
    fillDemo(m);
    m.screen = SCR_SETUP;    shot("09-setup");
    m.screen = SCR_SPLASH;
    m.calibrating = true; m.calibPage = 12; m.calibTotal = 30; m.synced = false;
    shot("10-splash-sync");
    m.wifiOk = false; m.calibrating = false; m.calibPage = 0;
    m.status = String(T(S_CONNECTING_TO)) + "HomeWiFi…";
    shot("11-splash-connect");
    return 0;
}


// ---------------------------------------------------------- maquettes UI
// Trois directions pour l'identité visuelle (décision PO en cours), rendues
// par le vrai moteur (efont, 240×135) sur l'écran de conversation. Ce sont
// des ESQUISSES : le code n'a pas vocation à être propre, mais à montrer.
namespace mock {

void emoji(lgfx::LGFX_Sprite& g, int x, int y, uint32_t cp) {
    int gi = bbEmojiGlyph(cp);
    if (gi < 0) return;
    const uint8_t* art = kEmojiArt[gi];
    for (int dy = 0; dy < kEmojiPx; dy++)
        for (int dx = 0; dx < kEmojiPx; dx++) {
            uint8_t v = art[dy * kEmojiPx + dx];
            if (v) g.drawPixel(x + dx, y + dy, kEmojiPalette[v]);
        }
}

// A — « Matière et profondeur » : encre et bulle poussé à fond.
void matiere(lgfx::LGFX_Sprite& g) {
    g.fillSprite(C_INK900);
    // Barre : titre + filet dégradé bleu, pastille d'état avec halo.
    g.fillRect(0, 0, 240, 16, C_INK800);
    g.setTextColor(C_WHITE, C_INK800);
    g.setCursor(6, 2); g.print("Camille");
    for (int i = 0; i < 60; i++)
        g.drawPixel(6 + i, 14, i < 42 ? C_BLUE400 : C_INK700);
    g.fillCircle(222, 8, 3, C_BLUE500); g.drawCircle(222, 8, 4, C_INK600);
    g.setTextColor(C_SLATE300, C_INK800); g.setCursor(196, 2); g.print("84");

    auto bubble = [&](int x, int y, int w, int h, bool sent) {
        g.fillRoundRect(x + 1, y + 2, w, h, 7, 0x0041);          // ombre portée
        uint16_t fill = sent ? C_BLUE500 : C_INK700;
        g.fillRoundRect(x, y, w, h, 7, fill);
        uint16_t hi = sent ? 0x449F : 0x2189;                     // lueur haute
        g.drawFastHLine(x + 6, y + 1, w - 12, hi);
        if (sent) g.fillTriangle(x + w - 5, y + h - 1, x + w + 2, y + h - 1, x + w - 1, y + h - 6, fill);
        else g.fillTriangle(x + 5, y + h - 1, x - 2, y + h - 1, x + 1, y + h - 6, fill);
    };
    g.setTextColor(C_SLATE300, C_INK900);
    g.setCursor(102, 20); g.print("18:20");
    bubble(6, 32, 152, 19, false);
    g.setTextColor(C_WHITE, C_INK700); g.setCursor(11, 35); g.print("Raison de plus pour venir !");
    bubble(78, 60, 156, 19, true);
    g.setTextColor(C_WHITE, C_BLUE500); g.setCursor(83, 63); g.print("Ok je passe vers 19h30 ");
    emoji(g, 83 + 138, 63, 0x1F377);
    // pilule de réactions, ombrée elle aussi
    g.fillRoundRect(69, 53, 34, 15, 7, 0x0041);
    g.fillRoundRect(68, 52, 34, 15, 7, C_INK800);
    g.drawRoundRect(68, 52, 34, 15, 7, C_INK600);
    emoji(g, 72, 54, 0x2764);
    g.setTextColor(C_SLATE300, C_INK800); g.setCursor(86, 54); g.print("x2");
    bubble(6, 88, 168, 19, false);
    g.setTextColor(C_WHITE, C_INK700); g.setCursor(11, 91); g.print("On dit 20h au Vieux Port ? ");
    emoji(g, 11 + 156, 91, 0x1F389);
    g.fillRect(0, 122, 240, 13, C_INK800);
    g.setTextColor(C_SLATE300, C_INK800); g.setCursor(5, 123); g.print(";. defiler  OK ecrire  ` retour");
}

// B — « Rétro-communicateur » : pager/BlackBerry, chrome d'appareil.
void retro(lgfx::LGFX_Sprite& g) {
    const uint16_t BG = 0x10E4, PANEL = 0x2189, EDGE_L = 0x4B0D, EDGE_D = 0x0862;
    const uint16_t TXT = 0xC618, BRIGHT = 0xFFFF, ACC = 0xFD84;  // ambre
    g.fillSprite(BG);
    // Barre biseautée + antenne/signal + batterie dessinée
    g.fillRect(0, 0, 240, 17, PANEL);
    g.drawFastHLine(0, 0, 240, EDGE_L); g.drawFastHLine(0, 16, 240, EDGE_D);
    g.setTextColor(BRIGHT, PANEL); g.setCursor(24, 2); g.print("CAMILLE");
    g.fillCircle(10, 8, 3, ACC);  // LED message
    for (int b = 0; b < 4; b++) g.fillRect(176 + b * 5, 11 - b * 2, 3, 3 + b * 2, b < 3 ? TXT : EDGE_D);
    g.drawRect(206, 4, 22, 9, TXT); g.fillRect(228, 6, 2, 5, TXT); g.fillRect(208, 6, 14, 5, ACC);
    auto panel = [&](int x, int y, int w, int h, bool sent) {
        g.fillRect(x, y, w, h, sent ? 0x21A9 : PANEL);
        g.drawRect(x, y, w, h, sent ? ACC : EDGE_L);
        g.drawFastVLine(x, y, h, sent ? ACC : EDGE_L);
    };
    g.setTextColor(ACC, BG); g.setCursor(6, 21); g.print("CAMILLE  18:20");
    panel(6, 32, 170, 17, false);
    g.setTextColor(BRIGHT, PANEL); g.setCursor(11, 34); g.print("Raison de plus pour venir !");
    g.setTextColor(ACC, BG); g.setCursor(155, 53); g.print("MOI  18:36");
    panel(64, 64, 170, 17, true);
    g.setTextColor(BRIGHT, 0x21A9); g.setCursor(69, 66); g.print("Ok je passe vers 19h30 ");
    emoji(g, 69 + 138, 66, 0x1F377);
    g.setTextColor(TXT, BG); g.setCursor(64, 83); g.print("[");
    emoji(g, 71, 82, 0x2764);
    g.setTextColor(TXT, BG); g.setCursor(84, 83); g.print("x2]  DISTRIBUE");
    g.setTextColor(ACC, BG); g.setCursor(6, 96); g.print("CAMILLE  18:38");
    panel(6, 107, 186, 17, false);
    g.setTextColor(BRIGHT, PANEL); g.setCursor(11, 109); g.print("On dit 20h au Vieux Port ? ");
    emoji(g, 11 + 156, 109, 0x1F389);
    g.fillRect(0, 126, 240, 9, PANEL);
    g.drawFastHLine(0, 126, 240, EDGE_L);
    g.setTextColor(TXT, PANEL); g.setFont(&fonts::Font0);
    g.setCursor(4, 127); g.print("MSG 07/30        NOUVEAU: 2        18:42");
    g.setFont(&fonts::efontJA_12);
}

// C — « Terminal phosphore » : monochrome vert, IRC dans un CRT.
void terminal(lgfx::LGFX_Sprite& g) {
    const uint16_t BG = 0x0060, DIM = 0x02E0, MID = 0x05C0, HOT = 0x07E8;
    g.fillSprite(BG);
    g.setTextColor(MID, BG); g.setCursor(4, 2);
    g.print("== imsg://camille ==================");
    auto line = [&](int y, const char* who, const char* txt, bool me) {
        g.setTextColor(me ? HOT : MID, BG);
        g.setCursor(4, y); g.print(who);
        g.setTextColor(me ? MID : DIM, BG);
        g.setCursor(4 + g.textWidth(who), y); g.print(txt);
    };
    g.setTextColor(DIM, BG); g.setCursor(4, 18); g.print("--- 18:20 ---");
    line(32, "<camille> ", "Raison de plus pour venir !", false);
    line(46, "<moi>     ", "Ok je passe vers 19h30", true);
    g.setTextColor(DIM, BG); g.setCursor(4, 60); g.print("          * camille a reagi ");
    g.setCursor(4 + 168, 60); g.print("<3 x2");
    line(74, "<camille> ", "On dit 20h au Vieux Port ?", false);
    g.setTextColor(DIM, BG); g.setCursor(4, 102); g.print("----------------------------------------");
    g.setTextColor(HOT, BG); g.setCursor(4, 114); g.print("> J'arrive vers 19h3");
    g.fillRect(4 + g.textWidth("> J'arrive vers 19h3") + 1, 114, 6, 12, HOT);
    // (pas de scanlines : sans alpha, elles effacent le texte — l'effet CRT
    // se ferait par post-traitement du sprite, à juger plus tard)
    g.setTextColor(DIM, BG); g.setCursor(4, 126); g.print("[240x135] [wifi:ok] [batt:84%]");
}


// D — Hybride : le chrome d'appareil de B, la matière de bulles de A.
// Règle de couleur : l'appareil parle AMBRE (LED, compteurs, accusés),
// les gens parlent BLEU (bulles). Jamais les deux sur le même élément.
void hybride(lgfx::LGFX_Sprite& g) {
    const uint16_t PANEL = 0x1926, EDGE_L = 0x3A8C, EDGE_D = 0x0862;
    const uint16_t ACC = 0xFD84;  // ambre machine
    g.fillSprite(C_INK900);

    // Barre biseautée : LED, titre en capitales, signal + batterie dessinés.
    g.fillRect(0, 0, 240, 17, PANEL);
    g.drawFastHLine(0, 0, 240, EDGE_L);
    g.drawFastHLine(0, 16, 240, EDGE_D);
    g.fillCircle(9, 8, 3, ACC);
    g.drawCircle(9, 8, 4, EDGE_D);
    g.setTextColor(C_WHITE, PANEL); g.setCursor(20, 2); g.print("CAMILLE");
    for (int b = 0; b < 4; b++) g.fillRect(178 + b * 4, 12 - b * 2 - 3, 2, 3 + b * 2, b < 4 ? C_SLATE300 : EDGE_D);
    g.drawRect(200, 4, 20, 9, C_SLATE300); g.fillRect(220, 6, 2, 5, C_SLATE300);
    g.fillRect(202, 6, 13, 5, C_SLATE300);

    // Bulles de A : ombre portée, lueur haute, coins ronds — sous des
    // étiquettes de B : QUI et QUAND en petites capitales grises.
    auto bubble = [&](int x, int y, int w, int h, bool sent) {
        g.fillRoundRect(x + 1, y + 2, w, h, 7, 0x0041);
        uint16_t fill = sent ? C_BLUE500 : C_INK700;
        g.fillRoundRect(x, y, w, h, 7, fill);
        g.drawFastHLine(x + 6, y + 1, w - 12, sent ? 0x449F : 0x2189);
        if (sent) g.fillTriangle(x + w - 5, y + h - 1, x + w + 2, y + h - 1, x + w - 1, y + h - 6, fill);
        else g.fillTriangle(x + 5, y + h - 1, x - 2, y + h - 1, x + 1, y + h - 6, fill);
    };
    g.setFont(&fonts::Font0);
    g.setTextColor(C_SLATE300, C_INK900); g.setCursor(7, 22); g.print("CAMILLE 18:20");
    g.setFont(&fonts::efontJA_12);
    bubble(6, 31, 152, 19, false);
    g.setTextColor(C_WHITE, C_INK700); g.setCursor(11, 34); g.print("Raison de plus pour venir !");

    g.setFont(&fonts::Font0);
    g.setTextColor(C_SLATE300, C_INK900); g.setCursor(180, 57); g.print("MOI 18:36");
    g.setFont(&fonts::efontJA_12);
    bubble(78, 66, 156, 19, true);
    g.setTextColor(C_WHITE, C_BLUE500); g.setCursor(83, 69); g.print("Ok je passe vers 19h30 ");
    emoji(g, 83 + 138, 69, 0x1F377);
    // pilule de réactions (matière A)
    g.fillRoundRect(69, 59, 34, 15, 7, 0x0041);
    g.fillRoundRect(68, 58, 34, 15, 7, C_INK800);
    g.drawRoundRect(68, 58, 34, 15, 7, C_INK600);
    emoji(g, 72, 60, 0x2764);
    g.setTextColor(C_SLATE300, C_INK800); g.setCursor(86, 60); g.print("x2");
    // accusé : la machine parle ambre, en petites capitales
    g.setFont(&fonts::Font0);
    g.setTextColor(ACC, C_INK900); g.setCursor(180, 88); g.print("DISTRIBUE");
    g.setFont(&fonts::efontJA_12);

    g.setFont(&fonts::Font0);
    g.setTextColor(C_SLATE300, C_INK900); g.setCursor(7, 96); g.print("CAMILLE 18:38");
    g.setFont(&fonts::efontJA_12);
    bubble(6, 105, 174, 19, false);
    g.setTextColor(C_WHITE, C_INK700); g.setCursor(11, 108); g.print("On dit 20h au Vieux Port ? ");
    emoji(g, 11 + 156, 108, 0x1F389);

    // Pied de page machine : compteurs en Font0, ambre sur panneau biseauté.
    g.fillRect(0, 127, 240, 8, PANEL);
    g.drawFastHLine(0, 127, 240, EDGE_L);
    g.setFont(&fonts::Font0);
    g.setTextColor(C_SLATE300, PANEL); g.setCursor(4, 128);
    g.print("MSG 07/30");
    g.setTextColor(ACC, PANEL); g.setCursor(96, 128); g.print("NOUVEAU: 2");
    g.setTextColor(C_SLATE300, PANEL); g.setCursor(206, 128); g.print("18:42");
    g.setFont(&fonts::efontJA_12);
}

}  // namespace mock

int runMocks(const std::string& dir) {
    lgfx::LGFX_Sprite g;
    g.setColorDepth(16);
    if (!g.createSprite(SCREEN_W, SCREEN_H)) return 1;
    g.setTextFont(&fonts::efontJA_12);
    auto save = [&](const char* n) {
        std::string path = dir + "/" + n + ".bmp";
        writeBmp(g, path); std::printf("%s\n", path.c_str());
    };
    mock::matiere(g);  save("A-matiere");
    mock::retro(g);    save("B-retro");
    mock::terminal(g); save("C-terminal");
    mock::hybride(g);  save("D-hybride");
    return 0;
}

// ------------------------------------------------------------ interactif
struct KeyEdge {
    bool prev[SDL_NUM_SCANCODES] = {false};
    bool pressed(const uint8_t* st, SDL_Scancode sc) {
        const bool down = st[sc] != 0;
        const bool edge = down && !prev[sc];
        prev[sc] = down;
        return edge;
    }
};

UiModel gM;

void simAdjust(uint8_t f, int delta) {
    switch (f) {
        case SET_LANG:
            gConfig.lang = (gConfig.lang == LANG_FR) ? LANG_EN : LANG_FR;
            gLang = gConfig.lang;
            break;
        case SET_VOL:  gConfig.sndVolume = (uint8_t)max(0, min(100, (int)gConfig.sndVolume + delta * 10)); break;
        case SET_KEYS: gConfig.sndKeys = !gConfig.sndKeys; break;
        case SET_SEND: gConfig.sndSend = !gConfig.sndSend; break;
        case SET_RECV: gConfig.sndRecv = !gConfig.sndRecv; break;
        case SET_NOTIF: gConfig.sndNotif = !gConfig.sndNotif; break;
        case SET_POLL: gConfig.pollSec = (uint16_t)max(3, min(120, (int)gConfig.pollSec + delta * 5)); break;
        case SET_HIST: gConfig.histDepth = (uint8_t)max(4, min(15, (int)gConfig.histDepth + delta)); break;
    }
}

int simRun(bool* running) {
    SimDisplay display;
    display.init();
    lgfx::LGFX_Sprite canvas(&display);
    canvas.setColorDepth(16);
    canvas.createSprite(SCREEN_W, SCREEN_H);

    fillDemo(gM);
    KeyEdge edge;
    SDL_StartTextInput();

    while (*running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) *running = false;
            if (ev.type == SDL_TEXTINPUT && gM.screen == SCR_COMPOSE) {
                if (ev.text.text[0] != '`') gM.compose += ev.text.text;
            }
        }
        const uint8_t* st = SDL_GetKeyboardState(nullptr);
        UiModel& m = gM;
        const bool composing = m.screen == SCR_COMPOSE;

        if (edge.pressed(st, SDL_SCANCODE_UP)) {
            if (m.screen == SCR_CHATS && m.chatSel > 0) m.chatSel--;
            else if (m.screen == SCR_MESSAGES) m.msgScroll = min(m.msgScroll + 1, m.msgStops - 1);
            else if (m.screen == SCR_SETTINGS && m.setSel > 0) m.setSel--;
        }
        if (edge.pressed(st, SDL_SCANCODE_DOWN)) {
            if (m.screen == SCR_CHATS && m.chatSel < (int)m.chats.size() - 1) m.chatSel++;
            else if (m.screen == SCR_MESSAGES) m.msgScroll = max(0, m.msgScroll - 1);
            else if (m.screen == SCR_SETTINGS && m.setSel < (int)SET_COUNT - 1) m.setSel++;
        }
        if (edge.pressed(st, SDL_SCANCODE_LEFT) && m.screen == SCR_SETTINGS) simAdjust(m.setSel, -1);
        if (edge.pressed(st, SDL_SCANCODE_RIGHT) && m.screen == SCR_SETTINGS) simAdjust(m.setSel, +1);
        if (edge.pressed(st, SDL_SCANCODE_RETURN)) {
            if (m.screen == SCR_CHATS && !m.chats.empty()) {
                m.curChatTitle = m.chats[m.chatSel].title;
                m.curChatKey = m.chats[m.chatSel].key;
                m.msgScroll = 0;
                m.screen = SCR_MESSAGES;
                m.seen[m.curChatKey] = m.chats[m.chatSel].lastDate;
            } else if (m.screen == SCR_MESSAGES) {
                m.screen = SCR_COMPOSE;
            } else if (composing) {
                if (m.compose.length()) {
                    BBMsg out = mkMsg("", demoNow(), true);
                    out.text = m.compose;
                    m.msgs.push_back(out);
                    m.compose = "";
                }
                m.screen = SCR_MESSAGES;
            }
        }
        if (edge.pressed(st, SDL_SCANCODE_BACKSPACE) && composing && m.compose.length()) {
            String s = m.compose;  // recule d'un caractère UTF-8 entier
            size_t n = s.length();
            while (n > 0 && ((uint8_t)s[n - 1] & 0xC0) == 0x80) n--;
            if (n > 0) n--;
            m.compose = s.substring(0, n);
        }
        if (edge.pressed(st, SDL_SCANCODE_ESCAPE) || edge.pressed(st, SDL_SCANCODE_GRAVE)) {
            if (composing) m.screen = SCR_MESSAGES;
            else if (m.screen == SCR_MESSAGES) m.screen = SCR_CHATS;
            else if (m.screen == SCR_SETTINGS) m.screen = SCR_INFO;
            else if (m.screen == SCR_INFO) m.screen = SCR_CHATS;
            else if (m.screen == SCR_CHATS) m.screen = SCR_INFO;
        }
        if (!composing) {
            if (edge.pressed(st, SDL_SCANCODE_S) && m.screen == SCR_INFO) {
                m.screen = SCR_SETTINGS;
                m.setSel = 0;
            }
            if (edge.pressed(st, SDL_SCANCODE_R) && m.screen == SCR_CHATS) {
                m.calibrating = !m.calibrating;  // bascule le modal pour l'inspecter
                m.calibPage = 9;
                m.calibTotal = 30;
            }
        }

        uiRender(canvas, m);
        canvas.pushSprite(&display, 0, 0);
        display.display();
        SDL_Delay(16);
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);  // heures des captures stables
    tzset();
    std::string screensDir;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--screens") && i + 1 < argc) screensDir = argv[++i];
        else if (!std::strcmp(argv[i], "--mocks") && i + 1 < argc) return runMocks(argv[++i]);
        else if (!std::strcmp(argv[i], "--lang") && i + 1 < argc)
            gLang = gConfig.lang = std::strcmp(argv[i + 1], "fr") ? LANG_EN : LANG_FR, ++i;
        else {
            std::fprintf(stderr, "usage: %s [--screens <dir>] [--lang fr|en]\n", argv[0]);
            return 2;
        }
    }
    if (!screensDir.empty()) return runScreens(screensDir);
    return lgfx::Panel_sdl::main(simRun);
}

#endif  // SIM_BUILD
