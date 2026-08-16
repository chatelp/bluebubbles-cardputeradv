#pragma once
// Tapbacks (réactions iMessage) — logique pure (sans Arduino), testée en
// natif. Une réaction est une ligne de message portant associatedMessageGuid
// (cible, préfixée « p:N/ » : l'index de partie) et associatedMessageType
// (love/like/dislike/laugh/emphasize/question, préfixé « - » pour un
// retrait). Les stickers portent aussi associatedMessageGuid mais PAS un de
// ces types : ils restent des messages affichés.
#include <string.h>
#include <stdint.h>

enum { BB_TAP_TYPES = 6 };

// Indice 0..5 pour un type de réaction (sans son éventuel « - »), -1 sinon.
inline int bbTapTypeIndex(const char* t) {
    static const char* kTypes[BB_TAP_TYPES] = {"love",  "like",      "dislike",
                                               "laugh", "emphasize", "question"};
    if (!t) return -1;
    for (int i = 0; i < BB_TAP_TYPES; i++)
        if (!strcmp(t, kTypes[i])) return i;
    return -1;
}

// Décompose un associatedMessageType : renvoie l'indice (ou -1) et pose
// *remove si le type était préfixé « - » (retrait de réaction).
inline int bbTapParseType(const char* t, bool* remove) {
    *remove = false;
    if (!t || !*t) return -1;
    if (*t == '-') {
        *remove = true;
        t++;
    }
    return bbTapTypeIndex(t);
}

// « p:0/GUID » (ou « bp:0/GUID ») → « GUID » ; sans « / » : inchangé.
inline const char* bbTapTarget(const char* assocGuid) {
    const char* slash = strrchr(assocGuid, '/');
    return slash ? slash + 1 : assocGuid;
}

// Applique une réaction à un compteur par type (saturation, plancher 0).
inline void bbTapApply(uint8_t counts[BB_TAP_TYPES], int type, bool remove) {
    if (type < 0 || type >= BB_TAP_TYPES) return;
    if (remove) {
        if (counts[type]) counts[type]--;
    } else if (counts[type] < 255) {
        counts[type]++;
    }
}
