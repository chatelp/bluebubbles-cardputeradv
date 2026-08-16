#pragma once
// Décodeurs de flux HTTP → ArduinoJson, sans jamais bufferiser le corps.
// Indépendants d'Arduino : templatés sur la source (Stream& sur l'appareil,
// source mémoire dans les tests natifs — voir test/test_native). ArduinoJson
// n'exige pas un Stream : read()/readBytes() suffisent (détection SFINAE).
//
// Contexte matériel : ~31 Ko de bloc contigu allouable (docs/02) — le corps
// est parsé au fil de l'eau, seul le résultat filtré occupe la mémoire.

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

// Horloge : millis() sur l'appareil, compteur factice dans les tests.
#ifdef ARDUINO
#include <Arduino.h>
static inline uint32_t bbNowMs() { return millis(); }
#else
uint32_t bbNowMs();  // fournie par test/test_native
#endif

// La source doit fournir : size_t readBytes(char*, size_t) — bloquant borné
// par son propre timeout, rendant 0 à la fermeture/expiration.

// ---------------------------------------------------------------------------
// Corps de taille connue (Content-Length) — ou borne de sûreté quand la
// taille est inconnue mais l'enveloppe est identity (fermeture = fin).
// ---------------------------------------------------------------------------
// budgetMs : durée TOTALE maximale de lecture du corps — une échéance
// horloge absolue, car le timeout de la source n'est qu'un timeout
// d'INACTIVITÉ (réarmé à chaque octet) : un serveur qui goutte le
// contournerait indéfiniment.
template <typename TSrc>
class BbBoundedStream {
public:
    BbBoundedStream(TSrc& src, size_t limit, uint32_t budgetMs = 30000)
        : _src(src), _left(limit), _deadline(bbNowMs() + budgetMs) {}
    int read() { return refill() ? _buf[_pos++] : -1; }
    int peek() { return refill() ? _buf[_pos] : -1; }
    size_t readBytes(char* buffer, size_t length) {
        size_t total = 0;
        while (total < length) {
            if (!refill()) break;
            size_t n = _fill - _pos;
            if (n > length - total) n = length - total;
            memcpy(buffer + total, _buf + _pos, n);
            _pos += n;
            total += n;
        }
        return total;
    }
    size_t remaining() const { return _left + (_fill - _pos); }
    // Consomme le reliquat (au plus maxSteps lectures) : vrai si tout est lu,
    // la connexion est alors propre pour le keep-alive. L'appelant borne le
    // temps via le timeout de la source.
    bool drain(uint32_t budgetMs = 3000) {
        _deadline = bbNowMs() + budgetMs;
        while (remaining()) {
            if (!refill()) break;
            _pos = _fill;
        }
        return remaining() == 0;
    }

private:
    bool refill() {
        if (_pos < _fill) return true;
        if (!_left) return false;
        if ((int32_t)(bbNowMs() - _deadline) >= 0) return false;  // budget épuisé
        size_t want = _left < sizeof(_buf) ? _left : sizeof(_buf);
        _fill = _src.readBytes((char*)_buf, want);
        _pos = 0;
        _left -= _fill;
        return _fill > 0;
    }
    TSrc& _src;
    size_t _left;
    uint32_t _deadline;
    uint8_t _buf[256];
    size_t _pos = 0, _fill = 0;
};

// ---------------------------------------------------------------------------
// Transfer-Encoding: chunked. Le flux HTTP brut livre le balisage (tailles
// hexadécimales + CRLF) : ce décodeur le désosse au fil de l'eau et consomme
// le 0-chunk final pour laisser la connexion réutilisable.
// ---------------------------------------------------------------------------
template <typename TSrc>
class BbChunkedStream {
public:
    explicit BbChunkedStream(TSrc& src, long totalCap = 200000, uint32_t budgetMs = 30000)
        : _src(src), _cap(totalCap), _deadline(bbNowMs() + budgetMs) {}
    int read() {
        if (!ensureData()) return -1;
        _chunkLeft--;
        return _buf[_pos++];
    }
    int peek() { return ensureData() ? _buf[_pos] : -1; }
    size_t readBytes(char* buffer, size_t length) {
        size_t total = 0;
        while (total < length && ensureData()) {
            size_t n = _fill - _pos;
            if (n > (size_t)_chunkLeft) n = (size_t)_chunkLeft;
            if (n > length - total) n = length - total;
            memcpy(buffer + total, _buf + _pos, n);
            _pos += n;
            _chunkLeft -= n;
            total += n;
        }
        return total;
    }
    // Vrai une fois le 0-chunk final et son épilogue consommés.
    bool clean() const { return _clean; }
    bool eof() const { return _eof; }
    bool drain(uint32_t budgetMs = 3000) {
        _deadline = bbNowMs() + budgetMs;
        char scratch[64];
        while (!_eof)
            if (!readBytes(scratch, sizeof(scratch))) break;
        return _clean;
    }

private:
    bool ensureData() {
        if (_eof) return _pos < _fill;
        if ((int32_t)(bbNowMs() - _deadline) >= 0) { _eof = true; return _pos < _fill; }
        if (_chunkLeft == 0 && _pos >= _fill) {
            if (!nextChunk()) return false;
        }
        if (_pos < _fill) return true;
        size_t want = (size_t)_chunkLeft < sizeof(_buf) ? (size_t)_chunkLeft : sizeof(_buf);
        _fill = _src.readBytes((char*)_buf, want);
        _pos = 0;
        if (!_fill) { _eof = true; return false; }  // flux mort en plein chunk
        return true;
    }
    // Une source TCP/TLS peut légalement rendre MOINS que demandé (frontière
    // de segment ou de record TLS) : seule une lecture NULLE est une fin de
    // flux. Ne jamais tester `readBytes(n) != n` directement.
    size_t readFully(char* b, size_t n) {
        size_t t = 0;
        while (t < n) {
            if ((int32_t)(bbNowMs() - _deadline) >= 0) break;  // budget épuisé
            size_t r = _src.readBytes(b + t, n - t);
            if (!r) break;
            t += r;
        }
        return t;
    }
    bool readLine(char* out, size_t cap) {
        size_t i = 0;
        while (i + 1 < cap) {
            char c;
            if (readFully(&c, 1) != 1) return false;
            if (c == '\n') break;
            if (c != '\r') out[i++] = c;
        }
        out[i] = 0;
        return true;
    }
    bool nextChunk() {
        char line[20];
        if (!_first) {  // CRLF de fin du chunk précédent
            char crlf[2];
            if (readFully(crlf, 2) != 2) { _eof = true; return false; }
        }
        _first = false;
        if (!readLine(line, sizeof(line))) { _eof = true; return false; }
        // Une ligne qui n'est pas une taille hexadécimale = enveloppe
        // inattendue : on s'arrête net plutôt que de lire du bruit.
        char* end = nullptr;
        long sz = strtol(line, &end, 16);
        if (end == line) { _eof = true; return false; }
        if (sz <= 0) {
            // Épilogue : éventuels en-têtes de fin, jusqu'à la ligne vide.
            for (int i = 0; i < 8; i++) {
                if (!readLine(line, sizeof(line))) break;
                if (!line[0]) { _clean = true; break; }
            }
            _eof = true;
            return false;
        }
        _total += sz;
        if (_total > _cap) { _eof = true; return false; }
        _chunkLeft = sz;
        return true;
    }
    TSrc& _src;
    long _cap;
    uint32_t _deadline;
    uint8_t _buf[256];
    size_t _pos = 0, _fill = 0;
    long _chunkLeft = 0;
    long _total = 0;
    bool _first = true, _eof = false, _clean = false;
};

// ---------------------------------------------------------------------------
// Clé de fusion des conversations (voir docs/04). Logique en C pur pour être
// testée en natif ; bb_client.cpp l'enrobe en String.
// buf doit faire >= addrLen + 3.
// Écrit "g:<guid>" (groupe), "e:<email minuscule>" ou "p:<chiffres>".
// ---------------------------------------------------------------------------
inline void bbBuildKey(const char* guid, char* buf, size_t bufLen) {
    const char* sep = strstr(guid, ";-;");
    if (!sep) {  // groupe : jamais fusionné
        snprintf(buf, bufLen, "g:%s", guid);
        return;
    }
    const char* addr = sep + 3;
    if (strchr(addr, '@')) {  // e-mail : minuscules exactes
        size_t o = 0;
        buf[o++] = 'e';
        buf[o++] = ':';
        for (const char* p = addr; *p && o + 1 < bufLen; p++)
            buf[o++] = (*p >= 'A' && *p <= 'Z') ? *p + 32 : *p;
        buf[o] = 0;
        return;
    }
    // Téléphone : seuls les préfixes EXPLICITEMENT équivalents de la
    // numérotation française sont normalisés (+33… ↔ 0…). Tout autre numéro
    // garde sa forme complète : une troncature aveugle fusionnerait des
    // correspondants réels différents (+1 212… / +1 812…).
    char digits[32];
    size_t d = 0;
    for (const char* p = addr; *p && d + 1 < sizeof(digits); p++)
        if (*p >= '0' && *p <= '9') digits[d++] = *p;
    digits[d] = 0;
    const char* keep = digits;
    if (d == 11 && digits[0] == '3' && digits[1] == '3') keep = digits + 2;
    else if (d == 10 && digits[0] == '0') keep = digits + 1;
    snprintf(buf, bufLen, "p:%s", keep);
}
