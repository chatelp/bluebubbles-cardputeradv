#pragma once
// Arrêts de défilement d'une conversation — logique pure (sans Arduino),
// testée en natif. Le contenu est ancré en BAS (le message le plus récent) ;
// un arrêt est un décalage en pixels qui pousse le contenu vers le bas pour
// découvrir l'historique.
//
// Règle de conception (docs/05) : on ne défile jamais au pixel ni à la ligne,
// mais de BULLE EN BULLE. Chaque arrêt aligne le haut d'une bulle sur le haut
// de la zone, en emportant son en-tête (nom d'expéditeur, séparateur horaire)
// pour ne jamais montrer une bulle sans son contexte.
#include <stddef.h>

// Un bloc de mise en page. `h` inclut l'écart ajouté SOUS le bloc.
// `header` : séparateur horaire ou nom d'expéditeur — jamais un arrêt à soi
// seul, il est fusionné avec la bulle qu'il annonce.
struct BbBlockInfo {
    int h;
    bool header;
};

// Remplit `stops` (croissants, stops[0] == 0) et renvoie leur nombre (>= 1).
// `blocks` est dans l'ordre d'affichage : du plus ancien au plus récent.
inline int bbScrollStops(const BbBlockInfo* blocks, int n, int areaH,
                         int* stops, int cap) {
    if (cap < 1) return 0;
    stops[0] = 0;  // le bas : le message le plus récent
    int count = 1;
    if (n <= 0 || areaH <= 0) return count;

    int total = 0;
    for (int i = 0; i < n; i++) total += blocks[i].h;
    const int maxScroll = total > areaH ? total - areaH : 0;
    if (!maxScroll) return count;

    int acc = 0;  // distance du bas du contenu au haut du bloc courant
    for (int i = n - 1; i >= 0; i--) {
        acc += blocks[i].h;
        int px = acc - areaH;   // amène le haut de ce bloc sur le haut de zone
        if (px <= 0) continue;  // bloc déjà entièrement visible depuis le bas
        if (px > maxScroll) px = maxScroll;
        if (blocks[i].header && stops[count - 1] > 0) {
            stops[count - 1] = px;  // en-tête collé à sa bulle : arrêt remonté
            continue;
        }
        if (px <= stops[count - 1]) continue;
        // Une bulle plus haute que l'écran ne doit pas créer un saut aveugle :
        // arrêts intermédiaires d'une hauteur de zone.
        while (px - stops[count - 1] > areaH && count < cap)
            stops[count] = stops[count - 1] + areaH, count++;
        if (count < cap) stops[count++] = px;
    }
    // Le haut de la conversation doit TOUJOURS être atteignable, même si la
    // table a débordé : le dernier arrêt vaut la borne.
    if (stops[count - 1] != maxScroll) stops[count - 1] = maxScroll;
    return count;
}
