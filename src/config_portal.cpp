#include "config_portal.h"
#include "i18n.h"
#include "app_config.h"

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>

static WebServer sServer(80);
static DNSServer sDns;
static bool sApMode = false;
static bool sReboot = false;

static const char* AP_SSID = "SiliconBubbles";
static const char* AP_PASS = "bluebubbles";

// Portail bilingue. Les libellés et explications vivent ici plutôt que dans
// i18n.h : ce sont des phrases longues, propres au web, que l'écran de
// l'appareil n'affichera jamais.
static inline const char* P(const char* en, const char* fr) {
    return gLang == LANG_FR ? fr : en;
}

// Page unique : formulaire prérempli. Le mot de passe serveur transite en
// clair sur le LAN (HTTP) : c'est un portail de configuration locale, à
// n'utiliser que sur un réseau de confiance (documenté dans docs/02).
static String htmlPage() {
    String h;
    h.reserve(7000);
    // Le portail parle le même langage que l'écran : nuit bleutée, un seul
    // bleu d'accent, tout le reste en gris (voir design/foundations/palette).
    h += F("<!DOCTYPE html><html lang='LG'><head><meta charset='utf-8'>"
           "<meta name='viewport' content='width=device-width,initial-scale=1'>"
           "<title>Silicon Bubbles</title><style>"
           ":root{--bg:#0A101B;--sur:#111A29;--sur2:#1B2740;--line:#25344F;"
           "--fg:#fff;--mut:#8FA3BC;--dim:#B9C7D8;--acc:#4DA3FF;--acc2:#2F7FEE;--ok:#31C48D}"
           "*{box-sizing:border-box}"
           "body{margin:0;padding:22px 16px 48px;background:var(--bg);color:var(--fg);"
           "font:15px/1.5 ui-sans-serif,-apple-system,'Segoe UI',Roboto,sans-serif}"
           ".w{max-width:480px;margin:0 auto}"
           "header{display:flex;align-items:center;gap:11px;margin-bottom:6px}"
           ".dot{width:9px;height:9px;border-radius:50%;background:var(--ok);flex:none;"
           "box-shadow:0 0 0 3px rgba(49,196,141,.16)}"
           "h1{font-size:19px;margin:0;letter-spacing:-.01em;font-weight:650}"
           "h1 b{color:var(--acc);font-weight:650}"
           ".sub{color:var(--mut);font-size:13px;margin:0 0 22px 20px}"
           "fieldset{border:1px solid var(--line);border-radius:12px;background:var(--sur);"
           "padding:4px 15px 16px;margin:0 0 15px}"
           "legend{font-size:11px;text-transform:uppercase;letter-spacing:.1em;"
           "color:var(--acc);padding:0 7px;font-weight:600}"
           "label{display:block;margin:13px 0 5px;font-size:13px;color:var(--dim)}"
           // Le reset d'apparence ne vaut QUE pour les champs texte : sur
           // Safari iOS, appearance:none sur une case à cocher la vide de sa
           // coche et casse le curseur de volume (constat PO sur téléphone).
           "input:not([type=checkbox]):not([type=range]),select{width:100%;"
           "padding:10px 11px;border-radius:9px;font-size:15px;"
           "border:1px solid var(--line);background:var(--sur2);color:var(--fg);"
           "-webkit-appearance:none;appearance:none}"
           "input:focus,select:focus{outline:0;border-color:var(--acc);"
           "box-shadow:0 0 0 3px rgba(77,163,255,.18)}"
           "input[type=range]{width:100%;padding:0;height:26px;background:transparent;border:0;"
           "accent-color:var(--acc2)}"
           "input[type=range]:focus{box-shadow:none}"
           "select{background-image:url(\"data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg'"
           " viewBox='0 0 12 8'%3E%3Cpath fill='%238FA3BC' d='M1 1l5 5 5-5'/%3E%3C/svg%3E\");"
           "background-repeat:no-repeat;background-position:right 12px center;background-size:11px}"
           ".hint{font-size:12px;color:var(--mut);margin-top:5px;line-height:1.45}"
           ".sw{display:flex;gap:11px;align-items:flex-start;margin-top:15px}"
           ".sw input{width:19px;height:19px;flex:none;margin:1px 0 0;accent-color:var(--acc2)}"
           ".sw label{margin:0;color:var(--fg);font-size:14px}"
           "button{margin-top:6px;width:100%;padding:14px;border:0;border-radius:11px;"
           "background:var(--acc2);color:#fff;font-size:15px;font-weight:650;cursor:pointer}"
           "button:active{background:var(--acc)}"
           ".meta{margin-top:18px;font-size:12px;color:var(--mut);text-align:center}"
           "@media(prefers-color-scheme:light){:root{--bg:#F5F7FB;--sur:#fff;--sur2:#fff;"
           "--line:#D9E0EC;--fg:#101828;--mut:#667085;--dim:#344054}}"
           "</style></head><body><div class='w'>"
           "<header><span class='dot'></span>"
           "<h1><b>Silicon</b> Bubbles</h1></header>"
           "<p class='sub'>SUBTITLE</p>"
           "<form method='POST' action='/save'>");
    h.replace("<html lang='LG'", gLang == LANG_FR ? "<html lang='fr'" : "<html lang='en'");
    h.replace("SUBTITLE", P("Device configuration", "Configuration de l'appareil"));
    // Révision de la config au moment du rendu : la sauvegarde la comparera.
    h += "<input type='hidden' name='rev' value='" + String(gConfig.rev) + "'>";

    // Les valeurs viennent de la config utilisateur : elles doivent être
    // échappées, sinon une apostrophe ou un chevron casse le formulaire.
    auto esc = [](const String& v) {
        String o;
        o.reserve(v.length() + 8);
        for (size_t i = 0; i < v.length(); i++) {
            char c = v[i];
            if (c == '&') o += "&amp;";
            else if (c == '<') o += "&lt;";
            else if (c == '>') o += "&gt;";
            else if (c == '"') o += "&quot;";
            else if (c == '\'') o += "&#39;";
            else o += c;
        }
        return o;
    };
    auto group = [&](const char* legend) {
        h += "<fieldset><legend>" + String(legend) + "</legend>";
    };
    auto endgroup = [&]() { h += "</fieldset>"; };
    auto field = [&](const char* label, const char* name, const String& value,
                     const char* type, const char* hint, const char* extra = "") {
        h += "<label for='" + String(name) + "'>" + String(label) + "</label><input id='" +
             name + "' name='" + name + "' type='" + type + "' " + extra +
             " value='" + esc(value) + "'>";
        if (hint) h += "<div class='hint'>" + String(hint) + "</div>";
    };

    group(P("Language", "Langue"));
    h += String("<label for='lang'>") + P("Interface language", "Langue de l'interface") +
         "</label><select id='lang' name='lang'>"
         "<option value='0'" + (gConfig.lang == LANG_EN ? " selected" : "") + ">English</option>"
         "<option value='1'" + (gConfig.lang == LANG_FR ? " selected" : "") + ">Français</option>"
         "</select><div class='hint'>" +
         P("Applies to this page and to the device screen.",
           "S'applique à cette page comme à l'écran de l'appareil.") +
         "</div>";
    endgroup();

    group(P("Network", "Réseau"));
    field(P("WiFi network name", "Nom du réseau WiFi"), "ssid", gConfig.wifiSsid, "text", nullptr,
          "autocapitalize='off' autocorrect='off' spellcheck='false'");
    field(P("WiFi password", "Mot de passe WiFi"), "wpass", gConfig.wifiPass, "password", nullptr);
    endgroup();

    group(P("BlueBubbles server", "Serveur BlueBubbles"));
    field(P("Address", "Adresse"), "url", gConfig.serverUrl, "url",
          P("For example https://my-server.example.com — no trailing slash.",
            "Par exemple https://mon-serveur.example.com — sans barre oblique finale."),
          "inputmode='url' autocapitalize='off' autocorrect='off' spellcheck='false' "
          "placeholder='https://…'");
    field(P("Server password", "Mot de passe du serveur"), "spass", gConfig.serverPass, "password",
          P("The one set in the server app, on the Mac.",
            "Celui défini dans l'application serveur, sur le Mac."));
    h += String("<label for='method'>") + P("Send method", "Méthode d'envoi") +
         "</label><select id='method' name='method'>";
    h += String("<option value='private-api'") +
         (gConfig.sendMethod == "private-api" ? " selected" : "") +
         (gLang == LANG_FR ? ">Private API — recommandé</option>"
                           : ">Private API — recommended</option>");
    h += String("<option value='apple-script'") +
         (gConfig.sendMethod == "apple-script" ? " selected" : "") +
         ">AppleScript</option></select><div class='hint'>" +
         P("AppleScript is only useful when the Private API is not installed on the server.",
           "AppleScript n'est utile que si la Private API n'est pas installée sur le serveur.") +
         "</div>";
    endgroup();

    group(P("Display and pace", "Affichage et rythme"));
    field(P("Refresh interval", "Intervalle de rafraîchissement"), "poll",
          String(gConfig.pollSec), "number",
          P("In seconds, 3 minimum. Shorter = more responsive, heavier on the battery.",
            "En secondes, 3 minimum. Plus court = plus réactif, plus gourmand en batterie."),
          "min='3' max='600' inputmode='numeric'");
    field(P("Messages loaded per conversation", "Messages chargés par conversation"), "hist",
          String(gConfig.histDepth), "number",
          P("From 4 to 15. Beyond that, the reply exceeds what the device memory can take "
            "in one contiguous block.",
            "De 4 à 15. Au-delà, la réponse dépasse ce que la mémoire de l'appareil "
            "peut recevoir d'un seul tenant."),
          "min='4' max='15' inputmode='numeric'");
    field(P("Time zone", "Fuseau horaire"), "tz", gConfig.tz, "text",
          P("POSIX format. Paris: CET-1CEST,M3.5.0,M10.5.0/3",
            "Format POSIX. Paris : CET-1CEST,M3.5.0,M10.5.0/3"),
          "autocapitalize='off' autocorrect='off' spellcheck='false'");
    endgroup();

    group(P("Sounds", "Sons"));
    field(P("Volume", "Volume"), "svol", String(gConfig.sndVolume), "range",
          P("0 = silent. The Cardputer ADV has a real speaker: the signals are struck-bar "
            "timbres, not beeps.",
            "0 = silencieux. Le Cardputer ADV a un vrai haut-parleur : les "
            "signaux sont des timbres de bois, pas des bips."),
          "min='0' max='100' step='5'");
    auto toggle = [&](const char* id, const char* label, bool on, const char* hint) {
        h += String("<div class='sw'><input type='checkbox' id='") + id + "' name='" + id +
             "' value='1'" + (on ? " checked" : "") + "><label for='" + id + "'>" +
             label + "</label></div>";
        if (hint) h += "<div class='hint' style='margin-left:30px'>" + String(hint) + "</div>";
    };
    toggle("skey", P("Keyboard keys", "Touches du clavier"), gConfig.sndKeys,
           P("A very short knock, pitched slightly differently on each keystroke.",
             "Un toc très bref, de hauteur variable à chaque frappe."));
    toggle("ssnd", P("Message sent", "Message envoyé"), gConfig.sndSend,
           P("Two rising notes.", "Deux notes montantes."));
    toggle("srcv", P("Message received", "Message reçu"), gConfig.sndRecv,
           P("Two notes settling down, in the open conversation.",
             "Deux notes qui se posent, dans la conversation ouverte."));
    toggle("sntf", P("Notifications", "Notifications"), gConfig.sndNotif,
           P("Message arriving in another conversation, and send failures.",
             "Message arrivé dans une autre conversation, et échecs d'envoi."));
    endgroup();

    group(P("Security", "Sécurité"));
    h += String(F("<div class='sw'><input type='checkbox' id='tlsv' name='tlsv' value='1'")) +
         (gConfig.tlsVerify ? " checked" : "") +
         "><label for='tlsv'>" + P("Verify the TLS certificate", "Vérifier le certificat TLS") +
         "</label></div><div class='hint'>" +
         P("Embedded authorities: GTS R1 and R4 (Cloudflare), ISRG X1 (Let's Encrypt). "
           "Uncheck only for a self-signed certificate, on a trusted network.",
           "Autorités embarquées : GTS R1 et R4 (Cloudflare), ISRG X1 (Let's Encrypt). "
           "Décochez uniquement pour un certificat auto-signé, sur un réseau de confiance.") +
         "</div>";
    endgroup();

    h += String("<button type='submit'>") +
         P("Save and restart", "Enregistrer et redémarrer") + "</button></form><p class='meta'>" +
         P("The device restarts to apply the changes.",
           "L'appareil redémarre pour appliquer les changements.") +
         "</p></div></body></html>";
    return h;
}

static void handleRoot() { sServer.send(200, "text/html; charset=utf-8", htmlPage()); }

static void handleSave() {
    // On n'écrit JAMAIS dans gConfig ici : la tâche réseau (cœur 0) le lit en
    // permanence et une affectation de String depuis le cœur 1 la ferait
    // planter. On sauvegarde une copie en NVS, et le redémarrage la relit.
    // Garde anti-conflit : le formulaire a été rendu avec une certaine
    // révision de la configuration. Si elle a changé depuis (réglage modifié
    // sur l'appareil pendant que la page était ouverte), on refuse plutôt que
    // d'écraser silencieusement — le formulaire renverrait des valeurs périmées.
    if (sServer.hasArg("rev") && (uint32_t)strtoul(sServer.arg("rev").c_str(), nullptr, 10) !=
                                     gConfig.rev) {
        String msg = String("<!DOCTYPE html><html lang='") + (gLang == LANG_FR ? "fr" : "en") +
            "'><meta charset='utf-8'>"
            "<meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<body style=\"margin:0;min-height:100vh;display:flex;align-items:center;"
            "justify-content:center;background:#0A101B;color:#fff;text-align:center;"
            "font:15px/1.5 ui-sans-serif,-apple-system,'Segoe UI',Roboto,sans-serif\">"
            "<div style='max-width:340px;padding:20px'>"
            "<div style='width:11px;height:11px;border-radius:50%;background:#F0A54A;"
            "margin:0 auto 18px;box-shadow:0 0 0 5px rgba(240,165,74,.16)'></div>"
            "<h2 style='margin:0 0 8px;font-size:19px;font-weight:650'>" +
            P("Settings changed on the device", "Réglages modifiés sur l'appareil") +
            "</h2><p style='margin:0 0 18px;color:#8FA3BC;font-size:14px'>" +
            P("Nothing was saved: this page was showing older values. Reload it and try again.",
              "Rien n'a été enregistré : cette page affichait des valeurs plus anciennes. "
              "Rechargez-la puis recommencez.") +
            "</p><a href='/' style='color:#4DA3FF'>" + P("Reload", "Recharger") + "</a></div>";
        sServer.send(409, "text/html; charset=utf-8", msg);
        return;
    }

    AppConfig next = gConfig;
    next.wifiSsid   = sServer.arg("ssid");
    next.wifiPass   = sServer.arg("wpass");
    next.serverUrl  = sServer.arg("url");
    next.serverPass = sServer.arg("spass");
    next.tz         = sServer.arg("tz");
    next.sendMethod = sServer.arg("method");
    next.pollSec    = (uint16_t)sServer.arg("poll").toInt();
    next.histDepth  = (uint8_t)sServer.arg("hist").toInt();
    next.tlsVerify  = sServer.hasArg("tlsv");
    next.lang       = (uint8_t)sServer.arg("lang").toInt();
    if (next.lang >= LANG_COUNT) next.lang = LANG_EN;
    next.sndVolume  = (uint8_t)sServer.arg("svol").toInt();
    next.sndKeys    = sServer.hasArg("skey");
    next.sndSend    = sServer.hasArg("ssnd");
    next.sndRecv    = sServer.hasArg("srcv");
    next.sndNotif   = sServer.hasArg("sntf");
    if (next.sndVolume > 100) next.sndVolume = 100;
    while (next.serverUrl.endsWith("/"))
        next.serverUrl.remove(next.serverUrl.length() - 1);
    if (next.pollSec < 3) next.pollSec = 3;
    if (next.histDepth < 4) next.histDepth = 4;
    if (next.histDepth > 15) next.histDepth = 15;
    next.save();

    sServer.send(200, "text/html; charset=utf-8",
                 F("<!DOCTYPE html><html lang='fr'><meta charset='utf-8'>"
                   "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                   "<title>Configuration enregistrée</title>"
                   "<body style=\"margin:0;min-height:100vh;display:flex;align-items:center;"
                   "justify-content:center;background:#0A101B;color:#fff;text-align:center;"
                   "font:15px/1.5 ui-sans-serif,-apple-system,'Segoe UI',Roboto,sans-serif\">"
                   "<div><div style='width:11px;height:11px;border-radius:50%;background:#31C48D;"
                   "margin:0 auto 18px;box-shadow:0 0 0 5px rgba(49,196,141,.16)'></div>"
                   "<h2 style='margin:0 0 8px;font-size:19px;font-weight:650'>"
                   "Configuration enregistrée</h2>"
                   "<p style='margin:0;color:#8FA3BC;font-size:14px'>"
                   "Le Cardputer redémarre…</p></div>"));
    sReboot = true;
}

static void handleStatus() {
    String j = "{\"app\":\"silicon-bubbles\",\"wifi\":\"" +
               WiFi.SSID() + "\",\"rssi\":" + WiFi.RSSI() +
               ",\"ip\":\"" + WiFi.localIP().toString() +
               "\",\"server\":\"" + gConfig.serverUrl + "\"}";
    sServer.send(200, "application/json", j);
}


static void setupRoutes() {
    sServer.on("/", HTTP_GET, handleRoot);
    sServer.on("/save", HTTP_POST, handleSave);
    sServer.on("/status", HTTP_GET, handleStatus);
    // Diagnostic : /debug/conv?run=1&i=0 déclenche, /debug/conv lit le résultat.
    sServer.on("/debug/conv", HTTP_GET, []() {
        if (sServer.hasArg("calib")) {
            sServer.send(200, "application/json", appDebugCalibRun());
        } else if (sServer.hasArg("run")) {
            int i = sServer.hasArg("i") ? sServer.arg("i").toInt() : 0;
            sServer.send(200, "application/json", appDebugConvRun(i));
        } else {
            sServer.send(200, "application/json", appDebugConvResult());
        }
    });
    // Portail captif : toute URL inconnue ramène au formulaire.
    sServer.onNotFound([]() {
        sServer.sendHeader("Location", "http://" +
                           (sApMode ? WiFi.softAPIP() : WiFi.localIP()).toString() + "/");
        sServer.send(302, "text/plain", "");
    });
    sServer.begin();
}

namespace ConfigPortal {

void startAP() {
    sApMode = true;
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);
    sDns.start(53, "*", WiFi.softAPIP());
    setupRoutes();
}

void startSTA() {
    sApMode = false;
    if (MDNS.begin("cardputer")) MDNS.addService("http", "tcp", 80);
    setupRoutes();
}

void handle() {
    if (sApMode) sDns.processNextRequest();
    sServer.handleClient();
}

bool rebootRequested() { return sReboot; }
String apSsid() { return AP_SSID; }
String apPass() { return AP_PASS; }

}  // namespace ConfigPortal
