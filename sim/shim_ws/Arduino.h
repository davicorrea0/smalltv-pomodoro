// Mínimo do Arduino para compilar o Parsing-impl.h do core 3.1.2 e o ws_parse.h no computador (sim/ws_parse_test.cpp).
// O relógio é simulado: millis() só anda quando alguém espera (yield), então o teste de "preso para sempre" é instantâneo.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <string>
#include <functional>
#include <memory>
#include <vector>
#define PROGMEM
#define F(s) (s)
#define FPSTR(s) (s)
#define ARDUINO_ESP8266_RELEASE_3_1_2
extern uint32_t wsNow;    // milissegundos simulados
extern uint64_t wsSteps;  // leituras, para detectar o laço sem fim mesmo com dados chegando sem parar
[[noreturn]] void wsHang(const char* onde);
inline uint32_t millis() { return wsNow; }
inline void yield() {
  wsNow += 1;
  if (wsNow > 600000UL) wsHang("yield");  // 10 min simulados = preso
}
struct String : std::string {
  using std::string::string;
  using std::string::operator=;
  using std::string::operator+=;
  String() {}
  String(const std::string& s) : std::string(s) {}
  bool isEmpty() const { return empty(); }
  bool startsWith(const String& p) const { return rfind(p, 0) == 0; }
  bool equalsIgnoreCase(const String& o) const {
    if (size() != o.size()) return false;
    for (size_t i = 0; i < size(); i++)
      if (tolower((unsigned char)(*this)[i]) != tolower((unsigned char)o[i])) return false;
    return true;
  }
  String substring(unsigned a, unsigned b = (unsigned)-1) const {
    if (b > size()) b = size();
    if (a > b) { unsigned t = a; a = b; b = t; }
    if (a > size()) return String();
    return String(substr(a, b - a));
  }
  int indexOf(char c, unsigned from = 0) const {
    size_t i = find(c, from);
    return i == npos ? -1 : (int)i;
  }
  void trim() {
    size_t a = 0, b = size();
    while (a < b && isspace((unsigned char)(*this)[a])) a++;
    while (b > a && isspace((unsigned char)(*this)[b - 1])) b--;
    assign(substr(a, b - a));
  }
  void replace(const String& a, const String& b) {
    for (size_t i = 0; (i = find(a, i)) != npos; i += b.size()) std::string::replace(i, a.size(), b);
  }
  char charAt(unsigned i) const { return i < size() ? (*this)[i] : 0; }
  void remove(unsigned i) { if (i < size()) erase(i); }
  long toInt() const { return atol(c_str()); }
  void clear() { std::string::clear(); }
};
struct S2Stream {
  String& s;
  explicit S2Stream(String& x) : s(x) {}
};
