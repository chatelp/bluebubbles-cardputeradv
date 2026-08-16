// Sauvegarde / restauration vers la carte microSD du Cardputer ADV
// (SPI : SCK 40, MISO 39, MOSI 14, CS 12 — pins M5Stack documentées).
//
// Contenu : /SiliconBubbles/config.json (toute la configuration, mots de
// passe INCLUS et en clair — c'est la carte de l'utilisateur, le README le
// dit), chats.bin (liste épinglée + marqueur, blob NVS tel quel) et
// names.bin (alias de contacts). La carte n'est montée que le temps de
// l'opération : le bus reste libre le reste du temps.
#include "sd_backup.h"

#include <Preferences.h>
#include <SD.h>
#include <SPI.h>

#include <ArduinoJson.h>

#include "app_config.h"
#include "bb_errors.h"
#include "i18n.h"

static const int SD_SCK = 40, SD_MISO = 39, SD_MOSI = 14, SD_CS = 12;
static const char* DIR = "/SiliconBubbles";

static bool sdMount() {
    SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    return SD.begin(SD_CS, SPI, 20000000);
}

static void sdUnmount() { SD.end(); }

// Copie un blob NVS vers un fichier (0 octet si absent : fichier omis).
static bool blobToFile(const char* ns, const char* key, const char* path) {
    Preferences p;
    if (!p.begin(ns, true)) return true;  // namespace vierge : rien à copier
    size_t len = p.getBytesLength(key);
    if (!len) { p.end(); return true; }
    std::vector<uint8_t> b(len);
    p.getBytes(key, b.data(), len);
    p.end();
    File f = SD.open(path, FILE_WRITE);
    if (!f) return false;
    bool ok = f.write(b.data(), len) == len;
    f.close();
    return ok;
}

static void fileToBlob(const char* path, const char* ns, const char* key) {
    File f = SD.open(path, FILE_READ);
    if (!f) return;
    size_t len = f.size();
    if (!len || len > 4096) { f.close(); return; }
    std::vector<uint8_t> b(len);
    f.read(b.data(), len);
    f.close();
    Preferences p;
    p.begin(ns, false);
    p.putBytes(key, b.data(), len);
    if (!strcmp(ns, "bbstore")) p.putBool("ok", true);
    p.end();
}

bool sdBackupExists() {
    if (!sdMount()) return false;
    bool ok = SD.exists(String(DIR) + "/config.json");
    sdUnmount();
    return ok;
}

bool sdBackupSave(String& err) {
    if (!sdMount()) { err = T(S_NO_SD); return false; }
    SD.mkdir(DIR);
    JsonDocument d;
    d["app"] = "silicon-bubbles";
    d["ver"] = 1;
    d["ssid"] = gConfig.wifiSsid;   d["wpass"] = gConfig.wifiPass;
    d["url"] = gConfig.serverUrl;   d["spass"] = gConfig.serverPass;
    d["method"] = gConfig.sendMethod; d["tz"] = gConfig.tz;
    d["poll"] = gConfig.pollSec;    d["hist"] = gConfig.histDepth;
    d["tlsv"] = gConfig.tlsVerify;  d["lang"] = gConfig.lang;
    d["svol"] = gConfig.sndVolume;  d["skey"] = gConfig.sndKeys;
    d["ssnd"] = gConfig.sndSend;    d["srcv"] = gConfig.sndRecv;
    d["sntf"] = gConfig.sndNotif;
    File f = SD.open(String(DIR) + "/config.json", FILE_WRITE);
    if (!f) { sdUnmount(); err = T(S_NO_SD); return false; }
    serializeJsonPretty(d, f);
    f.close();
    bool ok = blobToFile("bbstore", "chats", (String(DIR) + "/chats.bin").c_str()) &&
              blobToFile("bbnames", "v1", (String(DIR) + "/names.bin").c_str());
    // le marqueur voyage dans config ? non : il vit dans chats.bin côté NVS —
    // il est distinct : on le copie à part, en JSON simple.
    Preferences p;
    if (p.begin("bbstore", true)) {
        int64_t marker = p.getLong64("marker", 0);
        p.end();
        File m = SD.open(String(DIR) + "/marker.txt", FILE_WRITE);
        if (m) {
            m.print((long long)marker);
            m.close();
        }
    }
    sdUnmount();
    if (!ok) { err = T(S_NO_SD); return false; }
    return true;
}

bool sdBackupRestore(String& err) {
    if (!sdMount()) { err = T(S_NO_SD); return false; }
    File f = SD.open(String(DIR) + "/config.json", FILE_READ);
    if (!f) { sdUnmount(); err = T(S_SD_NONE); return false; }
    JsonDocument d;
    DeserializationError e = deserializeJson(d, f);
    f.close();
    if (e) { sdUnmount(); err = bbErr(BB_E31_RESPONSE_BAD); return false; }

    AppConfig next = gConfig;
    next.wifiSsid  = (const char*)(d["ssid"] | "");
    next.wifiPass  = (const char*)(d["wpass"] | "");
    next.serverUrl = (const char*)(d["url"] | "");
    next.serverPass = (const char*)(d["spass"] | "");
    next.sendMethod = (const char*)(d["method"] | "private-api");
    next.tz        = (const char*)(d["tz"] | "CET-1CEST,M3.5.0,M10.5.0/3");
    next.pollSec   = d["poll"] | 10;
    next.histDepth = d["hist"] | 10;
    next.tlsVerify = d["tlsv"] | true;
    next.lang      = d["lang"] | 0;
    next.sndVolume = d["svol"] | 60;
    next.sndKeys   = d["skey"] | true;
    next.sndSend   = d["ssnd"] | true;
    next.sndRecv   = d["srcv"] | true;
    next.sndNotif  = d["sntf"] | true;
    next.save();

    fileToBlob((String(DIR) + "/chats.bin").c_str(), "bbstore", "chats");
    fileToBlob((String(DIR) + "/names.bin").c_str(), "bbnames", "v1");
    File m = SD.open(String(DIR) + "/marker.txt", FILE_READ);
    if (m) {
        String v = m.readString();
        m.close();
        Preferences p;
        p.begin("bbstore", false);
        p.putLong64("marker", strtoll(v.c_str(), nullptr, 10));
        p.end();
    }
    sdUnmount();
    return true;
}
