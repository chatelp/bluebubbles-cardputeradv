#pragma once
#include <Arduino.h>

// Persistent app configuration, backed by NVS (Preferences).
struct AppConfig {
    String wifiSsid;
    String wifiPass;
    String serverUrl;    // e.g. https://my-server.example.com (no trailing slash)
    String serverPass;   // BlueBubbles server password
    String sendMethod;   // "private-api" or "apple-script"
    String tz;           // POSIX TZ string
    uint16_t pollSec;
    uint8_t histDepth;   // messages chargés par conversation (4-15)
    bool tlsVerify;
    uint8_t lang;        // 0 = anglais (défaut), 1 = français — voir i18n.h
    // Révision : incrémentée à chaque enregistrement, d'où qu'il vienne. Le
    // portail l'embarque dans son formulaire et refuse d'écrire par-dessus une
    // modification faite sur l'appareil entre-temps (pas de clobber silencieux).
    uint32_t rev;

    // Sons — chaque famille se coupe indépendamment (voir sound.h)
    uint8_t sndVolume;   // 0-100, 0 = silencieux
    bool sndKeys;
    bool sndSend;
    bool sndRecv;
    bool sndNotif;

    void load();
    void save();
    bool hasWifi() const { return wifiSsid.length() > 0; }
    bool hasServer() const { return serverUrl.length() > 0; }
};

extern AppConfig gConfig;
