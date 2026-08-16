#include "sound.h"
#include "app_config.h"

#include <M5Cardputer.h>
#include <math.h>

namespace Snd {

// 11 025 Hz : les partiels utiles montent à 5 kHz, ce qui suffit largement
// pour des timbres de bois, et divise par quatre la mémoire d'un 44 kHz.
static const float SR = 11025.0f;

// Gamme pentatonique de ré majeur — aucune dissonance possible, tout ce qui
// sonne ensemble sonne juste.
static const float D4 = 293.66f, F4 = 349.23f, A4 = 440.0f;
static const float D5 = 587.33f, FS5 = 739.99f;

struct Buf {
    int16_t* pcm = nullptr;
    size_t len = 0;
};
static Buf sBuf[COUNT];
static bool sEnabled[COUNT] = {false};
static uint8_t sVolume = 0;
static uint32_t sRnd = 22222;

static float noise() {
    sRnd = sRnd * 1664525u + 1013904223u;
    return ((float)(int32_t)(sRnd >> 8) / 8388608.0f) - 1.0f;
}

// Une note de barre frappée : trois partiels dans le rapport 1 : 4 : 10
// (accord de marimba), les aigus s'éteignant plus vite que le fondamental —
// c'est cette décroissance différenciée qui fait entendre du bois.
static void note(float* acc, size_t n, float t0, float freq, float amp, float tau) {
    static const float PF[3] = {1.0f, 4.0f, 10.0f};
    static const float PA[3] = {1.0f, 0.30f, 0.10f};
    static const float PD[3] = {1.0f, 2.1f, 3.8f};
    size_t s0 = (size_t)(t0 * SR);
    for (int p = 0; p < 3; p++) {
        float f = freq * PF[p];
        if (f > SR * 0.45f) continue;  // au-delà de Nyquist : repliement, on saute
        float w = 2.0f * (float)M_PI * f / SR;
        float a = amp * PA[p];
        float d = tau / PD[p];
        for (size_t i = s0; i < n; i++) {
            float t = (float)(i - s0) / SR;
            float env = expf(-t / d);
            if (env < 0.0004f) break;
            // 2 ms de montée : sans elle, le démarrage claque.
            float atk = t < 0.002f ? t / 0.002f : 1.0f;
            acc[i] += a * env * atk * sinf(w * (float)(i - s0));
        }
    }
}

// Sinus pur : sert de sub (chaleur) ou de souffle glissant.
static void glide(float* acc, size_t n, float t0, float f0, float f1,
                  float amp, float tau) {
    size_t s0 = (size_t)(t0 * SR);
    float phase = 0;
    for (size_t i = s0; i < n; i++) {
        float t = (float)(i - s0) / SR;
        float env = expf(-t / tau);
        if (env < 0.0004f) break;
        float k = tau > 0 ? fminf(t / (tau * 2.0f), 1.0f) : 1.0f;
        float f = f0 + (f1 - f0) * k;
        phase += 2.0f * (float)M_PI * f / SR;
        float atk = t < 0.003f ? t / 0.003f : 1.0f;
        acc[i] += amp * env * atk * sinf(phase);
    }
}

// Normalise puis convertit en 16 bits signés.
static bool finish(Buf& b, float* acc, size_t n, float peakTarget) {
    float peak = 0;
    for (size_t i = 0; i < n; i++) peak = fmaxf(peak, fabsf(acc[i]));
    if (peak < 1e-6f) return false;
    float g = peakTarget * 32767.0f / peak;
    b.pcm = (int16_t*)malloc(n * sizeof(int16_t));
    if (!b.pcm) return false;
    b.len = n;
    for (size_t i = 0; i < n; i++) {
        float v = acc[i] * g;
        b.pcm[i] = (int16_t)(v > 32767.0f ? 32767.0f : (v < -32768.0f ? -32768.0f : v));
    }
    return true;
}

static bool render(Event e) {
    float dur;
    switch (e) {
        case KEY:      dur = 0.016f; break;
        case SENT:     dur = 0.26f;  break;
        case RECEIVED: dur = 0.32f;  break;
        case NOTIF:    dur = 0.42f;  break;
        case ERROR:    dur = 0.30f;  break;
        default: return false;
    }
    size_t n = (size_t)(dur * SR);
    float* acc = (float*)calloc(n, sizeof(float));
    if (!acc) return false;

    switch (e) {
        case KEY:
            // Un « toc » de clavier, pas un bip : bruit très bref filtré
            // passe-bas, plus une basse qui meurt en 9 ms.
            {
                float lp = 0;
                for (size_t i = 0; i < n; i++) {
                    float t = (float)i / SR;
                    lp += (noise() - lp) * 0.25f;          // passe-bas 1 pôle
                    acc[i] += lp * 0.9f * expf(-t / 0.0022f);
                }
                glide(acc, n, 0, 200.0f, 170.0f, 0.7f, 0.009f);
                glide(acc, n, 0, 420.0f, 400.0f, 0.18f, 0.005f);
            }
            break;

        case SENT:
            // Ça part : quarte ascendante A4 → D5, plus un souffle qui monte.
            note(acc, n, 0.000f, A4, 0.85f, 0.10f);
            note(acc, n, 0.055f, D5, 1.00f, 0.13f);
            glide(acc, n, 0.010f, 700.0f, 1500.0f, 0.10f, 0.075f);
            break;

        case RECEIVED:
            // Ça arrive et ça se pose : quarte descendante D5 → A4, sub chaud.
            note(acc, n, 0.000f, D5, 0.80f, 0.11f);
            note(acc, n, 0.075f, A4, 1.00f, 0.20f);
            glide(acc, n, 0.075f, 220.0f, 220.0f, 0.14f, 0.18f);
            break;

        case NOTIF:
            // Plus présent : arpège de ré majeur qui se résout sur le sub.
            note(acc, n, 0.000f, A4, 0.75f, 0.10f);
            note(acc, n, 0.075f, D5, 0.85f, 0.11f);
            note(acc, n, 0.150f, FS5, 1.00f, 0.24f);
            glide(acc, n, 0.150f, D4, D4, 0.18f, 0.28f);
            break;

        case ERROR:
            // Ça n'est pas passé : tierce descendante, la note basse doublée
            // et désaccordée de 5 Hz — un battement, pas une stridence.
            note(acc, n, 0.000f, A4, 0.85f, 0.10f);
            note(acc, n, 0.090f, F4, 1.00f, 0.22f);
            note(acc, n, 0.090f, F4 - 5.0f, 0.55f, 0.22f);
            break;

        default: break;
    }

    // Le clic reste volontairement en retrait : il accompagne la frappe, il
    // ne la commente pas.
    bool ok = finish(sBuf[e], acc, n, e == KEY ? 0.35f : 0.82f);
    free(acc);
    return ok;
}

void begin() { applyConfig(); }

void applyConfig() {
    sVolume = gConfig.sndVolume;
    if (sVolume > 100) sVolume = 100;
    sEnabled[KEY]      = gConfig.sndKeys;
    sEnabled[SENT]     = gConfig.sndSend;
    sEnabled[RECEIVED] = gConfig.sndRecv;
    sEnabled[NOTIF]    = gConfig.sndNotif;
    sEnabled[ERROR]    = gConfig.sndNotif;  // l'échec suit les notifications

    if (!sVolume) {
        M5Cardputer.Speaker.setVolume(0);
        return;
    }
    // 0-100 dans les réglages → 0-255 côté M5Unified, avec un plancher
    // audible : en dessous de ~40, le haut-parleur ne rend plus rien.
    M5Cardputer.Speaker.setVolume((uint8_t)(40 + (sVolume * 215) / 100));

    // Un tampon déjà rendu n'est jamais refait : render() alloue, le refaire
    // fuirait la mémoire à chaque passage dans les réglages.
    for (int e = 0; e < COUNT; e++)
        if (sEnabled[e] && !sBuf[e].pcm && !render((Event)e)) sEnabled[e] = false;
}

void play(Event e) {
    if (e >= COUNT || !sEnabled[e] || !sVolume || !sBuf[e].pcm) return;
    if (e == KEY) {
        // ±4 % de hauteur au hasard : sans cette variation, une frappe rapide
        // sonne comme une mitrailleuse. Canal 0, interrompu par lui-même.
        float r = SR * (0.96f + 0.08f * ((float)(sRnd = sRnd * 1103515245u + 12345u) /
                                         4294967296.0f));
        M5Cardputer.Speaker.playRaw(sBuf[e].pcm, sBuf[e].len, (uint32_t)r, false, 1, 0, true);
    } else {
        // Canal 1 : une frappe clavier ne coupe jamais une notification.
        M5Cardputer.Speaker.playRaw(sBuf[e].pcm, sBuf[e].len, (uint32_t)SR, false, 1, 1, true);
    }
}

size_t bytesUsed() {
    size_t t = 0;
    for (int e = 0; e < COUNT; e++) t += sBuf[e].len * sizeof(int16_t);
    return t;
}

}  // namespace Snd
