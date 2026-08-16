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
