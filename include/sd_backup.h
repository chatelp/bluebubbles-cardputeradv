#pragma once
// Sauvegarde / restauration de la configuration et des données vers la
// carte microSD (dossier /SiliconBubbles). Voir src/sd_backup.cpp.
#include <Arduino.h>

bool sdBackupExists();               // une sauvegarde est-elle présente ?
bool sdBackupSave(String& err);      // écrit config + chats + alias + marqueur
bool sdBackupRestore(String& err);   // relit tout vers la NVS (redémarrer ensuite)
