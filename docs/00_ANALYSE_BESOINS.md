# Analyse de faisabilité — client BlueBubbles sur Cardputer ADV

Étude menée le 2026-08-14, avant toute ligne de code.

## Question posée

Peut-on, techniquement et légalement, implémenter un client léger pour le
serveur [BlueBubbles](https://github.com/BlueBubblesApp) sur un M5Stack
Cardputer ADV ?

## Licence : aucun obstacle

- Le [serveur](https://github.com/BlueBubblesApp/bluebubbles-server) et
  l'[app cliente officielle](https://github.com/BlueBubblesApp/bluebubbles-app)
  sont sous **Apache-2.0** (permissive, sans copyleft).
- Écrire un client tiers qui consomme l'API est libre ; l'API REST est
  [documentée publiquement](https://docs.bluebubbles.app/server/developer-guides/rest-api-and-webhooks)
  précisément pour les intégrations tierces.
- Réutiliser du code du projet serait possible (en conservant notices et
  mention Apache-2.0), mais l'app officielle est en Dart/Flutter : rien de
  réutilisable en embarqué. Ce dépôt est du code original → licence MIT,
  comme les autres projets Cardputer de Pierre.
- Le risque vis-à-vis des CGU d'Apple est porté par l'opérateur du serveur
  (le Mac), pas par le client.

## Serveur : API adaptée à un client contraint

- Auth minimale : `?password=` en query string.
- REST JSON : conversations, messages paginés, envoi, pièces jointes.
- Temps réel : Socket.IO (client officiel), webhooks (le serveur POSTe), ou
  polling REST. Voir docs/01 pour le choix du polling.

## Matériel : suffisant, de justesse

| Élément | Donnée | Conséquence |
|---|---|---|
| ESP32-S3FN8 | 8 Mo flash, **pas de PSRAM**, ~512 Ko SRAM (~300 Ko de heap utiles) | 1 seule session TLS (~45 Ko), pagination, parsing filtré |
| Écran ST7789V2 | 240 × 135 | sprite plein écran 16 bits = 64 Ko, OK |
| Clavier 56 touches | TCA8418 (I²C), géré par `M5Cardputer` ≥ 1.1.1 | même API que le Cardputer original |
| WiFi 2,4 GHz | ~130 mA actif | modem-sleep entre les polls |
| Batterie | 1 750 mAh | ordre de grandeur 8–12 h en polling |

## Limites acceptées en v1

- Pas de push (FCM inaccessible) : le polling fait foi.
- Texte seulement ; pièces jointes signalées par un placeholder.
- Pas de HEIC/vidéo envisageable à terme ; vignettes JPEG/PNG possibles
  mais reportées (RAM).
- Accès distant : HTTPS via Cloudflare/ngrok → racines CA embarquées ;
  en LAN, HTTP simple accepté.

## Verdict

Faisable sans friction de licence. MVP : liste des conversations, lecture,
envoi de texte, notification sonore, configuration par portail web.
