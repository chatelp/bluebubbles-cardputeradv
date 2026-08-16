#include "app_config.h"
#include "i18n.h"
#include <Preferences.h>

AppConfig gConfig;
uint8_t gLang = LANG_EN;  // miroir de gConfig.lang (voir i18n.h)

static const char* NVS_NS = "bbcfg";

void AppConfig::load() {
    Preferences p;
    p.begin(NVS_NS, true);
    wifiSsid   = p.getString("ssid", "");
    wifiPass   = p.getString("wpass", "");
    serverUrl  = p.getString("url", "");
    serverPass = p.getString("spass", "");
    sendMethod = p.getString("method", "private-api");
    tz         = p.getString("tz", "CET-1CEST,M3.5.0,M10.5.0/3");
    pollSec    = p.getUShort("poll", 10);
    histDepth  = p.getUChar("hist", 10);
    tlsVerify  = p.getBool("tlsv", true);
    sndVolume  = p.getUChar("svol", 60);
    sndKeys    = p.getBool("skey", true);
    sndSend    = p.getBool("ssnd", true);
    sndRecv    = p.getBool("srcv", true);
    sndNotif   = p.getBool("sntf", true);
    lang       = p.getUChar("lang", LANG_EN);
    rev        = p.getULong("rev", 0);
    p.end();

    while (serverUrl.endsWith("/")) serverUrl.remove(serverUrl.length() - 1);
    if (pollSec < 3) pollSec = 3;
    if (histDepth < 4) histDepth = 4;
    if (histDepth > 15) histDepth = 15;
    if (sndVolume > 100) sndVolume = 100;
    if (lang >= LANG_COUNT) lang = LANG_EN;
    gLang = lang;
}

void AppConfig::save() {
    rev++;  // toute écriture invalide les formulaires déjà rendus
    Preferences p;
    p.begin(NVS_NS, false);
    p.putString("ssid", wifiSsid);
    p.putString("wpass", wifiPass);
    p.putString("url", serverUrl);
    p.putString("spass", serverPass);
    p.putString("method", sendMethod);
    p.putString("tz", tz);
    p.putUShort("poll", pollSec);
    p.putUChar("hist", histDepth);
    p.putBool("tlsv", tlsVerify);
    p.putUChar("svol", sndVolume);
    p.putBool("skey", sndKeys);
    p.putBool("ssnd", sndSend);
    p.putBool("srcv", sndRecv);
    p.putBool("sntf", sndNotif);
    p.putUChar("lang", lang);
    p.putULong("rev", rev);
    p.end();
}
