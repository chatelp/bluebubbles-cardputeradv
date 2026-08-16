// Shim hôte : le strict nécessaire d'Arduino pour compiler l'UI en natif
// (simulateur SDL). Seuls les en-têtes du projet l'incluent — jamais le code
// réseau ni la NVS, qui restent côté firmware.
#pragma once
#ifndef SIM_BUILD
#error "Ce shim n'est destiné qu'au simulateur (SIM_BUILD)."
#endif

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using std::max;
using std::min;

uint32_t millis();  // fournie par main_sim.cpp (SDL_GetTicks)

// Sous-ensemble de la String d'Arduino, adossé à std::string. La conversion
// implicite vers const char* sert print()/textWidth() de LovyanGFX natif ;
// les operator[] surchargés (int ET size_t) gardent l'indexation sur le
// membre, sans ambiguïté avec la conversion pointeur.
class String {
public:
    String() {}
    String(const char* s) : _s(s ? s : "") {}
    String(const std::string& s) : _s(s) {}
    String(char c) : _s(1, c) {}
    String(int v) : _s(std::to_string(v)) {}
    String(unsigned int v) : _s(std::to_string(v)) {}
    String(long v) : _s(std::to_string(v)) {}
    String(unsigned long v) : _s(std::to_string(v)) {}
    String(long long v) : _s(std::to_string(v)) {}
    String(unsigned long long v) : _s(std::to_string(v)) {}
    String(short v) : _s(std::to_string(v)) {}
    String(unsigned short v) : _s(std::to_string(v)) {}
    String(unsigned char v) : _s(std::to_string((int)v)) {}

    size_t length() const { return _s.size(); }
    const char* c_str() const { return _s.c_str(); }
    operator const char*() const { return _s.c_str(); }
    bool isEmpty() const { return _s.empty(); }
    void reserve(size_t n) { _s.reserve(n); }

    char operator[](int i) const { return i >= 0 && (size_t)i < _s.size() ? _s[i] : 0; }
    char operator[](size_t i) const { return i < _s.size() ? _s[i] : 0; }

    String& operator+=(const String& o) { _s += o._s; return *this; }
    String& operator+=(const char* o) { _s += o ? o : ""; return *this; }
    String& operator+=(char c) { _s += c; return *this; }
    String operator+(const String& o) const { return String(_s + o._s); }
    String operator+(const char* o) const { return String(_s + (o ? o : "")); }
    String operator+(char c) const { return String(_s + c); }
    String operator+(int v) const { return String(_s + std::to_string(v)); }
    String operator+(unsigned v) const { return String(_s + std::to_string(v)); }
    String operator+(long long v) const { return String(_s + std::to_string(v)); }

    bool operator==(const String& o) const { return _s == o._s; }
    bool operator==(const char* o) const { return _s == (o ? o : ""); }
    bool operator!=(const String& o) const { return _s != o._s; }
    bool operator!=(const char* o) const { return !(*this == o); }
    bool operator<(const String& o) const { return _s < o._s; }  // std::map

    bool startsWith(const String& p) const { return _s.rfind(p._s, 0) == 0; }
    bool endsWith(const String& p) const {
        return _s.size() >= p._s.size() && !_s.compare(_s.size() - p._s.size(), p._s.size(), p._s);
    }
    int indexOf(char c, size_t from = 0) const {
        auto p = _s.find(c, from);
        return p == std::string::npos ? -1 : (int)p;
    }
    int indexOf(const char* sub, size_t from = 0) const {
        auto p = _s.find(sub ? sub : "", from);
        return p == std::string::npos ? -1 : (int)p;
    }
    int indexOf(const String& sub, size_t from = 0) const { return indexOf(sub.c_str(), from); }
    String substring(size_t from) const {
        return from >= _s.size() ? String() : String(_s.substr(from));
    }
    String substring(size_t from, size_t to) const {
        if (from >= _s.size() || to <= from) return String();
        return String(_s.substr(from, min(to, _s.size()) - from));
    }
    void replace(const String& a, const String& b) {
        if (a._s.empty()) return;
        size_t p = 0;
        while ((p = _s.find(a._s, p)) != std::string::npos) {
            _s.replace(p, a._s.size(), b._s);
            p += b._s.size();
        }
    }
    void remove(size_t index) { if (index < _s.size()) _s.erase(index); }
    void remove(size_t index, size_t count) { if (index < _s.size()) _s.erase(index, count); }
    long toInt() const { return std::strtol(_s.c_str(), nullptr, 10); }

private:
    std::string _s;
};

inline String operator+(const char* a, const String& b) { return String(a) + b; }
