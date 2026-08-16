#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>

#include "bb_tapback.h"

// Une entrée de la liste des conversations, après fusion des fils jumeaux
// d'une même personne (voir docs/04) : `key` est la clé de fusion (adresse
// normalisée pour un 1:1, guid pour un groupe), `guid` le fil canonique —
// celui du message le plus récent, cible d'ouverture et de réponse.
struct BBChat {
    String guid;
    String key;
    String title;
    String lastText;
    String lastGuid;        // guid du dernier message absorbé (anti-double comptage)
    int64_t lastDate = 0;   // ms epoch (horloge serveur)
    bool lastFromMe = false;
    float score = 0;        // score de calibration (récence + fréquence)
};

struct BBMsg {
    String guid;
    String text;
    String sender;          // adresse de l'expéditeur, vide si de moi
    int64_t date = 0;       // ms epoch
    bool fromMe = false;
    bool hasAttachment = false;
    uint8_t taps[BB_TAP_TYPES] = {0};  // réactions reçues, par type (bb_tapback.h)
};

// Une réaction rencontrée dans la page : à appliquer sur le message cible
// (qui peut être hors de la page — l'appelant fait le rapprochement).
struct BBTap {
    String guid;    // guid de la LIGNE réaction (déduplication entre polls)
    String target;  // guid du message cible (préfixe de partie retiré)
    int8_t type = -1;
    bool remove = false;
};

// Un message récent tel que renvoyé par message/query avec son chat
// embarqué. Matière première de la calibration et du polling.
struct BBRecent {
    String msgGuid;
    String chatGuid;
    String title;           // displayName (groupe) ou chatIdentifier
    String text;
    int64_t date = 0;
    bool fromMe = false;
    bool isReaction = false;  // associatedMessageGuid non nul
    bool isEvent = false;     // itemType != 0 (renommage, entrée/sortie…)
    bool hasAttachment = false;
};

// Client REST BlueBubbles minimal (polling, pas de Socket.IO).
// Ne jamais utiliser chat/query : son lastMessage est choisi par MAX(ROWID)
// (dernier inséré, pas dernier daté) et il pagine avant de trier — docs/04.
class BBClient {
public:
    bool ping(String& err);

    // Une page de message/query triée dateCreated DESC (tri SQL fiable).
    // beforeMs/afterMs (ms epoch serveur) optionnels (0 = absent) : curseurs
    // de pagination et borne de rattrapage. limit ≤ 15 (budget RAM).
    // rawOut reçoit le nombre de lignes réellement renvoyées par le serveur —
    // `out` peut être plus court (lignes inexploitables filtrées) et ne dit
    // donc pas si la page était pleine.
    // rawOldestOut : date de la dernière ligne BRUTE de la page (même filtrée)
    // — c'est elle qui fait avancer le curseur, sinon une page entièrement
    // filtrée bloquerait la pagination.
    bool fetchMessagesPage(int64_t beforeMs, int64_t afterMs, uint8_t limit,
                           std::vector<BBRecent>& out, size_t& rawOut,
                           int64_t& rawOldestOut, String& err);

    // Messages d'une conversation. afterMs > 0 : uniquement les messages
    // plus récents (rafraîchissement incrémental, réponse minuscule).
    // pageFull : la page serveur était pleine, il reste peut-être des
    // messages non couverts — l'appelant doit recharger en entier.
    // taps : réactions rencontrées dans la page, en ordre CHRONOLOGIQUE
    // (les retraits doivent s'appliquer après les ajouts qu'ils annulent).
    bool fetchMessages(const String& chatGuid, std::vector<BBMsg>& out,
                       std::vector<BBTap>& taps, uint8_t limit, int64_t afterMs,
                       bool& pageFull, String& err);

    // Résultat d'envoi : la distinction « échec certain » / « non confirmé »
    // décide si le brouillon peut être restauré sans risquer un double envoi.
    enum SendResult : int8_t {
        SEND_OK = 0,
        SEND_FAILED = 1,       // le serveur a explicitement refusé (ou rien n'est parti)
        SEND_UNCONFIRMED = 2,  // la requête est très probablement arrivée, réponse perdue
    };
    SendResult sendText(const String& chatGuid, const String& text, String& err);

private:
    String apiUrl(const String& path) const;
    // Requête + parse ArduinoJson filtré, directement depuis le flux TLS —
    // le corps n'est jamais bufferisé en entier (bloc contigu max ~31 Ko,
    // voir docs/02). `out` reçoit le document filtré.
    // httpCodeOut : code HTTP reçu (0 si l'erreur est survenue avant toute
    // réponse — transport). Permet de distinguer échec certain / non confirmé.
    bool requestJson(const char* method, const String& path, const String& jsonBody,
                     JsonDocument& filter, JsonDocument& out, String& err,
                     uint32_t timeoutMs = 15000, int* httpCodeOut = nullptr);
};

// Clé de fusion d'un guid de chat : "g:<guid>" pour un groupe (pas de
// séparateur ";-;"), sinon "e:<email minuscule>" ou "p:<9 derniers chiffres>"
// — 9 et non 10 à cause du 0 de la numérotation nationale française
// (+33 6 12… et 06 12… doivent donner la même clé). Le préfixe de service
// (iMessage/SMS) est volontairement exclu pour fusionner les fils jumeaux
// d'une même personne.
String bbChatKey(const String& chatGuid);
