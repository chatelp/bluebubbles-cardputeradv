// Rendu complet d'une frame dans le canvas, à partir du seul modèle.
// Pas de verrou ici : l'appelant verrouille (le firmware) ou est
// mono-thread (le simulateur). Peut écrire dans le modèle : bornage du
// défilement (msgScroll/msgStops) et de la sélection.
#pragma once
#include "display.h"
#include "model.h"

void uiRender(BbCanvas& canvas, UiModel& m);
