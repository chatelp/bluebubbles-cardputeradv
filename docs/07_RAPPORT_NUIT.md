# Rapport de la session de nuit — 2026-08-16

Travail autonome (sous-agents Sonnet uniquement). **Le Cardputer était
débranché et éteint** : rien n'a pu être flashé ni testé sur matériel.
Tout ce qui suit est compilé, testé en natif (13/13), et **prêt à flasher
au réveil** — voir « Au réveil » en fin de rapport.

## Le bug « JSON : EmptyInput » — cause trouvée et corrigée

Deux défauts réels dans ma couche de flux, tous deux prouvés par les tests :

1. **Enveloppe identity-sans-longueur non gérée.** Quand une réponse arrive
   sans `Content-Length` ni chunked (corps délimité par la fermeture de
   connexion), le décodeur chunked lisait `{"status"…` comme une taille de
   chunk hexadécimale → « document vide ». Correctif : discrimination par
   le **premier octet** du corps (un `{`/`[` = identity, un chiffre hexa =
   chunked), sans le consommer.
2. **Lecture courte traitée en fin de flux.** Un flux TCP/TLS peut
   légalement rendre moins d'octets que demandé (frontière de record TLS) ;
   le décodeur déclarait la fin du flux en plein CRLF inter-chunks →
   `IncompleteInput`. C'est le **test natif qui l'a attrapé** avant tout
   flash. Correctif : `readFully` (seule une lecture nulle est une fin).

## Tests natifs — nouveau filet de sécurité

Les décodeurs de flux et la clé de fusion sont extraits dans
`include/bb_streams.h`, **le même code** compilé pour le firmware et pour
le Mac. `pio test -e test-native` : **13 tests, 13 verts** — parse borné,
troncatures, chunked multi-tailles × fragmentations, extensions de chunk,
corps identity refusé par le décodeur chunked (le bug d'hier, reproduit),
flux sale, serveur qui goutte coupé par échéance, et la clé de fusion
(formes françaises qui fusionnent, numéros NANP qui ne fusionnent jamais,
numéros courts, e-mails, groupes).

## Revue Sonnet de nuit — 8 constats, tout traité

- **Critique — risque de double envoi restant** : mon attente du premier
  octet était codée à 8 s alors qu'un envoi peut légitimement rester 60 s
  sans réponse (confirmation Apple) ; l'échec fabriqué (« EmptyInput »)
  contournait la garde anti-double-envoi. Correctif structurel : les
  budgets suivent le timeout de la requête, et l'envoi renvoie un
  **tri-état** (`SEND_OK` / `SEND_FAILED` certain / `SEND_UNCONFIRMED`)
  fondé sur l'origine réelle de l'erreur (code HTTP vs transport), plus
  aucune analyse de sous-chaînes. Le brouillon n'est restauré que sur
  échec **certain**.
- **Majeur — timeouts d'inactivité ≠ budgets** : `readBytes()` du core
  réarme son échéance à chaque octet ; un serveur qui goutte pouvait
  retenir la tâche réseau indéfiniment. Correctif : **échéances horloge
  absolues** dans les décodeurs (constructeur + drain), testées avec une
  source qui goutte.
- Mineurs corrigés : bande de 3 px non masquée sous la barre haute
  (fragments de glyphes), compteur de rechargement complet non remis à
  zéro au changement de conversation. Vérifiés sûrs par la revue : pas de
  deadlock DataLock/netEnqueue, pas de boucle sur le rechargement complet.

## Endpoint de diagnostic (nouveau)

`GET /debug/conv?run=1&i=N` déclenche le chargement de la conversation N
par la tâche réseau ; `GET /debug/conv` en lit le résultat — **ok, nombre
de messages, durée, erreur uniquement, jamais de contenu**. Il me permet de
tester l'ouverture de conversation sans intervention. Coût : ~23 s de file
réseau au pire pendant un test (diagnostic occasionnel, assumé).

## Divers

- `STORE_VER` 2 → 3 : le format des clés de fusion a changé (correctif des
  collisions de numéros étrangers d'hier) — une recalibration propre se
  déclenchera automatiquement au premier démarrage.
- Version : 0.3.0. RAM statique 17,1 %, flash 20 %.
- Reste ouvert (décision PO, docs/03) : titre des groupes sans nom.

## Au réveil

1. Brancher le Cardputer (USB-C).
2. `pio run -e cardputer-adv -t upload`
3. L'appareil recalibrera (~25 s), puis : ouvrir la conversation qui
   affichait « JSON : EmptyInput » — elle doit se charger ; vérifier les
   aperçus `[pièce jointe]`, les noms d'expéditeur dans un groupe, et un
   envoi.
4. En cas de souci : `curl http://cardputer.local/debug/conv?run=1` puis
   `curl http://cardputer.local/debug/conv` — et la console série donne le
   diagnostic exact (`[bb] json …`).
