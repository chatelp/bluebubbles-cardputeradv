# Doctrine mémoire et vitesse

Règles fermes qui gouvernent **toute** implémentation à venir. Ratifiée par
le product owner le 2026-08-15 : *« l'objectif principal est d'avoir une app
fonctionnelle et rapide, même si ça signifie charger moins de messages ou de
conversations. L'UX est primordiale. »* En cas de conflit entre exhaustivité
et vitesse, **la vitesse gagne, toujours**.

## La réalité mesurée (ne pas raisonner de mémoire — mesurer)

Chiffres relevés sur l'appareil, WiFi + TLS actifs (2026-08-15) :

| Poste | Coût |
|---|---|
| SRAM totale utilisable | ~320 Ko (pas de PSRAM sur l'ESP32-S3FN8) |
| Sprite plein écran 16 bits | 64 Ko, permanent |
| Session TLS (mbedTLS, Cloudflare sans MFLN) | ~45-50 Ko, quasi permanent |
| Tampons audio (5 sons) | 29 Ko, permanents |
| Heap **total** libre en fonctionnement | ~73 Ko |
| Plus grand bloc **contigu** allouable | **~31 Ko** — et il baissera |

**Le chiffre qui gouverne est le bloc contigu, pas le total libre.** Le tas
est fragmenté par les gros blocs permanents ; `getFreeHeap()` ment par
omission. Toute garde mémoire se fait sur `ESP.getMaxAllocHeap()`.

## Les huit règles

**R1 — Jamais un corps de réponse entier en mémoire.** Le JSON se parse en
flux depuis la socket TLS avec un filtre ArduinoJson (`requestJson`,
bb_client.cpp) ; seul le résultat filtré est alloué, par petits blocs. Le
repli tamponné (chunked) est borné à 24 Ko. Aucune nouvelle route ne
réintroduit `getString()` sur un corps non borné.

**R2 — Aucune allocation dynamique > 8 Ko** dans le flux normal. Ce qui est
gros est **permanent et alloué au boot** (sprite, sons) ; ce qui est
dynamique est petit et bref. Une exception se justifie par écrit dans docs/02
avec sa mesure.

**R3 — Des pages petites, des requêtes nombreuses.** Le keep-alive rend une
requête supplémentaire quasi gratuite (~0,3-0,7 s) ; un gros corps, lui,
menace le tas et ralentit le premier rendu. Pages de 10 messages, point.

**R4 — Le réseau ne touche jamais l'UI.** Tout passe par la tâche réseau ;
la boucle UI ne bloque sous aucun prétexte. Une frappe clavier répond en
moins d'une frame, toujours.

**R5 — L'écran n'attend pas le réseau.** Au boot : liste depuis la NVS en
moins d'une seconde, mise à jour silencieuse ensuite. Une erreur réseau est
un bandeau de 13 px sur du contenu en cache — jamais un écran vide, jamais
un spinner plein écran (seule exception : la calibration, quand il n'existe
littéralement rien à montrer, et sa progression a un dénominateur).

**R6 — Dégradation, jamais de plantage.** Mémoire tendue → page plus petite
→ requête sautée → au pire une donnée plus ancienne à l'écran. Tout rejet
s'imprime sur la série (`[bb] …`) : un plafond se diagnostique en une ligne.

**R7 — Charger moins est une solution légitime.** Moins de messages par
conversation, moins de conversations listées, une fenêtre de calibration
plus courte : ce sont des réglages assumés, pas des régressions — tant que
ce qui est affiché est juste et frais. L'inverse (tout charger, lentement)
est la régression.

**R8 — Toute fonctionnalité nouvelle paie d'avance.** Avant d'implémenter :
annoncer son coût RAM (permanent + transitoire). Coût permanent > 10 Ko →
elle doit être désactivable, et son coût nul une fois désactivée (modèle :
les sons, 29 Ko rendus seulement si activés). Après l'ajout : re-mesurer
heap et bloc contigu au boot, mettre à jour le tableau de docs/02.

## Budgets UX chiffrés

| Moment | Budget |
|---|---|
| Boot → liste affichée (NVS) | < 1 s |
| Frappe clavier → écho à l'écran | < 50 ms |
| Ouverture d'une conversation (réseau) | < 1,5 s |
| Envoi → retour visuel « Envoi… » | immédiat |
| Poll sans nouveauté | invisible (quelques centaines d'octets) |
| Calibration complète | ~25 s, progression affichée, liste utilisable avant la fin |

Un dépassement répété d'un de ces budgets est un bug, au même titre qu'un
crash.

## Conséquences pour les chantiers envisagés (docs/03)

- **Vignettes de pièces jointes** : interdites en pleine résolution. Un
  décodage JPEG devra être progressif (ligne à ligne vers le sprite) avec un
  tampon < 8 Ko, ou demander au serveur une miniature — sinon on affiche
  `[pièce jointe]` et c'est très bien.
- **Pagination arrière de l'historique** : par pages de 10 avec `before`,
  en remplaçant les messages affichés (fenêtre glissante plafonnée à 30),
  jamais en accumulant.
- **Tapbacks, indicateurs** : uniquement s'ils tiennent dans les structures
  existantes (un octet par message), pas de nouvelle collection.
