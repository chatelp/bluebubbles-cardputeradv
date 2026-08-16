# Analyse du chargement de la liste — causes racines et refonte v0.2

Analyse ultracode du 2026-08-15 : 6 agents (lecture du source serveur
BlueBubbles 1.9.9 et de l'app officielle Flutter, commit 08c69ce), chaque
affirmation contre-vérifiée sur le code, deux reproduites empiriquement sur
SQLite 3.44.4. Statut : **proposition à valider par le product owner**
(les décisions ratifiées iront dans docs/01).

## Causes racines confirmées

### 1. `chat/query` : le « dernier message » est le dernier *inséré*, pas le dernier *daté*

`getChats(withLastMessage)` sélectionne le message joint par
`GROUP BY chat.guid` + `HAVING message.ROWID = MAX(message.ROWID)`, sans
aucun `ORDER BY` sur une colonne de message
(`imessage/index.ts:83-113`). L'extension SQLite « bare columns avec
min()/max() » rend ce choix déterministe : c'est la ligne au **ROWID
maximal** — le message le plus récemment **inséré dans la chat.db du Mac**.
Or la synchronisation iCloud/multi-appareils insère régulièrement des
messages *anciens* (backfill) avec des ROWID élevés : la conversation la
plus active peut se voir attribuer une date de dernier message périmée,
qui varie au gré des synchronisations. D'où le « parfois présente, parfois
absente » du top-K trié côté client. Vérifié empiriquement : un chat dont
la ligne au ROWID max porte la date la plus *ancienne* renvoie bien cette
vieille date.

À noter : le serveur possède une définition correcte (par date) dans
`getChatLastMessage` (`index.ts:123-133`), utilisée par `GET /chat/:guid`
— mais pas par `chat/query`.

### 2. Pagination avant tri, en ordre de création **descendant**

Déjà documenté (docs/01) : la page de chats est découpée sur
`chat.ROWID DESC` (fils les plus récemment *créés* d'abord), le tri
« lastmessage » n'arrivant qu'après, en JS, sur la page tronquée. Deux
conséquences : un fil ancien très actif peut être absent des premières
pages, et notre balayage plafonné à 180 chats peut ne jamais atteindre un
fil créé il y a des années.

### 3. Guids multiples pour une même personne — le « fil vide »

Dans chat.db, une même personne 1:1 possède jusqu'à plusieurs fils :
`iMessage;-;+33…`, `iMessage;-;adresse@icloud.com`, `SMS;-;+33…`. Un
message entrant rejoint **le fil du service et de l'alias par lesquels il a
voyagé** (`actions.ts:531`, `scripts.ts:173-197`) ; ni le serveur ni l'app
ne re-routent. L'app officielle n'affiche pas non plus de fusion : une
tuile par guid. Le symptôme « la conversation apparaît enfin, mais vide »
correspond à l'affichage d'un **fil jumeau quasi vide** (alias e-mail,
jumeau SMS, variante résiduelle) de la même personne — en l'ouvrant, on
consulte l'historique de CE fil-là, pas du fil principal.

### 4. Non-problème : le texte `NULL`

Sur le serveur actuel (1.9.9), `text` est **toujours** peuplé via
`universalText()` (extraction d'`attributedBody` quand la colonne SQL est
NULL) — `MessageSerializer.ts:132`, `Message.ts:18-26`. Ne **pas**
demander `attributedBody` dans `with` (ça n'ajoute que le tableau brut,
volumineux). `text` peut rester vide seulement pour : pièce jointe seule,
événements de groupe (`itemType != 0`), stickers, rares échecs de décodage
typedstream — à tolérer par placeholder, pas par suppression silencieuse.

## Ce que fait l'app officielle (et qui valide l'idée « épinglées »)

- **Sync initiale** : la totalité des chats (pages de 200) copiée dans une
  base locale (ObjectBox) ; `sort: null` — l'app native **ne fait jamais
  confiance à l'ordre du serveur**.
- **Ensuite `chat/query` n'est plus jamais réinterrogé.** L'ordre est
  entretenu localement : messages entrants (socket) + rattrapage
  incrémental par `POST /message/query` depuis un **marqueur persisté**
  (ROWID ou `after`), chaque message portant son chat embarqué — exactement
  le principe « dériver les conversations des messages récents ».
- Tri local : épinglés d'abord, puis date du dernier message, avec une
  garde « la date ne recule jamais » (tolérance 2 s).

L'idée du PO (premier démarrage lent et exhaustif → déduction de
conversations « épinglées » qui pilotent la suite) est la miniaturisation
de cette architecture, avec la NVS dans le rôle de la base locale.

## Design v0.2 révisé (après revue adversariale)

La revue a éliminé deux étapes du plan initial : le balayage `chat/query`
en calibration (~350 Ko de payloads aux dates non fiables, redondant — un
chat sans message dans la fenêtre ne peut pas être épinglé) et les 15
requêtes par-chat au démarrage (redondantes avec le rattrapage, et elles
monopoliseraient l'unique connexion TLS).

1. **Calibration** (premier démarrage, demande manuelle, ou marqueur
   > 7 jours) : balayage unique de `message/query` — pages de 10,
   `with:["chats"]`, curseurs `before` (stables face aux insertions, pas
   d'`offset`), arrêt à 300 messages OU 30 jours OU 30 pages. ~25 s,
   progression déterministe (« page N/M »), liste rendue dès la
   page 1, pages suivantes en fond cédant la main aux actions utilisateur.
2. **Score** par conversation : fréquence pondérée par récence, sur une
   clé **fusionnée** : adresse du correspondant normalisée extraite du guid
   après `;-;` (e-mails en minuscules exacts ; téléphones chiffres
   seulement, comparés par suffixe de 9 chiffres — 9 et non 10, sinon le 0
   de la numérotation française empêche la fusion ; préfixe de service
   exclu → les jumeaux iMessage/SMS d'une même personne fusionnent ; les
   groupes — guid sans `;-;` — jamais fusionnés). Top-12 épinglées.
3. **Guid canonique** d'une entrée fusionnée = celui du message le plus
   récent vu — c'est le fil réellement actif : jamais vide à l'ouverture,
   et c'est la bonne cible de réponse (même service/alias que le dernier
   message du correspondant).
4. **Persistance NVS** : un blob unique versionné (~3 Ko : guid, clé, titre,
   extrait, guid du dernier message, date, score × 12) + drapeau `ok` écrit en dernier
   (atomicité face aux coupures). Marqueur = date serveur max observée.
5. **Démarrages suivants** : liste immédiate depuis la NVS (indicateur
   « MAJ… ») + rattrapage = le poll standard.
6. **Polling** : `message/query` limite 10,
   `after = marqueur - 1`, dédup par guid de message ; si la page est
   pleine sans atteindre le marqueur, continuer par curseurs `before`
   (5 pages max, sinon resync) ; traitement du plus ancien au plus récent ;
   garde « la date ne recule jamais ». **Le marqueur n'avance qu'une fois
   la borne atteinte** (sinon des messages se perdent dans les rafales).
7. **Marqueur** : toujours une date **serveur** (jamais l'horloge du
   Cardputer) ; tenu en RAM, persisté au plus toutes les 5 min + envoi/
   ouverture/extinction (usure NVS).
8. **Réactions et événements** (`associatedMessageGuid`, `itemType != 0`) :
   exclus du score, des extraits et du classement, mais leurs dates font
   avancer le marqueur (sinon refetch perpétuel).
9. **Vue conversation** : profondeur configurable dans le portail
   (4-15, défaut 10) ; rafraîchissement par `after = dernier connu - 1`
   avec dédup — un poll sans nouveauté ne coûte que quelques centaines
   d'octets.
10. **Entretien** : décroissance quotidienne des scores alimentée par le
    trafic du poll (promotion/rétrogradation continues) ; recalibration
    manuelle (portail + touche) ; automatique uniquement si marqueur
    > 7 jours.

**Budget mémoire** : les chiffres réellement mesurés sur l'appareil (et la
raison des pages de 10) sont dans docs/02 — la contrainte n'est pas le heap
total libre mais le plus grand bloc **contigu** allouable, ~31 Ko.

**Perte connue et assumée** (identique à l'app officielle) : un message
backfillé par iCloud avec une date ancienne est invisible au polling
`after` ; seule l'ouverture du fil ou une recalibration le fait apparaître.
