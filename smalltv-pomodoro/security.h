#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
// Allocation-free checks shared with the regression tests.
struct UploadGate {
  size_t bytes = 0, limit = 0;
  bool active = false;
  void begin(bool authorized, size_t maxBytes) { bytes = 0; limit = maxBytes; active = authorized; }
  bool take(size_t n) {
    if (!active || n > limit - bytes) { active = false; return false; }
    bytes += n; return true;
  }
  void stop() { active = false; }
};
struct AuthThrottle {
  struct Entry { uint32_t ip = 0, until = 0, last = 0; uint8_t failures = 0; } entries[4];
  static bool before(uint32_t now, uint32_t deadline) { return (int32_t)(deadline - now) > 0; }
  Entry *find(uint32_t ip, uint32_t now, bool create) {
    Entry *empty = nullptr;
    Entry *oldest = &entries[0];
    for (Entry &e : entries) {
      if (e.ip == ip) return &e;
      if (!e.ip) { if (!empty) empty = &e; continue; }
      if ((int32_t)(e.last - oldest->last) < 0) oldest = &e;
    }
    if (!create) return nullptr;
    Entry *slot = empty ? empty : oldest;
    *slot = Entry();
    slot->last = now;
    return slot;
  }
  bool blocked(uint32_t ip, uint32_t now, uint32_t *waitMs = nullptr) {
    Entry *e = find(ip, now, false);
    bool yes = e && before(now, e->until);
    if (waitMs) *waitMs = yes ? e->until - now : 0;
    return yes;
  }
  uint32_t fail(uint32_t ip, uint32_t now) {
    Entry *e = find(ip, now, true);
    if (e->ip != ip || now - e->last > 300000UL) e->failures = 0;
    e->ip = ip;
    if (e->failures < 6) e->failures++;
    uint32_t wait = 1000UL << (e->failures - 1);  // 1, 2, 4, 8, 16, 32 s
    e->last = now;
    e->until = now + wait;
    return wait;
  }
  void success(uint32_t ip) {
    for (Entry &e : entries) if (e.ip == ip) { e = Entry(); return; }
  }
};
static bool safeHttpUrl(const char *s, size_t n) {
  if (n < 8 || n > 400 || memcmp(s, "http://", 7)) return false;
  for (size_t i=0; i<n; i++) {
    unsigned char c = s[i];
    if (c <= 0x20 || c == 0x7f || c == '"' || c == '\\' || c == '#') return false;
  }
  size_t end=7;
  while (end<n && s[end]!='/' && s[end]!='?') end++;
  size_t hostEnd=7;
  while(hostEnd<end && s[hostEnd]!=':') {
    char c=s[hostEnd++];
    if (!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='-')) return false;
  }
  if (hostEnd==7) return false;
  if (hostEnd<end) {
    uint32_t port=0;
    if (++hostEnd==end) return false;
    while(hostEnd<end) {
      char c=s[hostEnd++]; if(c<'0'||c>'9')return false;
      port=port*10+(c-'0'); if(port>65535)return false;
    }
    if (!port) return false;
  }
  return true;
}
static bool safeIpv4Host(const char *s, size_t n) {
  if (!n || n > 21) return false;
  size_t i = 0;
  for (int part = 0; part < 4; part++) {
    if (i >= n || s[i] < '0' || s[i] > '9') return false;
    unsigned value = 0, digits = 0;
    while (i < n && s[i] >= '0' && s[i] <= '9') {
      value = value * 10 + (unsigned)(s[i++] - '0');
      if (++digits > 3 || value > 255) return false;
    }
    if (part < 3) {
      if (i >= n || s[i++] != '.') return false;
    }
  }
  if (i == n) return true;
  if (s[i++] != ':' || i == n) return false;
  unsigned port = 0;
  while (i < n) {
    if (s[i] < '0' || s[i] > '9') return false;
    port = port * 10 + (unsigned)(s[i++] - '0');
    if (port > 65535) return false;
  }
  return port > 0;
}

// "2026-10-23T00:30" -> segundos desde 1970 (como se fosse UTC; quem chama tira o fuso). -1 se inválido.
static int64_t isoToEpoch(const char *s) {
  if (!s || strlen(s) < 16 || s[4] != '-' || s[7] != '-' || s[10] != 'T' || s[13] != ':') return -1;
  int v[5], at[5] = {0, 5, 8, 11, 14}, len[5] = {4, 2, 2, 2, 2};
  for (int k = 0; k < 5; k++) {
    v[k] = 0;
    for (int i = 0; i < len[k]; i++) {
      char c = s[at[k] + i];
      if (c < '0' || c > '9') return -1;
      v[k] = v[k] * 10 + (c - '0');
    }
  }
  int y = v[0], m = v[1], d = v[2];
  if (m < 1 || m > 12 || d < 1 || d > 31 || v[3] > 23 || v[4] > 59) return -1;
  y -= m <= 2;  // "days from civil"
  int era = y / 400, yoe = y - era * 400, doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  int64_t days = (int64_t)era * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468;
  return days * 86400 + v[3] * 3600 + v[4] * 60;
}

// Hora protegida. O SNTP do núcleo aceita qualquer resposta (SNTP_CHECK_RESPONSE 0 no lwIP pré-compilado):
// um pacote UDP injetado (FragAttacks, ou alguém no caminho) muda a hora. A hora só fica "confirmada"
// quando bate com uma segunda fonte: a hora local que o Open-Meteo manda por TCP (bem mais difícil de
// forjar às cegas) ou, sem clima, duas sincronizações seguidas que concordam. Com a hora confirmada,
// um salto do SNTP é desfeito até ser confirmado. O histórico de focos só é gravado com ok() == true.
struct TimeGuard {
  int64_t ref = 0;              // última hora confirmada (s desde 1970)
  uint32_t refMs = 0;           // millis() nesse momento
  int64_t cand = 0;             // hora do SNTP que ainda não foi confirmada
  uint32_t candMs = 0;
  bool confirmed = false;
  uint16_t undone = 0;          // saltos desfeitos desde que ligou
  static bool near(int64_t a, int64_t b, int64_t tol) { return a - b <= tol && b - a <= tol; }
  int64_t expected(uint32_t ms) const { return ref + (int64_t)((uint32_t)(ms - refMs) / 1000); }
  int64_t candNow(uint32_t ms) const { return cand + (int64_t)((uint32_t)(ms - candMs) / 1000); }
  void confirm(int64_t t, uint32_t ms) { ref = t; refMs = ms; cand = 0; confirmed = true; }
  // O SNTP acabou de pôr a hora t. Devolve a hora que deve valer: t, ou a esperada se o salto foi desfeito.
  // twoSyncs: sem a segunda fonte (clima desligado), duas sincronizações que concordam confirmam.
  int64_t onSntp(int64_t t, uint32_t ms, bool twoSyncs) {
    if (confirmed && near(t, expected(ms), 120)) { ref = t; refMs = ms; return t; }  // só a correção de sempre
    if (twoSyncs && cand && near(t, candNow(ms), 120)) { confirm(t, ms); return t; }
    cand = t;
    candMs = ms;
    if (!confirmed) return t;  // logo depois de ligar: vale na tela, sem gravar histórico até confirmar
    undone++;
    return expected(ms);
  }
  // Hora UTC do clima (o começo do quarto de hora atual: até ~15 min atrás). Devolve a hora que deve valer.
  int64_t onWeather(int64_t utc, int64_t now, uint32_t ms) {
    const int64_t tol = 1200;
    if (near(now, utc + 450, tol)) { confirm(now, ms); return now; }
    if (cand && near(candNow(ms), utc + 450, tol)) { int64_t c = candNow(ms); confirm(c, ms); return c; }
    confirmed = false;  // nenhuma bate: segue mostrando, mas não grava histórico
    return now;
  }
  bool ok(int64_t now, uint32_t ms) const { return confirmed && near(now, expected(ms), 120); }
  // de hora em hora: acompanha o millis() (que volta a zero a cada 49 dias) sem perder a referência
  void tick(int64_t now, uint32_t ms) { if (ok(now, ms) && (uint32_t)(ms - refMs) > 3600000) { ref = now; refMs = ms; } }
};

// Senha errada: o mesmo IP espera 1 s antes de tentar de novo (antes era delay(1000), que congelava tela,
// timer e Alexa a cada tentativa). Guarda os 4 IPs mais recentes.
struct AuthThrottle {
  struct { uint32_t ip, at; } f[4] = {};
  bool blocked(uint32_t ip, uint32_t ms) const {
    for (auto &e : f) if (e.at && e.ip == ip && ms - e.at < 1000) return true;
    return false;
  }
  void fail(uint32_t ip, uint32_t ms) {
    int o = 0;
    for (int i = 0; i < 4; i++) {
      if (f[i].ip == ip) { o = i; break; }
      if (!f[i].at || ms - f[i].at > ms - f[o].at) o = i;
    }
    f[o].ip = ip;
    f[o].at = ms | 1;
  }
};

// No máximo n vezes por segundo (respostas SSDP: um pedido pequeno vira uma resposta ~3x maior).
struct RateLimit {
  uint32_t at = 0;
  uint8_t used = 0;
  bool take(uint32_t ms, uint8_t n) {
    if (ms - at >= 1000) { at = ms; used = 0; }
    return used < n && ++used;
  }
};
