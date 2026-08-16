// Palette « encre et bulle » et géométrie — voir le design system
// (design/foundations) et docs/05. Valeurs RGB565 : exactement les teintes
// que l'écran affiche. Jamais de littéral de couleur inline dans le rendu.
#pragma once
#include <stdint.h>

static const uint16_t C_INK900   = 0x0883;  // fond
static const uint16_t C_INK800   = 0x10C5;  // barres
static const uint16_t C_INK700   = 0x1928;  // bulle reçue, surface
static const uint16_t C_INK600   = 0x21A9;  // ligne sélectionnée
static const uint16_t C_SLATE300 = 0x8D17;  // texte secondaire
static const uint16_t C_SLATE200 = 0xBE3B;  // aperçu
static const uint16_t C_WHITE    = 0xFFFF;
static const uint16_t C_BLUE500  = 0x2BFD;  // bulle envoyée
static const uint16_t C_BLUE400  = 0x4D1F;  // accent
static const uint16_t C_GREEN400 = 0x3631;
static const uint16_t C_AMBER400 = 0xFD84;  // pastille non lu
static const uint16_t C_RED400   = 0xFACB;  // erreur

static const int SCREEN_W = 240;
static const int SCREEN_H = 135;
static const int BAR_H  = 16;      // barre supérieure
static const int HINT_H = 13;      // barre d'aide

static const int BUB_MAXW  = 176;  // 73 % de l'écran
static const int BUB_PADX  = 4;
static const int BUB_PADY  = 3;
static const int BUB_R     = 5;
static const int BUB_TAIL  = 3;
static const int LINE_H    = 13;
static const int GAP_SAME  = 3;    // même auteur
static const int GAP_TURN  = 7;    // changement d'auteur
static const int TAP_PAD   = 10;   // réserve au-dessus d'une bulle à réactions
static const int TAP_H     = 16;   // hauteur du badge (chevauche la bulle de 6 px)
static const int EDGE      = 6;    // marge d'écran

static const int EMOJI_ADV = 13;   // 12 px de glyphe + 1 d'espacement
