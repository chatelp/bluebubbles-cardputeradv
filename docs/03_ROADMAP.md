# Roadmap

## v0.1 — MVP (2026-08-14) — code écrit, compile, **à valider sur matériel**

- [x] Étude de faisabilité (docs/00)
- [x] Config NVS + portail web (AP captif + STA + mDNS `cardputer.local`)
- [x] Client REST : ping, liste des chats, messages d'un chat, envoi texte
- [x] TLS vérifié (racines GTS/ISRG embarquées) + option non-vérifié
- [x] UI : liste, conversation, composition, infos ; police accentuée
- [x] Pastille non-lu + bip sur message entrant
- [x] Compile (flash 19 %, RAM statique 17 %)
- [x] Boot validé sur Cardputer ADV réel (après correctif du canvas global —
      voir le piège M5GFX dans docs/02) : sprite 16 bits OK, 230 Ko de heap
      restants après init
- [ ] **Test fonctionnel** contre un serveur réel (config par le
      portail, lecture, envoi)
- [ ] Vérifier le rendu efont (accents) et la réactivité du clavier TCA8418

## v0.2 — Chargement refondu (2026-08-15) — flashé, à valider à l'usage

- [x] Diagnostic des causes racines (docs/04) : `chat/query` renvoie le
      dernier message par MAX(ROWID) et non par date ; fils jumeaux d'une
      même personne
- [x] Calibration par `message/query` (curseurs `before`, ~500 messages),
      score récence + fréquence, top-12 persisté en NVS
- [x] Fusion des fils jumeaux (clé adresse normalisée), guid canonique =
      fil le plus récemment actif
- [x] Polling incrémental `after` sans perte (marqueur avancé seulement une
      fois la borne atteinte, reprise par curseur, resync au-delà)
- [x] Profondeur d'historique configurable (4-25) + rafraîchissement
      incrémental de la conversation ouverte
- [x] Revue multi-agents (24 constats, 10 confirmés) — tous corrigés :
      perte de messages en rafale, envoi au mauvais destinataire après
      changement de conversation, liste effacée par une calibration en
      échec, réponse périmée à la réouverture, course portail/tâche
      réseau sur `gConfig`, lecture 64 bits non atomique du marqueur,
      file pleine silencieuse, budget mémoire des réponses, guid tronqué
      en NVS, échappement HTML du portail
- [x] Revue de régression (Sonnet) — 9 constats, tous corrigés : score
      gonflé indéfiniment par le message-frontière du marqueur (critique),
      réactions comptées dans le score contre la décision actée, curseur de
      reprise bloqué sur une page entièrement filtrée, corps tronqué
      renvoyé comme succès, clé de fusion téléphone à 10 chiffres empêchant
      `+33…` et `06…` de fusionner, seuil d'épinglage non déterministe

## v0.3 — Langage visuel (2026-08-15) — flashé

- [x] Design system « encre et bulle » publié sur Claude Design
      (`design/`, régénérable par `python3 design/build.py`) — voir docs/05
- [x] Bulles de conversation : alignement gauche/droite, ergot 3 px,
      séparateurs temporels centrés, groupement par auteur
- [x] Liste refondue : deux étages, filet bleu de sélection, pastille ambre
- [x] Champ de composition surélevé, cerné de bleu, qui pousse la
      conversation au lieu de la recouvrir
- [x] Plein écran de calibration à progression déterministe ; erreurs en
      bandeau de 13 px, jamais en écran vide
- [x] Point d'état réseau (3 px) dans la barre supérieure
- [x] Portail web refondu : quatre groupes, thème clair/sombre, 5 Ko
- [x] Famille sonore : cinq signaux synthétisés (barres frappées,
      pentatonique de ré), 29 Ko, volume et quatre interrupteurs
      indépendants dans le portail — voir docs/05
- [ ] Valider à l'usage sur l'appareil (lisibilité en plein soleil,
      confort de défilement, justesse et niveau des sons)

## v0.4 — Doctrine mémoire et parsing en flux (2026-08-15) — flashé

- [x] « Conversation vide » diagnostiqué : gardes mémoire rejetant les
      chargements de conversation (bloc contigu ~31 Ko), erreur masquée par
      le statut global effacé par un poll réussi
- [x] Parsing ArduinoJson **en flux** depuis la socket TLS (BoundedStream) :
      plus aucun corps de réponse bufferisé — la limite de bloc contigu ne
      s'applique plus qu'au résultat filtré
- [x] Keep-alive TLS réellement fonctionnel pour la première fois
      (HTTPClient persistants — l'objet local coupait la connexion à chaque
      requête depuis v0.1)
- [x] Statuts liste / conversation séparés ; « Memoire » reconnu comme
      erreur ; sur-lecture anti-tapbacks (une conversation dont les
      dernières lignes sont des réactions ne s'affiche plus vide)
- [x] Revue Sonnet du flux (11 constats, 2 sans objet) — tous traités,
      dont la perte silencieuse de messages en rafraîchissement incrémental
- [x] **docs/06 — doctrine mémoire et vitesse** ratifiée par le PO :
      vitesse > exhaustivité, budgets UX chiffrés, huit règles fermes

## v0.5 — Audit de contrat contre les sources BlueBubbles (2026-08-16) — flashé

Passe profonde Sonnet : 5 chercheurs croisant le firmware avec le code du
serveur 1.9.9 et de l'app officielle, 10 vérifications adversariales.
15 constats, 2 réfutés preuves à l'appui, 13 corrigés :

- [x] **`with=attachment` manquant** : `[pièce jointe]` ne pouvait
      littéralement jamais s'afficher (critique)
- [x] **Messages retirés (unsend)** → « [message retiré] » ; **édités** →
      suffixe « (modifié) », mise à jour en place + rechargement complet
      périodique de la conversation ouverte (les éditions ne changent pas
      dateCreated : l'incrémental seul ne les revoit jamais)
- [x] **Stickers** : porteurs d'`associatedMessageGuid` mais pas des
      tapbacks — ils disparaissaient de l'affichage ; le filtre teste
      désormais `associatedMessageType`
- [x] **Timeout d'envoi** 15 s → 60 s (le serveur attend la confirmation
      Apple) ; un timeout n'est plus traité en échec : « Envoi non
      confirmé », brouillon non restauré → plus de double envoi possible
- [x] **Fusion des numéros** : le suffixe aveugle à 9 chiffres pouvait
      fusionner deux correspondants réels (+1 212… / +1 812…) — envoi au
      mauvais destinataire possible ; normalisation limitée aux préfixes
      explicites français (+33/0), forme complète sinon
- [x] **Curseur `before` + 1 ms** : symétrique de `after = marqueur − 1`
      (le serveur tronque les dates à la ms — perte en frontière de page)
- [x] **Rafale > page en incrémental** → rechargement complet automatique
- [x] **Groupes** : nom de l'expéditeur affiché au changement de voix
- [x] Chat supprimé côté Mac : « Conversation supprimée » en français,
      vue vidée
- [ ] Reporté (décision PO) : titre des groupes sans nom (chatIdentifier
      opaque — il faudrait `chats.participants` en calibration)

## v0.6 — Session de nuit (2026-08-16) — compilé + testé natif, À FLASHER

Voir docs/07_RAPPORT_NUIT.md. « JSON : EmptyInput » corrigé (enveloppe
identity-sans-longueur + lectures courtes TLS), décodeurs extraits dans
`bb_streams.h` avec **13 tests natifs** (`pio test -e test-native`),
budgets en échéances absolues, envoi tri-état (fin du risque de double
envoi), endpoint /debug/conv, STORE_VER 3 (recalibration auto).

## v0.7 — Émojis (2026-08-16) — flashé

- [x] 42 glyphes pixel 12×12 pour 68 codepoints (visages, cœurs colorés,
      mains, feu/étoile/fête…), même système que les symboles de Geek
      Casino : générateur Python source de vérité
      (`design/tools/gen_emoji.py` → `include/emoji_art.h` + carte de
      prévisualisation), palette indexée RGB565, 6 Ko de flash
- [x] Rendu « texte riche » : mesure et dessin mêlant efont et glyphes dans
      les bulles, aperçus, titres, en-têtes de groupe et composition
- [x] Fiabilité Unicode : sélecteurs de variation, tons de peau et
      séquences ZWJ absorbés ; variantes fusionnées (😀😃😄 → un glyphe) ;
      émoji inconnu → glyphe « ? » — jamais de tofu (`include/bb_emoji.h`,
      logique pure)
- [x] 8 tests natifs de segmentation (21 tests au total, tous verts)
- [x] Carte « Émojis » publiée au design system

## v0.8 — Cause racine des « IncompleteInput » (2026-08-16) — flashé, validé

- [x] Diagnostic sur matériel (sonde d'octets) : `NetworkClientSecure::read()`
      rend `-1` dès que `available() == 0` et `NetworkClient::readBytes()`
      traite ce `-1` en erreur fatale → toute réponse > ~3-4 Ko (tampon TLS
      déchiffré) « finissait » en plein corps. Les petites réponses passaient :
      seuls calibration et chargement de conversation mouraient — d'où la
      liste réduite à la conversation la plus récente
- [x] `NetSource` (bb_client.cpp) : lecture par `available()`/`connected()`
      + attente courte (l'idiome de HTTPClient), échéance absolue, sonde de
      diagnostic intégrée (octets livrés + fin de flux) — seule source des
      décodeurs de `bb_streams.h` ; piège documenté dans docs/02
- [x] Déclencheur distant `/debug/conv?calib=1` (équivalent touche « r »)
- [x] Validé sur l'appareil : conversation de 30 Ko chargée en ~190 ms,
      recalibration complète sans erreur, liste reconstruite (fenêtre
      300 messages / 30 jours), 21 tests natifs verts
- [x] Recalibration fiabilisée (constat PO : « 2 conversations puis les
      autres ») : un double appui « r » mettait deux calibrations en file et
      la première, avortée, remplaçait la liste par une page partielle —
      désormais point d'entrée unique anti-doublon, un balayage écourté
      **fusionne** avec la liste existante au lieu de la remplacer, et un
      **modal de progression** reste affiché pendant toute la recalibration
      (clavier ignoré) ; plein écran conservé pour la synchro initiale —
      validé sur l'appareil (doublon absorbé, interruption sans perte)

## v0.19 — Chaîne de publication (2026-08-16)

Doctrine reprise de Silicon Casino (elle-même de Daoa Mini) : **chaque
image publiée sort du simulateur, jamais d'une maquette**. `captures/`
n'est pas versionné, `docs/images/` et `docs/m5burner/` le sont, et deux
scripts sont l'unique passerelle entre les deux.

```bash
pio run -e sim
.pio/build/sim/program --screens captures/screens
.pio/build/sim/program --frames captures/gif 60
python3 scripts/readme_images.py      # 16 écrans + hero.png (pur Python)
python3 scripts/make_store_images.py  # cover M5Burner, mosaïque 3x2, GIF
```

- [x] Mode `--frames` du simulateur : la boucle produit en 60 images
      déterministes — on tape lettre à lettre, on envoie, la bulle part,
      un cœur s'y pose, la réponse arrive (`docs/images/loop.gif`, 39 Ko)
- [x] `readme_images.py` (pur Python, BMP → PNG par zlib, aucune
      dépendance) : 16 écrans à l'échelle 2 + héros 2×2
- [x] `make_store_images.py` (PIL) : affiche M5Burner 1200×900 avec son
      test de vignette à 170×135, mosaïque 3×2 pour Reddit, GIF quantisé
- [x] Deux défauts vus au montage du héros et corrigés : fragment
      d'étiquette sous la barre (étiquette d'un message défilé), et émojis
      de démonstration hors couverture qui tombaient sur le glyphe « ? »
- [x] README bilingues illustrés : héros, GIF, galerie 3×2

## v0.18 — Alias dans le chrome, et écran À propos (2026-08-16) — flashé

- [x] **Le nom local prime dans la barre de titre** et les étiquettes QUI :
      `openChat` figeait le titre serveur, le numéro restait affiché malgré
      l'alias (constat PO)
- [x] **Troncature du chrome resserrée** (140 px) : un nom long s'arrête sur
      « … » et ne mange jamais batterie ni signal ; dans l'étiquette QUI,
      c'est le NOM qui cède, jamais l'heure (« …DELACROIX 1 » pour 18:38)
- [x] **Écran À propos** (Infos → `a`), même patron que Silicon Casino : nom,
      auteur, crédit d'outil avec l'astre Claude qui bat d'un pixel, puis la
      distinction honnête — adaptée ici, où le réseau EXISTE : « aucune IA ne
      tourne sur l'appareil », « votre Mac, votre appareil, rien entre les
      deux » — et la licence

## v0.17 — Mobilité : WiFi/serveur sur l'appareil, QR, SD, alias (2026-08-16) — flashé

Cinq demandes PO d'un même mouvement — pouvoir vivre avec l'appareil hors
de chez soi :

- [x] **WiFi et serveur modifiables depuis l'appareil** : scan des réseaux
      (asynchrone, tri par force, dédoublonnage, réseaux ouverts marqués),
      éditeur de texte générique (masquage par caractère UTF-8), et le
      chemin sûr du portail — copie, NVS, redémarrage (jamais de String
      écrite dans gConfig à chaud)
- [x] **QR de premier démarrage** (`OK` sur l'écran Setup) : format
      `WIFI:T:WPA;…` — le téléphone rejoint l'AP en scannant, le portail
      captif s'ouvre ensuite tout seul
- [x] **QR « portail sur téléphone »** dans les réglages : `http://<ip>/`
      à l'adresse courante (lib ricmoo/QRCode, version 4, modules 3 px sur
      carte blanche)
- [x] **Sauvegarde/restauration SD** (`/SiliconBubbles/` : config.json —
      mots de passe en clair, dit dans le README —, chats.bin, names.bin,
      marker.txt ; carte montée le temps de l'opération). Restauration
      proposée au premier démarrage (`b`) si une sauvegarde existe
- [x] **Noms de contacts locaux** (`n` sur une conversation) : le serveur
      ne fournit pas les contacts du Mac ; alias en NVS (blob bbnames),
      prioritaire sur le titre serveur, inclus dans la sauvegarde SD
- [x] Réglages : 6 lignes d'action (WiFi, serveur, mot de passe, QR,
      sauvegarde, restauration) ouvertes par `OK`

## v0.16 — Fenêtre de calibration élargie (2026-08-16) — flashé, validé

Constat PO : « je n'ai que 4 conversations, c'est très peu ». La fenêtre de
300 messages / 30 jours datait d'un bug de transport corrigé depuis (piège
readBytes, docs/02) — et le parsing en flux rend la profondeur du balayage
GRATUITE en mémoire : elle ne coûte que du temps de calibration.

- [x] Nouveau critère d'arrêt, dans l'ordre : **liste pleine** (20
      conversations distinctes — ce que l'utilisateur veut vraiment),
      1000 messages, ou 90 jours
- [x] Validé sur l'appareil : 4 → 9-11 conversations après recalibration

## v0.15 — Codes d'erreur utilisateur (2026-08-16) — flashé

- [x] **Table de codes stables E11-E50** (`include/bb_errors.h`, bilingue) :
      un code par cause actionnable — injoignable, timeout, mot de passe
      refusé, 5xx, API introuvable, proxy, réponse aberrante/interrompue/
      trop grosse, mémoire appareil, URL invalide. Le jargon (« JSON »,
      transport, messages serveur) va en console série, jamais à l'écran ;
      les cas évidents (WiFi perdu) restent un message nu sans code
- [x] Table publique documentée dans les **deux README** (cause + geste de
      réparation), source de vérité pointée
- [x] Le bandeau d'état reconnaît les codes E## pour virer au rouge (fin de
      la liste de mots-clés fragile) ; « conversation supprimée » détectée
      par E22 et non plus par le texte « HTTP 404 »

## v0.14 — Direction visuelle « D » (2026-08-16) — flashé

Actée par le PO après maquettes comparatives au simulateur (A matière /
B rétro-communicateur / C terminal / D hybride) : **le chrome d'appareil de
B, la matière de bulles de A** — l'appareil parle ambre, les gens parlent
bleu (docs/05, règle 2 révisée).

- [x] Barres biseautées, LED d'état, titre en capitales, %, signal et pile
      dessinés ; barre d'aide en capitales bitmap + compteur NOUVEAU ambre
- [x] Étiquettes machine « QUI HH:MM » alignées côté locuteur — remplacent
      séparateurs horaires ET en-têtes d'expéditeur de groupe
- [x] Bulles en matière : ombre portée, lueur haute, rayon 6 ; pilule de
      réactions ombrée ; progressions en ambre (splash, calibration, modal)
- [x] Écrans Infos et Réglages en libellés machine ; tout décliné sur les
      10 écrans, vérifié au simulateur avant flash

## v0.13 — Modularisation et simulateur (2026-08-16) — flashé

- [x] **Splash screen** : logo bulle « en train d'écrire » (points animés),
      étapes WiFi › serveur › synchro déduites du modèle, barre de
      progression de la première calibration, version — il vit du démarrage
      à la première liste (chemin rapide : liste NVS immédiate, sans splash)
- [x] **Barres de signal WiFi** type téléphone (0-4 selon RSSI) dans la
      barre supérieure, échelle en creux toujours lisible
- [x] Trois **esquisses de direction visuelle** rendues par le vrai moteur
      (`--mocks`) : matière et profondeur / rétro-communicateur / terminal
      phosphore — décision PO en cours

- [x] **Fin du monolithe** : le rendu sort de main.cpp vers `src/ui/`
      (display.h / theme.h / model.h / render.cpp). Le rendu ne lit qu'un
      `UiModel` rempli sous verrou — plus aucun accès matériel, réseau ou NVS
      dans le dessin. main.cpp : 1794 → ~1100 lignes (état, réseau, NVS,
      clavier)
- [x] **Simulateur SDL macOS** (`pio run -e sim`, patron Geek Casino) :
      LovyanGFX + Panel_sdl, rendu au pixel identique (même API lgfx, même
      efont), fenêtre interactive ×3 ou captures BMP sans fenêtre
      (`--screens <dir> [--lang fr]`). Shim Arduino hôte minimal
      (`src/sim_compat/Arduino.h`)
- [x] **Jeu de données de démonstration** (contacts inventés, FR/EN,
      émojis + tapbacks) : les captures ne peuvent pas contenir de donnée
      personnelle — pas de floutage à faire, jamais
- [x] Premiers fruits : trois défauts vus et corrigés à la première capture
      (double « moi : », barre d'aide FR tronquée, écran Setup non traduit)
- [x] Le firmware compile inchangé, 34 tests natifs verts, flashé et validé

## v0.12 — Bilingue et réglages sur l'appareil (2026-08-16) — flashé

- [x] **Interface bilingue EN/FR, anglais par défaut** (docs/01) : table
      plate en flash `include/i18n.h` (`T(S_…)`, ~60 chaînes), portail web
      traduit par `P("en", "fr")`, `<html lang>` suivi. Le bandeau d'erreur
      reconnaît désormais les mots-clés des DEUX langues (sinon une erreur
      anglaise s'affichait en gris au lieu de rouge)
- [x] **Écran Réglages sur le Cardputer** (Infos → `s`) : langue, volume,
      4 interrupteurs de son, rythme, profondeur d'historique. `;/.` change
      de champ, `,` et `/` la valeur, `` ` `` enregistre et sort (une seule
      écriture NVS par visite). Les sons s'appliquent à chaud
      (`Snd::applyConfig()`, qui ne réalloue jamais un tampon déjà rendu)
- [x] **Périmètre volontairement restreint aux champs POD** : WiFi, URL, mot
      de passe et TLS restent au portail — la tâche réseau lit `gConfig` en
      permanence et réaffecter une `String` depuis l'autre cœur la ferait
      planter
- [x] **Garde anti-conflit portail ↔ appareil** : `AppConfig::rev` incrémentée
      à chaque enregistrement ; le formulaire embarque sa révision et le
      portail refuse en **HTTP 409** (page dédiée bilingue) si un réglage a
      changé sur l'appareil entre-temps — plus d'écrasement silencieux.
      Vérifié sur l'appareil : POST à révision périmée → 409, config intacte

## v0.11 — Aperçus figés et espaces insécables (2026-08-16) — flashé

- [x] **Aperçu de la liste figé à la dernière calibration** (constat PO) :
      `sListChanged` n'était posé QUE lorsqu'une conversation entrait dans la
      liste — jamais quand l'aperçu d'une entrée existante changeait. Le blob
      NVS gardait donc les aperçus de la calibration, pendant que
      `storeSaveMarker()` persistait le marqueur toutes les 5 min : au
      redémarrage, le marqueur était à jour, le poll ne rapportait donc rien,
      et les aperçus restaient figés **définitivement**. Toute mise à jour
      d'aperçu marque désormais le blob à repersister
- [x] **Carré à la place de l'espace avant « ? »** : iMessage insère les
      espaces insécables typographiques françaises (U+00A0, fine U+202F) que
      la police efont ne possède pas — elles s'affichaient en tofu et
      faussaient la mesure de largeur. Tout texte venant du serveur est
      normalisé à l'entrée (`bbNormalizeSpaces`, bb_emoji.h) : espaces
      exotiques → espace ASCII, largeurs nulles (U+200B, BOM) supprimées
- [x] 4 tests natifs de normalisation (34 au total, tous verts)

## v0.10 — Tapbacks, affichage (2026-08-16) — flashé

- [x] Les réactions ne sont plus jetées par `fetchMessages` : collectées
      (guid propre, cible sans préfixe de partie « p:N/ », type, retrait
      « - ») et appliquées en compteurs par type sur le message cible
      (`include/bb_tapback.h`, logique pure, 3 tests natifs — 30 au total)
- [x] Badge en pilule à cheval sur le coin haut de la bulle, décalé vers le
      centre (comme iMessage) : jusqu'à 3 types en glyphes ❤ 👍 👎 😂 ou en
      texte (!! ?), « xN » si le total dépasse l'affiché ; la bulle réserve
      10 px au-dessus d'elle
- [x] Déduplication entre polls (le curseur incrémental ne dépasse jamais la
      dernière bulle : une réaction plus récente est re-servie à chaque
      rafraîchissement) ; retraits appliqués en ordre chronologique
- [x] Vérifié sur données réelles : 2 réactions parsées dans la première
      conversation du serveur de test (`/debug/conv` expose `taps`)
- [ ] Envoi de tapbacks (Private API) — non demandé, reporté

## v0.9 — Défilement des conversations (2026-08-16) — flashé

Constat PO : « le scroll ne marche que partiellement, la bulle la plus haute
souvent coupée ou inaccessible ». Deux bugs de fond derrière le symptôme :

- [x] **Sens du défilement inversé** : le contenu, ancré en bas, était poussé
      vers le HAUT — on ne révélait jamais un message plus ancien, on vidait
      l'écran par le haut en laissant une bande blanche en bas (la branche
      « bloc sous la zone » du rendu était du code mort, ce qui le confirme)
- [x] **Pas en lignes de 13 px** sur des blocs qui n'en sont pas des
      multiples (marges 3 px, écarts 3/7 px, séparateurs 12 px) ; la borne
      arrondie vers le bas rendait les derniers pixels du premier message
      **définitivement** inaccessibles
- [x] Nouveau modèle : **arrêts de bulle** (`include/bb_scroll.h`, logique
      pure) — chaque arrêt aligne le haut d'une bulle sur le haut de la zone,
      emporte son en-tête (expéditeur, séparateur horaire), et une bulle plus
      haute que l'écran reçoit des arrêts intermédiaires
- [x] Un message qui arrive ne ramène plus brutalement en bas pendant la
      lecture de l'historique : la position est conservée (index décalé du
      nombre de bulles ajoutées)
- [x] **6 tests natifs** de défilement (27 au total, tous verts)

## À venir — Confort

- [x] Polling dans une tâche FreeRTOS (UI jamais bloquée, polling possible
      pendant la composition) — fait dès v0.1 après constat de lenteur
- [x] Keep-alive HTTP (une poignée de main TLS, connexion réutilisée)
- [ ] Pagination arrière (charger plus de 20 messages en scrollant)
- [ ] Marqueurs de date entre messages, meilleur affichage des groupes
- [ ] Écran de veille / extinction écran sur inactivité (batterie)
- [ ] Résolution des numéros en noms via `?with=participants` + contacts API

## À venir — Au-delà du texte

- [ ] Vignettes JPEG/PNG des pièces jointes (download + decode, RAM à
      surveiller)
- [x] Tapbacks : affichage fait (v0.10) ; envoi via Private API à décider
- [ ] Notifications même hors écran CHATS (poll léger global)
- [ ] Multi-serveur ?

## Explicitement hors périmètre (sauf décision PO)

Socket.IO, webhooks, FCM, HEIC/vidéo, comptes multiples, OTA.
