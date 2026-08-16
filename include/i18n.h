#pragma once
// Interface bilingue anglais / français — anglais par défaut (décision PO du
// 2026-08-16, elle remplace la règle « tout en français » de CLAUDE.md).
//
// Une table plate indexée par identifiant : pas d'allocation, pas de String,
// tout vit en flash. La langue est un champ de configuration (gConfig.lang),
// recopié dans gLang pour que le rendu n'ait pas à traverser la config à
// chaque chaîne.
//
// Règle d'écriture : les chaînes de la barre d'aide décrivent des touches
// physiques — garder la même longueur d'une langue à l'autre autant que
// possible (240 px = 40 caractères à 6 px).
#include <stdint.h>

enum BbLang : uint8_t { LANG_EN = 0, LANG_FR = 1, LANG_COUNT = 2 };

enum StrId : uint8_t {
    S_CONVERSATIONS, S_SETUP_REQUIRED, S_PORTAL_OPEN, S_CALIBRATION, S_CALIB_SUB,
    S_PAGE, S_PLEASE_WAIT, S_HINT_CHATS, S_INFO, S_HINT_INFO, S_NO_CHATS,
    S_CATCHUP_RECALIB, S_CATCHUP, S_CHAT_DELETED, S_SEND_UNCONFIRMED,
    S_SEND_FAILED, S_BUSY_RETRY, S_SENDING, S_LOW_MEMORY, S_CONNECTING_TO,
    S_WIFI_NOT_FOUND, S_SERVER_NOT_CONFIGURED, S_UPDATING, S_CALIBRATING,
    S_REBOOTING, S_EMPTY_CHAT, S_LOADING, S_HINT_MSGS, S_HINT_COMPOSE,
    S_SERVER_OK, S_ERROR_PREFIX, S_TESTING_SERVER, S_NO_CHATS_YET,
    // Écran Infos
    S_VERSION, S_IP, S_CONFIG, S_SERVER, S_SYNC, S_HISTORY, S_TLS,
    S_TLS_ON, S_TLS_OFF, S_MESSAGES_UNIT,
    // Écran Réglages
    S_SETTINGS, S_LANGUAGE, S_SOUNDS, S_SND_KEYS, S_SND_SENT, S_SND_RECV,
    S_SND_NOTIF, S_VOLUME, S_POLL, S_ON, S_OFF, S_HINT_SETTINGS, S_SAVED,
    S_PORTAL_ONLY,
    S_NO_WIFI, S_ME_PREFIX, S_ME_CAPS, S_NEW_COUNT, S_SETUP_WIFI, S_SETUP_PASS, S_SETUP_FILL, S_SETUP_REBOOT,
    // Réglages étendus, scan WiFi, éditeur, QR, SD, alias
    S_SET_WIFI, S_SET_SERVER, S_SET_SPASS, S_SET_QR, S_SET_BACKUP, S_SET_RESTORE,
    S_SCAN_TITLE, S_SCANNING, S_NO_NETWORKS, S_OPEN_NET, S_HINT_SCAN,
    S_HINT_INPUT, S_HINT_QR, S_QR_JOIN_T, S_QR_JOIN_S, S_QR_PORTAL_T, S_QR_PORTAL_S,
    S_PASS_FOR, S_CONTACT_NAME, S_NO_SD, S_SD_SAVED, S_SD_NONE, S_SETUP_RESTORE,
    // Contenu de message (bb_client)
    S_ATTACHMENT, S_NO_TEXT, S_RETRACTED, S_EDITED,
    S_COUNT
};

// Anglais d'abord : c'est la langue par défaut.
static const char* const kStrings[S_COUNT][LANG_COUNT] = {
    {"Conversations", "Conversations"},
    {"Setup required", "Configuration requise"},
    {"portal open — OK: QR", "portail ouvert — OK : QR"},
    {"Calibration", "Calibration"},
    {"analysing recent conversations", "analyse des conversations recentes"},
    {"page", "page"},
    {"please wait…", "patientez…"},
    {";. move OK open n name r sync ` info", ";. aller OK ouvrir n nom r sync ` info"},
    {"Info", "Infos"},
    {"p test  s settings  ` back", "p tester  s reglages  ` retour"},
    {"No conversation found", "Aucune conversation trouvee"},
    {"Far behind: recalibrating…", "Retard important : recalibration…"},
    {"Catching up…", "Rattrapage…"},
    {"Conversation deleted on the Mac", "Conversation supprimée sur le Mac"},
    {"Sent but unconfirmed — check it", "Envoi non confirmé — vérifiez"},
    {"Send failed: ", "Echec envoi : "},
    {"Busy: try again", "Occupe : reessayez"},
    {"Sending…", "Envoi…"},
    {"Error: not enough memory", "Erreur : memoire insuffisante"},
    {"Connecting to ", "Connexion à "},
    {"WiFi not found", "WiFi introuvable"},
    {"Server not set: http://", "Serveur non configure : http://"},
    {"Updating…", "MAJ…"},
    {"Calibration…", "Calibration…"},
    {"Restarting…", "Redémarrage…"},
    {"Empty conversation", "Conversation vide"},
    {"Loading…", "Chargement…"},
    {";/. scroll  OK write  ` back", ";/. defiler  OK ecrire  ` retour"},
    {"OK send  ` cancel", "OK envoyer  ` annuler"},
    {"Server OK", "Serveur OK"},
    {"Error: ", "Erreur : "},
    {"Testing server…", "Test du serveur…"},
    {"No conversation", "Aucune conversation"},
    {"Version", "Version"},
    {"IP", "IP"},
    {"Config", "Config"},
    {"Server", "Serveur"},
    {"Sync", "Synchro"},
    {"History", "Historique"},
    {"TLS", "TLS"},
    {"verified", "verifie"},
    {"not verified", "non verifie"},
    {"messages", "messages"},
    {"Settings", "Reglages"},
    {"Language", "Langue"},
    {"Sounds", "Sons"},
    {"Keys", "Touches"},
    {"Sent", "Envoi"},
    {"Received", "Reception"},
    {"Notifications", "Notifications"},
    {"Volume", "Volume"},
    {"Refresh", "Rythme"},
    {"on", "oui"},
    {"off", "non"},
    {";. field ,/ value OK open ` save", ";. champ ,/ valeur OK ouvrir ` enr."},
    {"Saved", "Enregistre"},
    {"Network changes restart the device", "Reseau/serveur : redemarrage a l'enreg."},
    {"No WiFi", "WiFi perdu"},
    {"me: ", "moi : "},
    {"ME", "MOI"},
    {"NEW: ", "NOUVEAU: "},
    {"WiFi network  ", "Reseau WiFi  "},
    {"password  ", "mot de passe  "},
    {"Enter WiFi and server", "Renseignez WiFi et serveur"},
    {"the device then restarts", "l'appareil redemarre ensuite"},
    {"WiFi network", "Reseau WiFi"},
    {"Server address", "Adresse serveur"},
    {"Server password", "Mot de passe serveur"},
    {"Portal on phone", "Portail sur telephone"},
    {"Backup to SD", "Sauvegarde sur SD"},
    {"Restore from SD", "Restaurer depuis SD"},
    {"WiFi networks", "Reseaux WiFi"},
    {"scanning…", "recherche…"},
    {"no network found", "aucun reseau trouve"},
    {"open", "ouvert"},
    {";. move  OK choose  r scan  ` back", ";. aller  OK choisir  r scan  ` retour"},
    {"OK confirm  ` cancel", "OK valider  ` annuler"},
    {"` back", "` retour"},
    {"Join the setup WiFi", "Rejoindre le WiFi d'installation"},
    {"Scan with the phone camera; the setup page then opens by itself.",
     "Scannez avec l'appareil photo : la page de reglage s'ouvre ensuite toute seule."},
    {"Settings on the phone", "Reglages sur le telephone"},
    {"Scan to open the portal at the device's current address.",
     "Scannez pour ouvrir le portail a l'adresse actuelle de l'appareil."},
    {"Password for ", "Mot de passe pour "},
    {"Contact name", "Nom du contact"},
    {"No SD card", "Carte SD introuvable"},
    {"Saved to SD", "Sauvegarde sur SD faite"},
    {"No backup on SD", "Aucune sauvegarde sur la SD"},
    {"b restore SD backup", "b restaurer la sauvegarde SD"},
    {"[attachment]", "[pièce jointe]"},
    {"[no text]", "[message sans texte]"},
    {"[message unsent]", "[message retiré]"},
    {" (edited)", " (modifié)"},
};

// Langue courante, tenue à jour par AppConfig::load() et l'écran Réglages.
extern uint8_t gLang;

inline const char* T(StrId id) {
    return kStrings[id][gLang < LANG_COUNT ? gLang : LANG_EN];
}
