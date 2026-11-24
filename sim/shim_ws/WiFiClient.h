// Cliente falso: bytes que "chegam" em instantes simulados, fim da conexão e espera com prazo como o Stream do core.
#pragma once
#include "Arduino.h"
struct WiFiClient {
  struct Seg { uint32_t at; std::string d; };
  std::vector<Seg> segs;           // o que o cliente envia, e quando chega
  uint32_t closeAt = 0xFFFFFFFFu;  // quando o cliente fecha o socket (nunca, por padrão)
  bool junk = false;               // depois dos segmentos, lixo sem fim ("xxxxxxxx\r\n")
  size_t seg = 0, off = 0, junkPos = 0;
  uint32_t timeout = 5000;         // prazo de cada readStringUntil
  uint64_t consumed = 0;
  void setTimeout(uint32_t t) { timeout = t; }
  uint32_t getTimeout() const { return timeout; }
  int available() {
    if (junk && seg >= segs.size()) return 1 << 20;
    size_t n = 0;
    for (size_t i = seg; i < segs.size() && segs[i].at <= wsNow; i++) n += segs[i].d.size() - (i == seg ? off : 0);
    return (int)n;
  }
  uint8_t connected() { return available() > 0 || wsNow < closeAt; }
  int read() {
    if (++wsSteps > 5000000ULL) wsHang("read");  // dados chegando sem parar e o laço sem fim
    while (seg < segs.size() && off >= segs[seg].d.size()) seg++, off = 0;
    if (seg < segs.size()) {
      if (segs[seg].at > wsNow) return -1;
      consumed++;
      return (uint8_t)segs[seg].d[off++];
    }
    if (junk) {
      static const char j[] = "xxxxxxxx\r\n";
      consumed++;
      return (uint8_t)j[junkPos++ % 10];
    }
    return -1;
  }
  int timedRead() {  // Stream::timedRead do core: tenta, cede (yield) e desiste no prazo
    uint32_t t0 = wsNow;
    do {
      int c = read();
      if (c >= 0) return c;
      yield();
    } while ((uint32_t)(wsNow - t0) < timeout);
    return -1;
  }
  size_t readBytes(char* out, size_t len) {
    size_t n = 0;
    while (n < len) {
      int c = timedRead();
      if (c < 0) break;
      out[n++] = (char)c;
    }
    return n;
  }
  String readStringUntil(char term) {
    String s;
    int c;
    while ((c = timedRead()) >= 0 && c != term) s += (char)c;
    return s;
  }
  void flush() {}
  void stop() {}
  size_t sendSize(S2Stream& d, size_t max, int tmo) {  // lê até max bytes com prazo (corpo urlencoded, como no core)
    size_t n = 0;
    uint32_t t0 = wsNow;
    while (n < max && (uint32_t)(wsNow - t0) < (uint32_t)tmo) {
      int c = read();
      if (c < 0) {
        if (!connected()) break;
        yield();
        continue;
      }
      d.s += (char)c;
      n++;
    }
    return n;
  }
};
