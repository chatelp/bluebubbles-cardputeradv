# Journal des décisions

Format : date — décision — statut. Les décisions actées ne se rediscutent
qu'avec le product owner.

## 2026-08-14 — Périmètre v1 — ACTÉ

Client de **lecture et d'envoi de texte** : liste des conversations (12),
vue conversation (20 derniers messages), composition, envoi. Tapbacks,
événements de groupe et pièces jointes exclus de l'affichage v1 (les
tapbacks et événements sont filtrés, les pièces jointes remplacées par
`[pièce jointe]`). Interface en français.

## 2026-08-14 — Temps réel par polling REST — ACTÉ

Pas de Socket.IO : les implémentations Engine.IO sur ESP32 sont fragiles et
le protocole ajoute une session TLS permanente. Pas de webhooks : il
faudrait que le Mac joigne le Cardputer (LAN seulement, NAT). Le **polling
REST** (10 s par défaut, réglable ≥ 3 s) est simple, robuste, et suffit à
l'usage. Pas de polling pendant la composition (requêtes bloquantes =
frappes perdues).

## 2026-08-14 — Configuration par portail web embarqué — ACTÉ

Demande explicite du product owner. Premier démarrage (ou WiFi introuvable) :
AP `CardputerBB` / `bluebubbles`, portail captif sur `http://192.168.4.1`.
En usage normal : même formulaire servi sur l'IP STA et `http://cardputer.local`
(mDNS). Champs : WiFi, URL serveur, mot de passe serveur, intervalle de
polling, fuseau horaire, méthode d'envoi, vérification TLS. Sauvegarde en
NVS puis redémarrage. Le portail est en HTTP local non authentifié :
réseau de confiance requis (documenté dans docs/02).

## 2026-08-14 — Stack technique — ACTÉ

Même socle que Daoa Mini : PlatformIO + Arduino-ESP32 core 3.x (pioarduino),
`M5Cardputer` ≥ 1.1.1, C++17, partition `max_app_8MB.csv`, pas de LVGL.
S'y ajoute `ArduinoJson` 7 (parsing filtré). Serveur de test :
l'instance personnelle de Pierre (hors dépôt) — jamais en dur dans
le firmware.

## 2026-08-14 — TLS : racines embarquées + option de contournement — ACTÉ

L'instance de test est derrière Cloudflare (chaîne Google Trust Services
WE1 → GTS Root R4). On embarque **GTS Root R1, GTS Root R4 et ISRG Root X1**
(Let's Encrypt) concaténées dans `include/certs.h` — cela couvre Cloudflare
et les certificats Let's Encrypt usuels. Case « vérifier le certificat TLS »
décochable dans le portail pour les certs auto-signés. `setInsecure()`
n'est jamais le défaut.

## 2026-08-14 — Envoi via `private-api` par défaut — ACTÉ

`POST /api/v1/message/text` avec `method: "private-api"` (réactivité,
pas de fenêtre AppleScript côté Mac), basculable sur `apple-script` dans le
portail pour les serveurs sans Private API. `tempGuid` toujours fourni
(requis par la méthode apple-script, inoffensif sinon).

## 2026-08-14 — Liste des conversations : bootstrap + polling hybrides — ACTÉ

Découvert sur matériel puis confirmé dans le source du serveur
(`chatInterface.ts`, `imessage/index.ts`) : `POST /api/v1/chat/query` pagine
sur `chat.ROWID` (ordre de **création** des fils) et n'applique le tri
`lastmessage` qu'**après** la pagination — les conversations actives
récentes peuvent manquer de toutes les premières pages. Inversement,
balayer `message/query` seul manque de diversité : les N derniers messages
appartiennent souvent à 1 ou 2 conversations très actives (constaté :
une seule conversation listée). Décision, en deux primitives :

- **Bootstrap** (démarrage, touche `r`) : parcourir **toutes** les pages de
  `chat/query` avec `lastMessage` (15 par page, 12 pages max, arrêt page
  incomplète), en ne gardant que le **top-20 par date côté client**.
  Publication incrémentale page par page, abandon si une commande
  utilisateur attend.
- **Polling** (toutes les `pollSec`) : dernière page de
  `POST /api/v1/message/query` (tri `dateCreated DESC` fait en SQL,
  fiable) avec `with: ["chats"]` — tout nouveau message y figure ; mise à
  jour/insertion des conversations touchées puis retri client.

Au passage : le champ `hasAttachments` n'existe pas dans le sérialiseur de
messages ; la détection de pièce jointe passe par le tableau `attachments`
(filtré sur `guid`).

## 2026-08-14 — Réseau dans une tâche FreeRTOS — ACTÉ

Constaté sur matériel : les requêtes TLS bloquantes (0,5–3 s) dans `loop()`
rendaient le clavier inutilisable. Tout le réseau vit désormais dans une
tâche FreeRTOS épinglée au cœur 0 (pile 16 Ko, file de 6 commandes :
CHATS_FULL, CHATS_POLL, MSGS, SEND, PING). Données partagées protégées par
mutex (verrou RAII `DataLock` pris par `render()`, `handleKeys()` et les
publications de la tâche). L'UI met en file et ne bloque jamais ; le
brouillon est restauré si un envoi échoue. Le polling redevient possible
pendant la composition.

## 2026-08-14 — Licence du dépôt : MIT — ACTÉ

Code 100 % original (aucun code BlueBubbles porté), donc libre de licence.
MIT pour cohérence avec les autres projets Cardputer de Pierre. BlueBubbles
(Apache-2.0) n'est qu'un service distant consommé.

## 2026-08-16 — Interface bilingue EN/FR, anglais par défaut — **ACTÉ**

Le firmware ne parlait que français. Décision du PO : interface **bilingue,
anglais par défaut**, avec un réglage de langue accessible **des deux
côtés** (écran de l'appareil et portail web). Mise en œuvre : table plate en
flash (`include/i18n.h`, `T(S_…)`) pour l'appareil, helper `P("en", "fr")`
pour les phrases longues du portail. La doc et les commentaires restent en
français.

## 2026-08-16 — Réglages sur l'appareil : périmètre restreint aux POD — **ACTÉ**

Un écran Réglages est accessible depuis Infos (touche `s`) : langue, volume,
quatre interrupteurs de son, rythme de polling, profondeur d'historique.
**Il ne touche que des champs numériques ou booléens.** WiFi, URL, mot de
passe du serveur et vérification TLS restent l'apanage du portail web —
raison technique : la tâche réseau (cœur 0) lit `gConfig` en permanence, et
réaffecter une `String` depuis le cœur 1 la ferait planter (le portail
contourne déjà cela en écrivant une copie puis en redémarrant).

Anti-conflit entre les deux chemins : `AppConfig::rev` est incrémentée à
chaque enregistrement. Le portail embarque la révision dans son formulaire
et **refuse** (HTTP 409, page dédiée) si elle a changé entre-temps, au lieu
d'écraser silencieusement un réglage fait sur l'appareil.

## 2026-08-16 — Nom public : Silicon Bubbles — **ACTÉ**

Le firmware s'appelle **Silicon Bubbles** — famille assumée avec Silicon
Casino, jeu de mots silicium/champagne qui tient dans les deux langues, et
une saine distance de marque avec BlueBubbles (le serveur reste un projet
indépendant : on est « un client compatible BlueBubbles », pas eux).
Appliqué : splash, portail (titre et en-tête), SSID du point d'accès
(`SiliconBubbles`), `/status` (`"app":"silicon-bubbles"`), README, dépôt
GitHub renommé `silicon-bubbles-cardputeradv`.
