#pragma once
// Codes d'erreur UTILISATEUR — la table publique vit dans le README
// (section « Codes d'erreur ») : la tenir synchrone avec celle-ci.
//
// Règles (décision PO 2026-08-16) :
//  - un code par CAUSE actionnable, jamais par symptôme technique ;
//  - message court (bandeau de 13 px ≈ 38 caractères), sans jargon —
//    « JSON », « heap » ou « chunked » restent sur la console série ;
//  - les cas évidents (WiFi perdu) gardent un message nu, sans code.
#include <Arduino.h>

#include "i18n.h"  // gLang

enum BbErr : uint8_t {
    BB_E11_UNREACHABLE,     // connexion impossible (DNS, TCP, TLS)
    BB_E13_TIMEOUT,         // connecté mais pas de réponse à temps
    BB_E20_AUTH,            // HTTP 401/403 : mot de passe serveur refusé
    BB_E21_SERVER,          // HTTP 5xx : le serveur est en difficulté
    BB_E22_NOT_FOUND,       // HTTP 404 : pas l'API BlueBubbles attendue
    BB_E23_HTTP,            // autre code HTTP inattendu (proxy, portail captif…)
    BB_E30_RESPONSE_HUGE,   // réponse aberrante (> LEN_SANITY)
    BB_E31_RESPONSE_BAD,    // réponse interrompue ou illisible (parse)
    BB_E32_RESPONSE_MEMORY, // réponse valide mais trop grosse pour la RAM
    BB_E40_DEVICE_MEMORY,   // tas de l'appareil momentanément saturé
    BB_E50_URL,             // adresse du serveur invalide
    BB_ERR_COUNT
};

struct BbErrDef {
    const char* code;
    const char* txt[LANG_COUNT];  // EN, FR
};

static const BbErrDef kBbErrs[BB_ERR_COUNT] = {
    {"E11", {"server unreachable", "serveur injoignable"}},
    {"E13", {"server not answering", "serveur sans réponse"}},
    {"E20", {"server password refused", "mot de passe serveur refusé"}},
    {"E21", {"error on the server side", "erreur côté serveur"}},
    {"E22", {"BlueBubbles API not found", "API BlueBubbles introuvable"}},
    {"E23", {"unexpected HTTP reply", "réponse HTTP inattendue"}},
    {"E30", {"absurd response size", "réponse aberrante"}},
    {"E31", {"response cut short", "réponse interrompue"}},
    {"E32", {"response exceeds memory", "réponse trop grosse (mémoire)"}},
    {"E40", {"device memory is full", "mémoire de l'appareil saturée"}},
    {"E50", {"invalid server address", "adresse du serveur invalide"}},
};

// « E21 erreur côté serveur (502) » — le détail numérique est optionnel.
inline String bbErr(BbErr e, int detail = 0) {
    String s = kBbErrs[e].code;
    s += " ";
    s += kBbErrs[e].txt[gLang < LANG_COUNT ? gLang : LANG_EN];
    if (detail) {
        s += " (";
        s += detail;
        s += ")";
    }
    return s;
}

// Un message commence-t-il par un code (« E » + deux chiffres) ? Sert au
// bandeau d'état pour colorer en rouge sans liste de mots-clés fragile.
inline bool bbErrIsCode(const char* s) {
    // le code peut suivre un préfixe (« Send failed: E21 … »)
    for (const char* p = s; *p; p++)
        if (p[0] == 'E' && p[1] >= '0' && p[1] <= '9' && p[2] >= '0' && p[2] <= '9')
            return true;
    return false;
}
