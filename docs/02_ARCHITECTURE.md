# Architecture

## Vue d'ensemble

```
┌─────────────── Cardputer ADV ───────────────┐        ┌───── Mac ─────┐
│ main.cpp        machine à états + rendu     │  HTTPS │ BlueBubbles   │
│ bb_client.cpp   client REST (polling) ──────┼────────▶ Server        │
│ config_portal   serveur web de config       │  REST  │ (Apache-2.0)  │
│ app_config      persistance NVS             │        └───────┬───────┘
└───────────────▲─────────────────────────────┘             iMessage
                │ HTTP local (navigateur)
         config par l'utilisateur
```

Quatre modules, séparés pour rester testables et lisibles :

| Module | Rôle | Dépendances |
|---|---|---|
| `main.cpp` | écrans (setup, liste, conversation, composition, infos), clavier, boucle de polling | M5Cardputer, tous les autres |
| `bb_client` | requêtes REST + parsing filtré → structs `BBChat`/`BBMsg` | HTTPClient, ArduinoJson, certs.h |
| `config_portal` | formulaire web (AP captif ou STA), sauvegarde, redémarrage | WebServer, DNSServer, ESPmDNS |
| `app_config` | struct de config globale `gConfig`, NVS (`Preferences`, ns `bbcfg`) | Preferences |

## Machine à états d'écrans

```
SETUP (AP) ──config──▶ reboot
CHATS ◀──`──▶ INFO
  │ enter                    ` = retour/infos, ;/. = naviguer/défiler
  ▼
MESSAGES ──enter──▶ COMPOSE ──enter──▶ envoi ──▶ MESSAGES
  ▲__________`_________│
```

## Flux de données et budget mémoire (pas de PSRAM)

- ~300 Ko de heap utiles. Postes principaux : session TLS ~45 Ko (une
  seule, client `WiFiClientSecure` statique réutilisé), sprite écran 64 Ko,
  payload JSON transitoire (≤ ~60 Ko avec les limites 12 chats /
  20 messages), documents ArduinoJson filtrés (quelques Ko).
- `getString()` puis `deserializeJson` avec **filtre** : on ne garde que
  guid, titre, texte, date, expéditeur. Le streaming direct sur
  `http.getStream()` est volontairement évité (encodage chunked non décodé
  par HTTPClient).
- Les guids de chat (`iMessage;-;+336…`) contiennent `;` et `+` :
  **URL-encodage obligatoire** dans les chemins.

## Polling

- Liste des chats : toutes les `pollSec` (10 s défaut) sur l'écran CHATS.
- Messages du chat ouvert : même cadence sur l'écran MESSAGES.
- Jamais pendant COMPOSE (les requêtes sont bloquantes ~1-3 s : elles
  mangeraient les frappes). Amélioration possible : tâche FreeRTOS dédiée
  (voir roadmap).
- Nouveaux messages détectés par comparaison `lastMessage.dateCreated` à la
  table `sSeen` (RAM) → pastille ● + bip 880 Hz.

## Budget mémoire réel (mesuré le 2026-08-15)

Chiffres relevés sur l'appareil en fonctionnement, WiFi et TLS actifs :

| Grandeur | Valeur |
|---|---|
| Heap libre au boot, après le sprite | ~230 Ko |
| Tampons audio (5 sons, 11 025 Hz) | 29 Ko |
| Heap **total** libre en fonctionnement | ~73 Ko |
| Plus grand bloc **contigu** allouable | **~31 Ko** |
| Corps d'un message JSON (avec chat embarqué) | ~1,8 Ko |

**C'est le bloc contigu qui limite, pas le total libre.** Le sprite plein
écran (64 Ko) et les tampons audio fragmentent le tas ; la `String` qui
reçoit le corps d'une réponse a besoin, elle, d'un seul tenant. D'où :

- pages de **10 messages** (~18 Ko de corps) pour `message/query`, en
  calibration comme en polling ;
- profondeur d'historique plafonnée à **15** messages (et non 25) ;
- `MAX_PAYLOAD` = 26 Ko, marge de travail 6 Ko, contrôle fait sur
  `ESP.getMaxAllocHeap()` — jamais sur `getFreeHeap()`, qui aurait laissé
  passer une allocation vouée à l'échec.

Un rejet est toujours tracé sur la console série (`[bb] rejet …`) : un
plafond mal calé doit se diagnostiquer en une ligne, pas se deviner.

## Piège HTTP appris sur matériel (2026-08-16)

`http.getStreamPtr()` livre le flux **brut** : le décodage
`Transfer-Encoding: chunked` n'existe que dans `writeToStream()`. Un parse
en flux doit donc décoder les chunks lui-même (`ChunkedStream`,
bb_client.cpp), sinon le balisage hexadécimal se mélange au JSON
(« IncompleteInput »). Le piège était invisible tant que le keep-alive ne
fonctionnait pas : Cloudflare ne passe en chunked que sur les réponses des
connexions réutilisées. Deuxième piège liée : le keep-alive exige des
objets `HTTPClient` **persistants** — un objet local a `_host` vide et la
bibliothèque coupe alors la connexion encore ouverte à chaque requête.

Troisième piège, le plus sournois (2026-08-16, cause racine des
« IncompleteInput » persistants) : **ne jamais lire le corps par
`NetworkClient::readBytes`**. Sur TLS, `NetworkClientSecure::read()` rend
`-1` dès que `available() == 0` — connexion pourtant saine, simple creux
entre deux records — et `readBytes()` traite ce `-1` en erreur fatale
(`break` immédiat : sa boucle d'attente ne joue que pour un retour `0`).
Toute réponse dépassant le tampon déchiffré (~3-4 Ko) « finissait » donc en
plein corps ; les petites réponses, déjà entièrement bufferisées, passaient
— d'où une panne qui ne touchait que la calibration et les conversations.
Le remède est `NetSource` (bb_client.cpp) : la boucle
`available()`/`connected()` + attente courte de HTTPClient lui-même, bornée
par une échéance absolue, seule source donnée aux décodeurs de
`bb_streams.h`.

## Piège de persistance (2026-08-16)

Le marqueur se persiste seul toutes les 5 minutes (`storeSaveMarker`), la
liste seulement sur `sListChanged`. Tant que ce drapeau n'était posé qu'à
l'**entrée** d'une conversation, l'appareil redémarrait avec un marqueur à
jour et des aperçus vieux de la dernière calibration : le poll, n'ayant rien
de plus récent à rapporter, ne les corrigeait jamais. Règle : **tout champ
affiché qui vit dans le blob doit poser `sListChanged`** — un marqueur qui
avance plus vite que les données qu'il indexe fige l'affichage pour de bon.

## Piège de police (2026-08-16)

`efontJA_12` n'a pas les espaces typographiques Unicode. iMessage insère en
français une insécable (U+00A0) ou une fine insécable (U+202F) avant
« ? ! ; : » : sans normalisation elles s'affichent en carré vide et faussent
`textWidth`. Tout texte venant du serveur passe par `bbNormalizeSpaces`
(bb_emoji.h) à l'entrée — pas au rendu, sinon la mesure et le découpage de
lignes travailleraient encore sur les octets d'origine.

## Piège M5GFX (appris sur matériel le 2026-08-14)

Ne **jamais** construire le `M5Canvas` global avec `&M5Cardputer.Display`
en parent : l'objet est construit avant `M5Cardputer.begin()` et le premier
`pushSprite(0, 0)` panique (`LoadProhibited`, boucle de redémarrage, écran
qui clignote). Pattern sûr — le même que Daoa Mini : canvas global **sans
parent**, `createSprite()` appelé dans `setup()` après `begin()` et
**vérifié** (`!= nullptr`, repli 8 bits), poussée explicite par
`pushSprite(&M5Cardputer.Display, 0, 0)`.

## TLS

`include/certs.h` embarque GTS Root R1 + GTS Root R4 (Google Trust
Services, chaînes Cloudflare) + ISRG Root X1 (Let's Encrypt), concaténées —
mbedTLS accepte plusieurs PEM dans un même buffer. Régénération :

```bash
curl -sL https://i.pki.goog/r1.pem -o /tmp/r1.pem
curl -sL https://i.pki.goog/r4.pem -o /tmp/r4.pem
curl -sL https://letsencrypt.org/certs/isrgrootx1.pem -o /tmp/x1.pem
# puis reconstruire le R"CERT(...)CERT" de include/certs.h avec ces trois PEM
```

GTS R1/R4 expirent en 2036, ISRG X1 en 2035.

## Sécurité — hypothèses assumées

- Le **portail web est en HTTP local non authentifié** : quiconque est sur
  le LAN peut lire/modifier la config (le mot de passe serveur est dans le
  HTML préservi). Hypothèse : réseau personnel de confiance. En mode AP,
  l'accès est protégé par le mot de passe WPA2 de l'AP.
- Le mot de passe serveur transite en query string vers BlueBubbles :
  c'est le schéma d'auth officiel de l'API ; en HTTPS il est chiffré.
- Rien de secret dans le dépôt ni dans le firmware compilé hors NVS.
