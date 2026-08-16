// Le modèle de vue : tout ce que le rendu lit, et rien d'autre. Le firmware
// le remplit sous verrou (DataLock) depuis son état et son matériel ; le
// simulateur le remplit avec des données de démonstration. Le rendu ne touche
// jamais au réseau, à la NVS ni au matériel.
#pragma once
#include <map>
#include <vector>

#include "bb_client.h"  // BBChat, BBMsg (et String via Arduino ou le shim hôte)

enum Screen {
    SCR_SPLASH, SCR_SETUP, SCR_CHATS, SCR_MESSAGES, SCR_COMPOSE, SCR_INFO,
    SCR_SETTINGS, SCR_WIFI_SCAN, SCR_TEXT_INPUT, SCR_QR, SCR_ABOUT
};

// Champs de l'écran Réglages — l'ordre est l'ordre d'affichage.
enum SetField : uint8_t {
    SET_LANG, SET_VOL, SET_KEYS, SET_SEND, SET_RECV, SET_NOTIF,
    SET_POLL, SET_HIST,
    // Lignes d'ACTION (OK les ouvre) — modification réseau => redémarrage.
    SET_WIFI, SET_SERVER, SET_SPASS, SET_QR, SET_BACKUP, SET_RESTORE,
    SET_COUNT
};

// Un réseau vu par le scan WiFi.
struct UiNet {
    String ssid;
    int rssi = 0;
    bool secure = true;
};

struct UiModel {
    Screen screen = SCR_CHATS;

    // Liste des conversations
    std::vector<BBChat> chats;
    std::map<String, int64_t> seen;  // clé de fusion -> date du dernier msg vu
    int chatSel = 0;
    int chatTop = 0;

    // Conversation ouverte
    std::vector<BBMsg> msgs;
    int msgScroll = 0;   // index d'arrêt (0 = bas) — borné par le rendu
    int msgStops = 1;    // nombre d'arrêts, recalculé à chaque rendu
    String curChatTitle;
    String curChatKey;
    String compose;

    // Statuts (séparés : liste / conversation ouverte)
    String status;
    String statusView;

    // Calibration (écrits par la tâche réseau sans verrou complet)
    volatile bool calibrating = false;
    volatile int calibPage = 0;
    volatile int calibTotal = 0;
    bool synced = false;

    // Réglages
    int setSel = 0;

    // Scan WiFi (SCR_WIFI_SCAN)
    std::vector<UiNet> nets;
    bool scanning = false;
    int scanSel = 0;

    // Éditeur de texte générique (SCR_TEXT_INPUT)
    String editLabel;
    String editValue;
    bool editMask = false;

    // Écran QR (SCR_QR)
    String qrPayload, qrTitle, qrSub;

    // Une sauvegarde SD est disponible (proposée au premier démarrage)
    bool sdBackup = false;

    // Instantané matériel/réseau, rempli juste avant le rendu
    int battery = 100;
    bool wifiOk = true;
    int rssi = -50;
    String ssid, ip;
    String apSsid, apPass;  // écran de premier démarrage
    int64_t marker = 0;
    const char* version = "";
};
