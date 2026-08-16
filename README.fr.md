*[English](README.md) · **Français***

# BlueBubbles Cardputer

**Un client iMessage de poche.**
M5Stack Cardputer ADV · ESP32-S3 · écran 240 × 135 · clavier 56 touches.

Un client léger pour [BlueBubbles](https://bluebubbles.app), la passerelle
iMessage open source (Apache-2.0) qui tourne sur un Mac. Le Cardputer
interroge son API REST en HTTPS, affiche les conversations en vraies bulles
et envoie des messages depuis son clavier physique. Pas d'application, pas
de compte, pas de nuage au milieu — votre Mac et votre appareil.

---

## Ce qu'il fait

| | |
|---|---|
| **Conversations** | Liste fusionnée (une entrée par personne, pas par fil), aperçu, pastille non-lu |
| **Bulles** | Alignement gauche/droite, ergot, groupement par auteur, séparateurs horaires centrés |
| **Émojis** | 42 glyphes pixel couvrant 68 codepoints — jamais de carré vide |
| **Tapbacks** | Réactions affichées en pilule à cheval sur le coin de la bulle |
| **Envoi** | Private API ou AppleScript, avec un état honnête « parti mais non confirmé » |
| **Sons** | Cinq timbres de barre frappée, pas des bips — chacun désactivable |
| **Bilingue** | Anglais et français, au choix sur l'appareil ou dans le portail |

Texte seulement, par choix : les pièces jointes s'affichent
`[pièce jointe]`. Polling uniquement — ni Socket.IO, ni webhooks.

---

## Configuration, dans un navigateur

Au premier démarrage l'appareil ouvre un point d'accès : rejoignez
`CardputerBB` (mot de passe `bluebubbles`) et ouvrez `http://192.168.4.1`.
Ensuite, le même formulaire vit sur `http://cardputer.local`.

WiFi, adresse du serveur et mots de passe sont **réservés au portail** : la
tâche réseau lit la configuration en permanence sur l'autre cœur, et
réaffecter une chaîne depuis le cœur de l'interface la ferait planter. Tout
ce qui est numérique — langue, volume, les quatre interrupteurs de son,
rythme, profondeur d'historique — se règle aussi sur l'appareil, et les deux
chemins ne peuvent pas s'écraser en silence : le portail refuse (HTTP 409)
un formulaire rendu avant qu'un réglage ne change sur l'appareil.

Le mot de passe de votre serveur ne vit qu'en NVS sur l'appareil. Il n'est
jamais une valeur par défaut du firmware et n'entre jamais dans ce dépôt.

---

## Touches

| Touche | Liste | Conversation | Composition | Réglages |
|---|---|---|---|---|
| `;` `.` | naviguer | défiler, de bulle en bulle | — | changer de champ |
| `,` `/` | — | — | — | changer la valeur |
| `Entrée` | ouvrir | écrire | envoyer | — |
| `` ` `` | infos | retour | annuler | enregistrer et sortir |
| `r` | resynchroniser | — | — | — |
| `p` / `s` | *(écran Infos)* tester le serveur / réglages | | | |

---

## Compiler

```bash
pio run -e cardputer-adv -t upload
```

```bash
pio test -e test-native
```

La logique pure — décodage des flux HTTP, segmentation UTF-8 et émojis,
arrêts de défilement, analyse des tapbacks, clés de fusion des
conversations — vit dans des en-têtes de `include/` et est couverte par
34 tests natifs qui tournent sur votre machine, sans appareil branché. Sur
un écran de cette taille, ce banc d'essai est ce qui prouve la justesse.

---

## Prérequis

- Un serveur BlueBubbles fonctionnel (Mac + iMessage), joignable en réseau
  local ou en HTTPS (Cloudflare, ngrok, DNS dynamique)
- Un M5Stack **Cardputer ADV** — le Cardputer d'origine devrait convenir
  (même bibliothèque), mais n'a pas été testé

---

## Vérités matérielles

L'ESP32-S3FN8 n'a **pas de PSRAM**. Ce qui gouverne toutes les décisions
ici n'est ni les 8 Mo de flash ni les ~230 Ko de tas libre, mais le **plus
gros bloc contigu que le tas peut encore fournir en fonctionnement :
environ 31 Ko**. D'où des réponses analysées directement depuis la socket
TLS sans jamais bufferiser un corps, des requêtes paginées, et une doctrine
ratifiée — *la vitesse et l'UX priment sur l'exhaustivité* — à laquelle
toute nouvelle fonctionnalité doit répondre.

Les pièges qui ont coûté le plus cher sont consignés dans
[docs/02](docs/02_ARCHITECTURE.md), dont celui qui a été le plus long à
trouver : sur TLS, `NetworkClient::readBytes` traite une socket
momentanément vide comme une erreur fatale, tronquant en silence toute
réponse plus grosse que le tampon déchiffré.

---

## Documentation

| | |
|---|---|
| [CLAUDE.md](CLAUDE.md) | doctrine du projet, garde-fous de périmètre |
| [docs/01](docs/01_DECISIONS.md) | journal des décisions, datées et actées |
| [docs/02](docs/02_ARCHITECTURE.md) | architecture, budget mémoire mesuré, pièges matériels |
| [docs/04](docs/04_ANALYSE_CHARGEMENT.md) | pourquoi `chat/query` est inutilisable, et ce qui le remplace |
| [docs/05](docs/05_DESIGN.md) | le langage visuel « encre et bulle » |
| [docs/06](docs/06_DOCTRINE_MEMOIRE.md) | doctrine mémoire et vitesse |

---

## Licence

[MIT](LICENSE) — prenez-le, apprenez-en, construisez dessus.

BlueBubbles est un projet indépendant sous Apache-2.0 : ce dépôt ne reprend
aucune ligne de son code et ne consomme que son API documentée. iMessage et
Apple sont des marques d'Apple Inc. ; ce projet n'est affilié ni à Apple ni
à BlueBubbles. Les bibliothèques tierces (M5Unified, M5GFX, ArduinoJson)
conservent leurs licences.
