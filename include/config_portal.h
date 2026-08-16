#pragma once
#include <Arduino.h>

// Mini serveur web de configuration, accessible :
//  - en mode AP (premier démarrage / WiFi introuvable) sur http://192.168.4.1
//  - en mode STA (usage normal) sur l'IP du Cardputer et http://cardputer.local
namespace ConfigPortal {
    void startAP();          // ouvre l'AP "SiliconBubbles" + portail captif
    void startSTA();         // sert la page de config sur l'IP courante + mDNS
    void handle();           // à appeler dans loop()
    bool rebootRequested();  // vrai après sauvegarde : l'appli doit redémarrer
    String apSsid();
    String apPass();
}

// Implémentées dans main.cpp — diagnostic LAN : déclenche un chargement de
// conversation par la tâche réseau et en publie le résultat (ok, nombre,
// durée, erreur). Aucune donnée de message n'est exposée.
String appDebugConvRun(int index);
String appDebugConvResult();
String appDebugCalibRun();
