# CLAUDE.md — Silicon Bubbles (Cardputer ADV)

Claude Code est l'agent de développement principal de ce projet. Lire ce
fichier avant toute décision d'implémentation, puis `docs/01_DECISIONS.md`
(décisions actées), `docs/06_DOCTRINE_MEMOIRE.md` (**règles fermes ratifiées
par le PO : vitesse et UX priment sur l'exhaustivité, le bloc de heap
contigu ~31 Ko gouverne tout, toute fonctionnalité annonce son coût RAM
avant implémentation**) et `docs/03_ROADMAP.md` (état d'avancement).

## Ce qu'est ce projet

Un **client iMessage léger** pour M5Stack **Cardputer ADV** (ESP32-S3FN8,
écran 240 × 135, clavier 56 touches), qui parle à un serveur
[BlueBubbles](https://github.com/BlueBubblesApp/bluebubbles-server) via son
**API REST**, par **polling**. Boucle produit : liste des conversations →
lecture d'une conversation → composition/envoi → retour.

La configuration (WiFi, URL et mot de passe du serveur, intervalle de
polling, TLS) se fait **par navigateur** grâce au portail web embarqué :
mode AP au premier démarrage (`http://192.168.4.1`), puis sur l'IP du
Cardputer / `http://cardputer.local` en usage normal.

## Garde-fous de périmètre

- **Polling REST uniquement.** Pas de Socket.IO ni de webhooks (décision
  actée, voir docs/01) tant que le product owner n'en décide pas autrement.
- v1 : texte seulement. Pas d'affichage de pièces jointes (placeholder
  `[pièce jointe]`), pas de tapbacks, pas d'indicateurs de frappe.
- **Aucun secret dans le dépôt.** Le mot de passe serveur vit uniquement en
  NVS sur l'appareil. **L'URL du serveur de test de Pierre et son mot de
  passe ne doivent JAMAIS entrer dans le dépôt** (ni en valeur par défaut,
  ni en commentaire, ni dans une capture de portail) : ce sont des données
  de test locales, à lui demander. Le dépôt est public.
- **Interface bilingue anglais / français, anglais par défaut** (décision PO
  du 2026-08-16, remplace la règle « tout en français »). Toute chaîne
  affichée passe par `T(S_…)` (`include/i18n.h`) côté appareil, ou par
  `P("en", "fr")` côté portail web — jamais de littéral affiché en dur.
  **La documentation et les commentaires de code restent en français.**

## Décisions techniques actées (résumé — détail dans docs/01)

- **Stack** : PlatformIO + Arduino-ESP32 core 3.x (pioarduino),
  `M5Cardputer` ≥ 1.1.1 (gère le clavier TCA8418 de l'ADV), `M5Unified`,
  `M5GFX`, `ArduinoJson` 7. Pas de LVGL. Partition `max_app_8MB.csv`
  (pas d'OTA, pas de SPIFFS).
- **Pas de PSRAM** sur l'ESP32-S3FN8 : une seule connexion TLS à la fois,
  réponses paginées (12 chats, 20 messages), parsing ArduinoJson avec
  filtre. Sprite plein écran 16 bits (~64 Ko) accepté au budget mémoire.
- **TLS** : racines GTS R1/R4 + ISRG X1 embarquées (`include/certs.h`,
  régénérables — voir docs/02). Option « ne pas vérifier » dans le portail
  pour les certificats auto-signés.
- **Police** : `efontJA_12` (couvre l'ASCII + latin accentué). Les fontes
  ASCII de M5GFX n'ont pas les accents français.
- **API BlueBubbles** utilisée : `GET /api/v1/ping`,
  `POST /api/v1/message/query` (liste des conversations, dérivée des
  messages récents — **ne pas** utiliser `chat/query`, qui pagine sur
  ROWID avant de trier, voir docs/01), `GET /api/v1/chat/:guid/message`
  (guid **URL-encodé** : il contient `;` et `+`),
  `POST /api/v1/message/text` (méthode `private-api` par défaut).
  Auth par `?password=` en query string.

## Commandes

```bash
pio run -e cardputer-adv              # compiler
pio run -e cardputer-adv -t upload    # flasher (USB-C)
pio device monitor                    # console série (115200)
pio test -e test-native               # tests natifs (logique pure)
pio run -e sim                        # simulateur SDL macOS
.pio/build/sim/program                #   fenêtre interactive ×3
.pio/build/sim/program --screens captures/en   # captures BMP sans fenêtre
```

## Structure

```
include/   app_config.h, bb_client.h, config_portal.h, certs.h, i18n.h,
           bb_streams.h, bb_emoji.h, bb_scroll.h, bb_tapback.h (logique pure,
           testée en natif : pio test -e test-native)
src/       main.cpp (état, réseau, NVS, clavier), bb_client.cpp (REST),
           config_portal.cpp (portail web), app_config.cpp (NVS),
           sound.cpp (synthèse), main_sim.cpp (simulateur SDL + démo)
src/ui/    rendu PORTABLE appareil/simulateur : display.h (M5GFX ↔
           LovyanGFX), theme.h (palette+géométrie), model.h (UiModel),
           render.cpp — ne touche jamais réseau/NVS/matériel
docs/      00 analyse, 01 décisions, 02 architecture, 03 roadmap
```

## Conventions héritées des projets Cardputer précédents

Mêmes conventions que `daoa-mini-cardputeradv` et `geek-casino_cardputeradv` :
docs numérotés en français, journal de décisions au format
« date — décision — statut ACTÉ », README bilingue (`README.md` EN,
`README.fr.md` FR), licence MIT, C++17, code commenté en français.
