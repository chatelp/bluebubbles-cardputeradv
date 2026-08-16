#pragma once
#include <Arduino.h>

// Famille sonore de l'appareil. Pas de bips : chaque signal est un petit
// timbre de barre frappée (partiels 1 : 4 : 10, enveloppe exponentielle),
// synthétisé en PCM au démarrage puis joué par le codec ES8311.
// Tous les sons partagent la même gamme pentatonique de ré majeur : ils
// s'entendent comme une famille, jamais comme des bruits disparates.
namespace Snd {

enum Event : uint8_t {
    KEY,       // frappe clavier — très court, hauteur variée à chaque touche
    SENT,      // message parti — deux notes montantes + souffle ascendant
    RECEIVED,  // message reçu dans la conversation ouverte — deux notes qui se posent
    NOTIF,     // message reçu ailleurs — arpège de trois notes, plus présent
    ERROR,     // échec — deux notes descendantes, légèrement désaccordées
    COUNT
};

// Alloue et rend les tampons des catégories activées dans la configuration.
// À appeler après M5Cardputer.begin() et après AppConfig::load().
void begin();

// Réapplique la configuration à chaud (écran Réglages) : volume, catégories,
// et rendu des tampons nouvellement activés — sans réallouer ceux déjà rendus.
void applyConfig();

// Joue un signal. Sans effet si sa catégorie est désactivée, si le volume
// est nul, ou si le tampon n'a pas pu être alloué. Ne bloque jamais.
void play(Event e);

// Mémoire réellement occupée par les tampons PCM (diagnostic).
size_t bytesUsed();

}  // namespace Snd
