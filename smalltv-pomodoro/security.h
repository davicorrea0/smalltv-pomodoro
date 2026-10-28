#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
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

// O nome do próprio relógio na rede local ("smalltv-1a2b.local", porta opcional), sem diferenciar
// maiúsculas. Só esse nome exato: um nome qualquer reabriria o DNS rebinding que o IPv4 fecha.
static bool safeOwnHost(const char *s, size_t n, const char *name) {
  size_t k = strlen(name);
  if (!k || n < k + 6) return false;
  for (size_t i = 0; i < k; i++)
    if (tolower((unsigned char)s[i]) != tolower((unsigned char)name[i])) return false;
  const char *dot = ".local";
  for (size_t i = 0; i < 6; i++)
    if (tolower((unsigned char)s[k + i]) != dot[i]) return false;
  size_t i = k + 6;
  if (i == n) return true;
  if (s[i++] != ':' || i == n || n - i > 5) return false;
  unsigned port = 0;
  for (; i < n; i++) {
    if (s[i] < '0' || s[i] > '9') return false;
    port = port * 10 + (unsigned)(s[i] - '0');
  }
  return port > 0 && port <= 65535;
}

// ---------- mDNS mínimo (RFC 6762): só responde "qual o IPv4 de <nome>.local?" ----------
// O ESP8266mDNS do núcleo custa ~21 KB; para abrir o painel pelo nome basta o registro A.
// Lê um nome DNS em p[i..n) (ponteiros de compressão: no máximo 8 saltos), em minúsculas e com pontos.
// Rótulo com '.' ou byte nulo é recusado ("smalltv-1a2b.local" num rótulo só não vira o nosso nome).
static bool dnsReadName(const uint8_t *p, size_t n, size_t &i, char *out, size_t cap) {
  size_t pos = i, len = 0;
  bool jumped = false;
  int jumps = 0;
  for (;;) {
    if (pos >= n) return false;
    uint8_t c = p[pos];
    if (c == 0) {
      pos++;
      break;
    }
    if ((c & 0xC0) == 0xC0) {
      if (pos + 1 >= n || ++jumps > 8) return false;
      if (!jumped) i = pos + 2;
      jumped = true;
      pos = ((size_t)(c & 0x3F) << 8) | p[pos + 1];
      continue;
    }
    if (c & 0xC0 || pos + 1 + c > n) return false;
    if (len) {
      if (len + 1 >= cap) return false;
      out[len++] = '.';
    }
    for (size_t k = 0; k < c; k++) {
      uint8_t ch = p[pos + 1 + k];
      if (ch == '.' || ch == 0 || len + 1 >= cap) return false;
      out[len++] = (char)tolower(ch);
    }
    pos += 1 + c;
  }
  if (!jumped) i = pos;
  out[len] = 0;
  return true;
}

// É uma pergunta (não resposta) pelo registro A ou ANY, classe IN, de "<name>.local"? *qu = pediu
// resposta direta (bit QU). Nome comparado sem diferenciar maiúsculas.
static bool mdnsAsksFor(const uint8_t *p, size_t n, const char *name, bool *qu) {
  if (n < 12 || (p[2] & 0xF8)) return false;  // QR=1 (resposta) ou opcode diferente de consulta
  unsigned qd = (unsigned)p[4] << 8 | p[5];
  if (qd > 16) return false;
  char want[40], got[64];
  if (snprintf(want, sizeof want, "%s.local", name) >= (int)sizeof want) return false;
  for (char *c = want; *c; c++) *c = (char)tolower((unsigned char)*c);
  size_t i = 12;
  for (unsigned q = 0; q < qd; q++) {
    if (!dnsReadName(p, n, i, got, sizeof got) || i + 4 > n) return false;
    unsigned type = (unsigned)p[i] << 8 | p[i + 1], cls = (unsigned)p[i + 2] << 8 | p[i + 3];
    i += 4;
    if ((type == 1 || type == 255) && (cls & 0x7FFF) == 1 && !strcmp(got, want)) {
      if (qu) *qu = cls & 0x8000;
      return true;
    }
  }
  return false;
}

// Outro aparelho já responde por "<name>.local"? (resposta mDNS com um registro desse nome)
static bool mdnsHasAnswer(const uint8_t *p, size_t n, const char *name) {
  if (n < 12 || !(p[2] & 0x80)) return false;  // só respostas
  unsigned qd = (unsigned)p[4] << 8 | p[5], rr = ((unsigned)p[6] << 8 | p[7]) + ((unsigned)p[8] << 8 | p[9]) + ((unsigned)p[10] << 8 | p[11]);
  if (qd > 16 || rr > 64) return false;
  char want[40], got[64];
  if (snprintf(want, sizeof want, "%s.local", name) >= (int)sizeof want) return false;
  for (char *c = want; *c; c++) *c = (char)tolower((unsigned char)*c);
  size_t i = 12;
  for (unsigned q = 0; q < qd; q++) {
    if (!dnsReadName(p, n, i, got, sizeof got) || i + 4 > n) return false;
    i += 4;
  }
  for (unsigned r = 0; r < rr; r++) {
    if (!dnsReadName(p, n, i, got, sizeof got) || i + 10 > n) return false;
    if (!strcmp(got, want)) return true;
    size_t len = (size_t)p[i + 8] << 8 | p[i + 9];
    i += 10 + len;
    if (i > n) return false;
  }
  return false;
}

// Pergunta de sondagem (RFC 6762, 8.1): "alguém já usa <name>.local?", tipo ANY, pedindo resposta direta.
static size_t mdnsProbeQuery(uint8_t *o, size_t cap, const char *name) {
  size_t k = strlen(name);
  if (!k || k > 63 || cap < 12 + k + 8 + 4) return 0;
  memset(o, 0, 12);
  o[5] = 1;
  size_t i = 12;
  o[i++] = (uint8_t)k;
  memcpy(o + i, name, k);
  i += k;
  o[i++] = 5;
  memcpy(o + i, "local", 5);
  i += 5;
  o[i++] = 0;
  o[i++] = 0;
  o[i++] = 255;
  o[i++] = 0x80;  // QU: responder direto
  o[i++] = 1;
  return i;
}

// Monta a resposta com o registro A. legacy: quem perguntou não usa a porta 5353 (resolvedor comum):
// devolve o mesmo id, repete a pergunta e usa TTL curto, como pede a RFC 6762 (6.7). 0 se não couber.
static size_t mdnsAnswer(uint8_t *o, size_t cap, const char *name, const uint8_t ip[4], uint16_t id, bool legacy) {
  size_t k = strlen(name), need = 12 + (legacy ? k + 8 + 4 + 2 : k + 8) + 10 + 4;
  if (!k || k > 63 || cap < need) return 0;
  size_t i = 0;
  auto u16 = [&](unsigned v) { o[i++] = (uint8_t)(v >> 8); o[i++] = (uint8_t)v; };
  auto qname = [&] {
    o[i++] = (uint8_t)k;
    memcpy(o + i, name, k);
    i += k;
    o[i++] = 5;
    memcpy(o + i, "local", 5);
    i += 5;
    o[i++] = 0;
  };
  u16(legacy ? id : 0);
  u16(0x8400);  // resposta com autoridade
  u16(legacy ? 1 : 0);
  u16(1);
  u16(0);
  u16(0);
  if (legacy) {
    qname();
    u16(1);
    u16(1);
    u16(0xC00C);  // aponta para o nome da pergunta
  } else {
    qname();
  }
  u16(1);                         // A
  u16(legacy ? 0x0001 : 0x8001);  // IN (+ cache-flush no mDNS)
  u16(0);
  u16(legacy ? 10 : 120);  // TTL
  u16(4);
  memcpy(o + i, ip, 4);
  i += 4;
  return i;
}

// dias desde 1970 de uma data do calendário (y/m/d, m = 1..12)
static int64_t civilDays(int y, int m, int d) {
  y -= m <= 2;  // "days from civil"
  int era = y / 400, yoe = y - era * 400, doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  return (int64_t)era * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468;
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
  return civilDays(y, m, d) * 86400 + v[3] * 3600 + v[4] * 60;
}

// Cabeçalho "Date: Tue, 28 Oct 2026 12:00:00 GMT" (o que vem depois de "Date:") -> segundos desde 1970 (UTC).
// -1 se inválido. Só serve para MOSTRAR a hora até o SNTP chegar: vem por HTTP puro e nunca confirma nada.
static int64_t httpDateToEpoch(const char *s) {
  static const char MON[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
  if (!s) return -1;
  while (*s == ' ') s++;
  const char *c = strchr(s, ',');
  if (c && c - s <= 3) s = c + 1;  // dia da semana
  while (*s == ' ') s++;
  int d = 0, y = 0, hh, mm, ss, i = 0;
  for (; i < 2 && s[i] >= '0' && s[i] <= '9'; i++) d = d * 10 + (s[i] - '0');
  if (!i || s[i] != ' ') return -1;
  s += i + 1;
  int m = 0;
  for (int k = 0; k < 12 && !m; k++) if (!strncmp(s, MON + k * 3, 3)) m = k + 1;
  if (!m || s[3] != ' ') return -1;
  s += 4;
  for (i = 0; i < 4; i++) {
    if (s[i] < '0' || s[i] > '9') return -1;
    y = y * 10 + (s[i] - '0');
  }
  if (s[4] != ' ' || s[7] != ':' || s[10] != ':') return -1;
  int *f[3] = {&hh, &mm, &ss};
  for (int k = 0; k < 3; k++) {
    const char *p = s + 5 + k * 3;
    if (p[0] < '0' || p[0] > '9' || p[1] < '0' || p[1] > '9') return -1;
    *f[k] = (p[0] - '0') * 10 + (p[1] - '0');
  }
  if (d < 1 || d > 31 || y < 2000 || hh > 23 || mm > 59 || ss > 60) return -1;
  return civilDays(y, m, d) * 86400 + hh * 3600 + mm * 60 + ss;
}

// Hora guardada na memória RTC para sobreviver a um reinício por software (atualização, ajuste, queda): sec é a
// hora no instante em que ticks (system_get_rtc_time) foi lido. A RTC conta sozinha durante o reinício; cal é o
// período do tick em µs (system_rtc_clock_cali_proc, formato Q12). Na volta, a hora é sec + o tempo que passou.
struct RtcClock { uint32_t magic, sec, ticks, chk; };
static const uint32_t RTC_CLOCK_MAGIC = 0x434C4B31;  // "CLK1"
static uint32_t rtcClockChk(const RtcClock &r) { return r.magic ^ (r.sec * 2654435761u) ^ (r.ticks * 40503u) ^ 0x5A5AC3C3u; }
static RtcClock rtcClockMake(uint32_t sec, uint32_t ticks) {
  RtcClock r = {RTC_CLOCK_MAGIC, sec, ticks, 0};
  r.chk = rtcClockChk(r);
  return r;
}
// A hora agora, ou -1 se o registro não vale (memória de uma ligação anterior, corrompida, ou parado há mais de 10 min)
static int64_t rtcClockRestore(const RtcClock &r, uint32_t nowTicks, uint32_t cal) {
  if (r.magic != RTC_CLOCK_MAGIC || r.chk != rtcClockChk(r) || r.sec < 1700000000u || !cal) return -1;
  uint64_t us = ((uint64_t)(uint32_t)(nowTicks - r.ticks) * cal) >> 12;
  if (us > 600000000ull) return -1;
  return (int64_t)r.sec + (int64_t)(us / 1000000ull);
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

// No máximo n vezes por segundo (respostas SSDP: um pedido pequeno vira uma resposta ~3x maior).
struct RateLimit {
  uint32_t at = 0;
  uint8_t used = 0;
  bool take(uint32_t ms, uint8_t n) {
    if (ms - at >= 1000) { at = ms; used = 0; }
    return used < n && ++used;
  }
};
