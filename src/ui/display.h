// Frontière d'affichage : tout le rendu passe par l'API lgfx::, jamais par le
// matériel. M5GFX côté appareil, LovyanGFX (SDL) côté simulateur — M5GFX étant
// un dérivé de LovyanGFX, l'API de dessin est identique et le rendu l'est au
// pixel près (même police efont, mêmes primitives).
#pragma once

#if defined(SIM_BUILD)
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
using BbCanvas = lgfx::LGFX_Sprite;
// (le namespace global `fonts` est déjà fourni par lgfx_fonts.hpp)
#else
#include <M5Cardputer.h>
using BbCanvas = M5Canvas;
#endif
