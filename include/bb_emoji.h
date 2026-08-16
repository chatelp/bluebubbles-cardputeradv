#pragma once
// Segmentation émoji d'un texte UTF-8 — logique pure (sans Arduino), testée
// en natif. Découpe une chaîne en segments : lots de texte pour la police
// efont, ou glyphes pixel (emoji_art.h, généré par design/tools/gen_emoji.py).
// Fiabilité avant tout : sélecteurs de variation, tons de peau et séquences
// ZWJ sont absorbés (une famille 👨‍👩‍👧 se replie sur son premier membre),
// et tout émoji sans glyphe dédié devient le glyphe « ? » — jamais de tofu.
#include <stddef.h>
#include <stdint.h>

#include "emoji_art.h"

// Décode le codepoint à s[*i] et avance *i. Octet invalide → U+FFFD.
static inline uint32_t bbUtf8Next(const char* s, size_t len, size_t* i) {
    uint8_t c = (uint8_t)s[*i];
    if (c < 0x80) { (*i)++; return c; }
    int n = (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 1;
    if (n == 1 || *i + (size_t)n > len) { (*i)++; return 0xFFFD; }
    uint32_t cp = c & (uint8_t)(0x7F >> n);  // n=2→0x1F, n=3→0x0F, n=4→0x07
    for (int k = 1; k < n; k++) {
        uint8_t cc = (uint8_t)s[*i + k];
        if ((cc & 0xC0) != 0x80) { (*i)++; return 0xFFFD; }
        cp = (cp << 6) | (cc & 0x3F);
    }
    *i += (size_t)n;
    return cp;
}

// Codepoints sans largeur : sélecteurs de variation, tons de peau, joiners.
static inline bool bbCpInvisible(uint32_t cp) {
    return cp == 0xFE0E || cp == 0xFE0F || cp == 0x200D || cp == 0x20E3 ||
           (cp >= 0x1F3FB && cp <= 0x1F3FF);
}

// Plages « ceci est un émoji », même sans glyphe dédié (→ glyphe inconnu).
static inline bool bbCpEmojiRange(uint32_t cp) {
    return (cp >= 0x1F000 && cp <= 0x1FAFF) || (cp >= 0x2600 && cp <= 0x27BF) ||
           (cp >= 0x2B00 && cp <= 0x2BFF) || (cp >= 0x1FB00 && cp <= 0x1FBFF);
}

// Index de glyphe pour un codepoint, -1 si absent (table triée, dichotomie).
static inline int bbEmojiGlyph(uint32_t cp) {
    int lo = 0, hi = kEmojiMapCount - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (kEmojiMap[mid].cp == cp) return kEmojiMap[mid].glyph;
        if (kEmojiMap[mid].cp < cp) lo = mid + 1;
        else hi = mid - 1;
    }
    return -1;
}

// Espaces Unicode absentes d'efont : insécable (U+00A0), fine insécable
// (U+202F), cadratins… iMessage les insère automatiquement en français avant
// « ? ! ; : » et autour des guillemets. Sans normalisation, chacune s'affiche
// en carré vide — et fausse la mesure de largeur (2026-08-16).
inline bool bbCpSpaceLike(uint32_t cp) {
    return cp == 0x00A0 || cp == 0x1680 || (cp >= 0x2000 && cp <= 0x200A) ||
           cp == 0x202F || cp == 0x205F || cp == 0x3000;
}

// Codepoints de largeur nulle à supprimer purement (espace sans chasse, BOM).
inline bool bbCpZeroWidth(uint32_t cp) {
    return cp == 0x200B || cp == 0x2060 || cp == 0xFEFF;
}

// Normalise en place : espaces exotiques → espace ASCII, largeurs nulles
// supprimées. Ne fait que RÉTRÉCIR (toute séquence remplacée fait ≥ 2 octets),
// donc jamais de débordement. Renvoie la nouvelle longueur ; n'écrit pas de 0
// final (l'appelant s'en charge).
inline size_t bbNormalizeSpaces(char* s, size_t len) {
    size_t r = 0, w = 0;
    while (r < len) {
        size_t start = r;
        uint32_t cp = bbUtf8Next(s, len, &r);
        if (bbCpZeroWidth(cp)) continue;
        if (bbCpSpaceLike(cp)) {
            s[w++] = ' ';
            continue;
        }
        for (size_t k = start; k < r; k++) s[w++] = s[k];
    }
    return w;
}

// Un segment : du texte [start, end) (glyph < 0) ou un glyphe émoji.
typedef struct {
    int glyph;
    size_t start, end;
} BbSeg;

// Itère les segments ; renvoie false en fin de chaîne.
static inline bool bbNextSeg(const char* s, size_t len, size_t* i, BbSeg* out) {
    while (*i < len) {
        size_t segStart = *i;
        uint32_t cp = bbUtf8Next(s, len, i);
        if (bbCpInvisible(cp)) continue;  // invisible isolé : rien à montrer

        int g = bbEmojiGlyph(cp);
        bool emojiish = (g >= 0) || bbCpEmojiRange(cp);
        if (emojiish) {
            // Absorbe les modificateurs, et toute la séquence ZWJ : le glyphe
            // affiché est celui du premier membre.
            while (*i < len) {
                size_t save = *i;
                uint32_t nx = bbUtf8Next(s, len, i);
                if (nx == 0x200D) {
                    if (*i < len) bbUtf8Next(s, len, i);  // membre suivant absorbé
                } else if (bbCpInvisible(nx)) {
                    // absorbé
                } else {
                    *i = save;
                    break;
                }
            }
            out->glyph = (g >= 0) ? g : (int)kEmojiUnknown;
            out->start = segStart;
            out->end = *i;
            return true;
        }

        // Lot de texte : accumule jusqu'au prochain émoji ou invisible.
        out->glyph = -1;
        out->start = segStart;
        size_t end = *i;
        while (*i < len) {
            size_t save = *i;
            uint32_t nx = bbUtf8Next(s, len, i);
            if (bbCpInvisible(nx) || bbEmojiGlyph(nx) >= 0 || bbCpEmojiRange(nx)) {
                *i = save;
                break;
            }
            end = *i;
        }
        out->end = end;
        return true;
    }
    return false;
}
