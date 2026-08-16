# Langage visuel — « encre et bulle »

Design system publié sur Claude Design :
**BlueBubbles Cardputer — Design System**. Les sources sont dans `design/`,
régénérables par `python3 design/build.py`, et poussées par l'outil
DesignSync. Les maquettes sont dessinées à l'échelle réelle (240 × 135) :
ce que montre la carte est ce que l'écran affiche.

## Principe

Un écran de 4 cm ne supporte pas deux systèmes de signes concurrents.
D'où trois règles fermes :

1. **L'alignement porte le sens.** À gauche l'autre, à droite soi. La
   couleur confirme, elle ne répète pas.
2. **Un seul accent coloré à la fois.** Ambre (non lu), rouge (erreur),
   vert (confirmation) ne coexistent jamais : deux signaux simultanés n'en
   font plus aucun.
3. **Les trois bandes sont fixes** d'un écran à l'autre — barre supérieure
   16 px, contenu 106 px, barre d'aide 13 px : l'utilisateur sait toujours
   où regarder.

## Palette (RGB565, valeurs affichées après quantification)

| Jeton | Valeur | Rôle |
|---|---|---|
| `C_INK900` | `0x0883` | Fond |
| `C_INK800` | `0x10C5` | Barres haute et basse |
| `C_INK700` | `0x1928` | Bulle reçue, surface |
| `C_INK600` | `0x21A9` | Ligne sélectionnée |
| `C_SLATE300` | `0x8D17` | Texte secondaire, horodatage |
| `C_SLATE200` | `0xBE3B` | Aperçu de conversation |
| `C_WHITE` | `0xFFFF` | Texte des messages |
| `C_BLUE500` | `0x2BFD` | **Bulle envoyée** — n'appartient qu'à elle |
| `C_BLUE400` | `0x4D1F` | Accent : titres, sélection, focus |
| `C_GREEN400` | `0x3631` | Serveur OK |
| `C_AMBER400` | `0xFD84` | Pastille non lu, rattrapage |
| `C_RED400` | `0xFACB` | Erreur, batterie faible |

Les couleurs traversent le code en `constexpr uint16_t` nommées, jamais de
littéral inline (un `uint32` nu serait interprété RGB888).

## Typographie

`efontJA_12` pour le corps, `efontJA_10` pour les métadonnées. Les fontes
ASCII de M5GFX n'ont pas les accents français : la famille efont est la
seule praticable. **Métriques qui gouvernent tout le découpage de texte :**
un caractère ASCII fait **6 px**, l'interligne est de **13 px** — soit
**7 lignes** de conversation et **28 caractères** par ligne de bulle.

## Géométrie des bulles

| Élément | Valeur | Raison |
|---|---|---|
| Largeur max | 176 px (73 %) | Laisse voir le bord opposé : on lit l'alternance sans lire le texte |
| Rayon | 5 px | Au-delà, la bulle mange ses propres lettres |
| Marge interne | 4 × 3 px | Minimum pour que le texte ne touche pas le bord |
| Ergot | 3 px | Le seul détail qui fait « messagerie » plutôt que « liste » |
| Écart même auteur | 3 px | Les messages d'un même tour se touchent presque |
| Écart entre auteurs | 7 px | Le silence entre deux personnes |
| Marge d'écran | 6 px | Bord gauche des reçues, bord droit des envoyées |

**Défilement de bulle en bulle**, jamais au pixel ni à la ligne : chaque
arrêt aligne le haut d'une bulle sur le haut de la zone et emporte son
en-tête (nom d'expéditeur, séparateur horaire) — on ne montre jamais une
bulle sans son contexte. Une bulle plus haute que l'écran reçoit des arrêts
intermédiaires d'une hauteur de zone : aucun contenu n'est jamais
inatteignable. Le contenu est ancré en bas ; défiler le pousse vers le bas
pour découvrir l'historique. Calcul en logique pure (`include/bb_scroll.h`),
couvert par 6 tests natifs.

**Réactions (tapbacks)** : une pilule de 16 px à cheval sur le coin haut de
la bulle, décalée vers le centre de l'écran — la géométrie d'iMessage. La
bulle réserve 10 px au-dessus d'elle (le badge la chevauche de 6 px).
Jusqu'à trois types affichés : ❤ 👍 👎 😂 en glyphes pixel, « !! » et « ? »
en texte ; un « xN » gris clôt la pilule quand le total dépasse l'affiché.
Fond `C_INK800` cerné de `C_INK600` : lisible sur le fond comme sur les deux
couleurs de bulle, sans voler l'accent bleu.

**Pas d'horodatage par bulle** : à 6 px par caractère, « 18:44 » coûterait
30 px de largeur utile à chaque message. Le temps s'affiche en séparateur
centré entre deux groupes, uniquement au-delà d'un quart d'heure de
silence — la solution d'iMessage, et la seule qui tienne ici. **Pas
d'avatar** non plus : 20 px volés pour une initiale que l'alignement dit
déjà.

## États

Un écran d'état ne remplace jamais du contenu déjà chargé. La calibration
initiale est le **seul plein écran** — c'est le seul moment où il n'y a
effectivement rien à montrer, et sa progression est honnête (« page 9 / 34 »,
le nombre de pages étant connu d'avance). Une **recalibration** (liste déjà
affichée) montre un **modal centré** (184 × 56 px, bord bleu, même barre de
progression) qui reste affiché pendant tout le balayage, clavier ignoré :
demandé par le PO le 2026-08-16 après qu'une calibration interrompue par un
double appui avait commis une liste partielle. Toutes les erreurs
ultérieures sont des **bandeaux de 13 px** qui recouvrent la barre d'aide :
la liste en cache reste lisible pendant que l'appareil se rattrape.

Le **point d'état** de la barre supérieure (3 px) résume le réseau : bleu
synchronisé, ambre rattrapage en cours, rouge hors ligne.

## Famille sonore

Le Cardputer ADV a un codec **ES8311**, un ampli NS4150B et un haut-parleur
1 W : de quoi faire autre chose que du buzzer. Les cinq signaux sont
synthétisés en PCM au démarrage (`src/sound.cpp`), 11 025 Hz,
**29 Ko mesurés** sur l'appareil.

Chaque note est une **barre frappée** : trois partiels dans le rapport
1 : 4 : 10 (l'accord du marimba), les aigus s'éteignant 2 à 4 fois plus vite
que le fondamental. C'est cette décroissance différenciée qui fait entendre
du bois plutôt qu'un circuit. Tout est bâti sur la **pentatonique de ré
majeur** : aucune paire de notes ne peut sonner faux.

| Signal | Direction | Composition |
|---|---|---|
| Touche clavier | — | Bruit filtré 2,2 ms + basse 200 Hz, 16 ms, hauteur variée ±4 % |
| Message envoyé | Montant | Quarte A4 → D5 + glissando 700 → 1500 Hz, 260 ms |
| Message reçu | Descendant | Quarte D5 → A4 + sub 220 Hz, 320 ms |
| Notification | Montant, résolu | Arpège A4, D5, F#5 + sub D4, 420 ms |
| Échec d'envoi | Descendant | A4 → F4 doublé désaccordé de 5 Hz (battement), 300 ms |

**Contraintes tenues** : `playRaw` conserve un pointeur sans copier, donc
les tampons sont rendus une fois au démarrage et jamais libérés ; le clic
clavier varie de ±4 % en hauteur (sinon effet mitraillette à la frappe
rapide) ; le clavier joue sur le canal 0 et les événements sur le canal 1,
pour qu'une frappe ne coupe jamais une notification ; une catégorie dont
l'allocation échoue se désactive au lieu de faire planter.

Volume (0–100) et **quatre interrupteurs indépendants** (clavier, envoi,
réception, notifications) dans le portail. Les sons ne sont rendus que pour
les catégories activées : tout couper ne coûte pas un octet.

La carte « Famille sonore » du design system rejoue ces formules en
WebAudio — on peut donc écouter les signaux dans un navigateur, à
l'identique.

## Émojis

La police efont n'a aucun émoji. Les 42 glyphes (68 codepoints couverts)
sont des dessins pixel 12 × 12 — la hauteur de ligne du texte — générés par
`design/tools/gen_emoji.py`, **même système que les symboles de Geek
Casino** : le script Python est la source de vérité, il émet l'en-tête C++
(palette indexée RGB565, 0 = transparent, 6 Ko de flash) et la carte de
prévisualisation du design system. Règles : les variantes fusionnent
(😀😃😄 → un glyphe, tous les cœurs partagent une forme, sept couleurs) ;
les sélecteurs de variation, tons de peau et séquences ZWJ sont absorbés
(une famille se replie sur son premier membre) ; tout émoji sans glyphe
devient le glyphe « ? » — jamais de carré vide. La segmentation
(`include/bb_emoji.h`) est de la logique pure, couverte par 8 tests natifs.

## Portail web

Même langage : nuit bleutée, bleu unique, quatre groupes dans l'ordre du
besoin (Réseau → Serveur → Affichage et rythme → Sécurité). 5 Ko, aucune
ressource externe — il s'affiche en mode point d'accès, sans Internet.
Thème clair automatique via `prefers-color-scheme`. Les valeurs sont
échappées en HTML (une apostrophe dans un mot de passe cassait le
formulaire au rechargement).
