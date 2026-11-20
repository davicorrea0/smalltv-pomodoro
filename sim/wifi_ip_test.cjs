// Conexão do boot (connectStored + waitWifi, código real de modulos/web_portal.cpp) com um SDK de mentira.
// Caso da issue #44: depois de dois WiFi.begin() seguidos (o rápido falhou), o SDK pode ficar fora de STATION_GOT_IP com a
// estação já com IP e a rede funcionando (diag.rssi = 0 com painel e NTP respondendo). Antes o waitWifi esperava os 20 s
// pelo estado e abria o portal; agora vale o IP, e o boot.wf ganha + 8 para o relato seguinte mostrar que aconteceu.
// Uso: node wifi_ip_test.cjs saida.cpp (o sim/testes.sh compila e roda).
const fs = require('fs');
const source = require('./fonte.cjs');
const out = process.argv[2];
if (!out) throw Error('uso: node wifi_ip_test.cjs saida.cpp');
const a = source.indexOf('static uint32_t ssidHash('), b = source.indexOf('\n}', source.indexOf('bool connectStored(uint32_t timeoutMs) {'));
if (a < 0 || b < 0) throw Error('não achei ssidHash/connectStored');
if (!source.slice(a, b).includes('static bool wifiUp()')) throw Error('wifiUp precisa ficar entre ssidHash e connectStored');
const corpo = source.slice(a, b + 2).replace(/^static bool bootWifiIpOnly = false;.*$/m, '');
fs.writeFileSync(out, `#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <algorithm>
using std::min;
struct String { std::string s; String() {} String(const char *p) : s(p) {} const char *c_str() const { return s.c_str(); }
  unsigned length() const { return s.size(); } const char *begin() const { return s.data(); } const char *end() const { return s.data() + s.size(); } };
enum { WL_IDLE_STATUS = 0, WL_CONNECTED = 3, WL_DISCONNECTED = 7 };
enum { WIFI_STA = 1 };
struct WifiHint { uint32_t magic; uint32_t ssid; uint8_t bssid[6]; uint8_t chan, pad; };
const uint32_t WIFI_MAGIC = 0x57494649; const int WIFIH_OFF = 0;
static uint32_t g_ms = 1000;            // millis()
static int g_begins = 0;                // quantos WiFi.begin() o boot fez
static uint32_t g_beginAt = 0;          // quando foi o último
static bool g_pinned = false;           // o último begin fixou canal e BSSID
// roteiro do SDK de mentira: em quanto tempo depois do begin chegam o estado GOT_IP e o IP (0 = nunca)
static uint32_t g_gotPinned, g_ipPinned, g_got, g_ip;
static bool g_hint = true;
static uint32_t since() { return g_ms - g_beginAt; }
static uint32_t gotAfter() { return g_pinned ? g_gotPinned : g_got; }
static uint32_t ipAfter() { return g_pinned ? g_ipPinned : g_ip; }
struct IpStub { bool v; bool isSet() const { return v; } };
static struct {
  int status() { return g_begins && gotAfter() && since() >= gotAfter() ? WL_CONNECTED : WL_DISCONNECTED; }
  bool isConnected() { return status() == WL_CONNECTED; }
  IpStub localIP() { return {g_begins && ipAfter() && since() >= ipAfter()}; }
  void mode(int) {} void hostname(const char *) {} void persistent(bool) {}
  String SSID() { return String("casa"); } String psk() { return String("segredo-de-teste"); }
  void begin() { g_begins++; g_beginAt = g_ms; g_pinned = false; }
  void begin(const char *, const char *) { g_begins++; g_beginAt = g_ms; g_pinned = false; }
  void begin(const char *, const char *, int, const uint8_t *) { g_begins++; g_beginAt = g_ms; g_pinned = true; }
} WiFi;
static uint32_t ssidHashC(const char *s) { uint32_t h = 2166136261u; for (; *s; s++) h = (h ^ (uint8_t)*s) * 16777619u; return h; }
static struct { void get(int, WifiHint &h) { h = {g_hint ? WIFI_MAGIC : 0, ssidHashC("casa"), {1, 2, 3, 4, 5, 6}, 6, 0}; } } EEPROM;
uint32_t millis() { return g_ms; }
void delay(uint32_t ms) { g_ms += ms; }
static int g_anim = 0; void bootAnim(uint8_t) { g_anim++; }
static char netName[16] = "smalltv-abcd";
static bool wifiHarden() { return false; }
static uint8_t bootWifiPath = 0; static bool wifiPinned = false;
static bool bootWifiIpOnly = false;
${corpo}
static int falhas = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FALHOU %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\\n"); falhas++; } } while (0)
struct Caso { const char *nome; bool hint; uint32_t gotPinned, ipPinned, got, ip; bool ok; uint8_t wf; uint32_t maxMs; };
int main() {
  const Caso casos[] = {
    // nome, dica, GOT_IP/IP no rápido, GOT_IP/IP sem fixar, resultado, wf, tempo máximo
    {"normal: o rápido conecta (IP um instante antes do GOT_IP)", true, 1500, 1400, 0, 0, true, 1, 1600},
    {"o rápido falha, a varredura conecta", true, 0, 0, 3000, 2900, true, 3, 7100},
    {"issue #44: o rápido falha e o SDK não diz GOT_IP com IP", true, 0, 0, 0, 2500, true, 3 | 8, 7600},
    {"rápido com IP e o SDK sem GOT_IP", true, 0, 1200, 0, 0, true, 1 | 8, 2300},
    {"sem dica: rede salva com IP e o SDK sem GOT_IP", false, 0, 0, 0, 2000, true, 0 | 8, 3100},
    {"sem rede nenhuma: portal depois de 20 s", true, 0, 0, 0, 0, false, 3, 20100},
  };
  for (const Caso &c : casos) {
    g_ms = 1000; g_begins = 0; g_beginAt = 0; g_pinned = false; g_hint = c.hint;
    g_gotPinned = c.gotPinned; g_ipPinned = c.ipPinned; g_got = c.got; g_ip = c.ip;
    bootWifiPath = 0; wifiPinned = false; bootWifiIpOnly = false;
    uint32_t t0 = g_ms;
    bool ok = connectStored(20000);
    uint32_t used = g_ms - t0;
    CHECK(ok == c.ok, "%s: connectStored = %d", c.nome, ok);
    CHECK(bootWifiPath == c.wf, "%s: wf = %u, esperado %u", c.nome, bootWifiPath, c.wf);
    CHECK(used <= c.maxMs, "%s: levou %u ms (máximo %u)", c.nome, used, c.maxMs);
    CHECK(wifiPinned == ((c.wf & 7) == 1), "%s: wifiPinned = %d", c.nome, wifiPinned);
  }
  if (falhas) return 1;
  printf("wifi_ip: boot conecta pelo IP quando o SDK não diz GOT_IP (issue #44), wf + 8 marca o caso; sem rede, portal em 20 s\\n");
  return 0;
}
`);
