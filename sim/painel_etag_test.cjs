// ETag do painel (GET /): compila o corpo real do handler com um servidor e um LittleFS de mentira e confere
//  - 200 na primeira abertura, com ETag = hash do painel desta versão e Cache-Control: no-cache;
//  - 304 (sem corpo) quando o navegador manda If-None-Match igual;
//  - 200 com o hash de outra versão (depois de um OTA o navegador recebe o painel novo);
//  - 200 com o carregador, sem ETag e nunca 304, quando /www/<hash>.gz não existe ou o LittleFS não montou;
//  - com senha e sem login (readOk falso), nada de 304 nem de ETag: o 401 sai antes.
// O servidor de mentira responde ao header("If-None-Match") sem collectHeaders: no core 3.1.2 o collectHeaders sempre
// acrescenta If-None-Match (ESP8266WebServer-impl.h, ETAG_HEADER), e o serveStatic do core faz o mesmo send(304). Que o
// core entrega o cabeçalho só o relógio prova (200 → 304 no DevTools).
// Uso: node sim/painel_etag_test.cjs saida.cpp; depois g++ -std=c++17 saida.cpp (o sim/testes.sh faz isso).
const fs = require('fs'), path = require('path');
const source = require('./fonte.cjs');
const webh = fs.readFileSync(path.join(__dirname, '../smalltv-pomodoro/web.h'), 'utf8');
const hash = webh.match(/#define PANEL_HASH "([0-9a-f]{24})"/)[1];
const sig = 'static void sendPanel() {', a = source.indexOf(sig), b = source.indexOf('\n}', a);
if (a < 0 || b < 0) throw Error('não achei o sendPanel()');
const body = source.slice(a + sig.length, b);
const web = source.slice(source.indexOf('void setupWeb() {'), source.indexOf('\n}', source.indexOf('void setupWeb() {')));
if (!web.includes('server.on("/", HTTP_GET, sendPanel);')) throw Error('setupWeb: o GET / precisa ser o sendPanel');
if (!body.includes('readOk()') || body.indexOf('readOk()') > body.indexOf('304')) throw Error('GET /: readOk() antes do 304');
// Accept-Ranges faria o navegador guardar uma entrada parcial e pedir a retomada por Range, que o servidor não atende
if (body.includes('Accept-Ranges')) throw Error('GET /: sem Accept-Ranges');
const out = process.argv[2];
if (!out) throw Error('saida.cpp obrigatória');
fs.writeFileSync(out, `#include <string>
#include <vector>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdint>
#define PANEL_HASH "${hash}"
#define F(s) (s)
#define PSTR(s) (s)
#define FPSTR(s) (s)
typedef const char *PGM_P;
static int strcmp_P(const char *a, const char *b) { return strcmp(a, b); }
static const uint8_t LOADER_HTML_GZ[7] = {1, 2, 3, 4, 5, 6, 7};
struct String : std::string { using std::string::string; String(const std::string &s) : std::string(s) {} };
static int closes;
struct File { bool live = false; operator bool() const { return live; } void close() { if (live) closes++; live = false; } };
static bool fsOk = true, hasPanel = true, logged = true;
static std::string opened;
struct { File open(const char *p, const char *) { opened = p; return {hasPanel}; } } LittleFS;
struct Reply { int code = 0; std::vector<std::pair<std::string, std::string>> hdr; std::string body; bool streamed = false;
  std::string h(const char *k) const { for (auto &x : hdr) if (x.first == k) return x.second; return ""; } };
static Reply r;
static std::string inm;  // If-None-Match do pedido
struct {
  void sendHeader(const String &k, const String &v) { r.hdr.push_back({k, v}); }
  String header(const String &k) { assert(k == "If-None-Match"); return inm; }
  void send(int c) { assert(!r.code); r.code = c; }
  void send_P(int c, PGM_P, PGM_P p, size_t n) { assert(!r.code); r.code = c; r.body.assign(p, n); }
  void streamFile(File &f, const char *) { assert(!r.code && f); r.code = 200; r.streamed = true; }
} server;
static bool readOk() { if (logged) return true; r.code = 401; return false; }
static void panelGet() {${body}
}
static Reply get(const char *ifNoneMatch) { r = Reply(); inm = ifNoneMatch; opened.clear(); closes = 0; panelGet(); assert(r.h("Accept-Ranges").empty()); return r; }
int main() {
  const std::string et = "\\"" PANEL_HASH "\\"";
  // 1ª abertura: painel inteiro, com o ETag desta versão e revalidação obrigatória
  Reply x = get("");
  assert(x.code == 200 && x.streamed && x.h("ETag") == et && x.h("Cache-Control") == "no-cache");
  assert(opened == "/www/" PANEL_HASH ".gz");
  // mesma versão: 304 sem corpo, com o ETag e o Cache-Control de novo
  x = get(et.c_str());
  assert(x.code == 304 && !x.streamed && x.body.empty() && x.h("ETag") == et && x.h("Cache-Control") == "no-cache");
  assert(closes == 1);  // o arquivo do painel é fechado também no 304
  // painel em cache de outra versão (antes do OTA) ou ETag sem aspas: painel inteiro
  for (const char *old : {"\\"0123456789abcdef01234567\\"", PANEL_HASH, "W/\\"0123456789abcdef01234567\\"", "*"}) {
    x = get(old);
    assert(x.code == 200 && x.streamed && x.h("ETag") == et);
  }
  // sem o painel na memória de arquivos (ou sem LittleFS): carregador, sem ETag e nunca 304
  for (int fsBad = 0; fsBad < 2; fsBad++) {
    hasPanel = false; fsOk = !fsBad;
    for (const char *h : {"", et.c_str()}) {
      x = get(h);
      assert(x.code == 200 && !x.streamed && x.body.size() == sizeof LOADER_HTML_GZ && x.h("ETag").empty());
      assert(x.h("Content-Encoding") == "gzip" && x.h("Cache-Control") == "no-cache");
    }
  }
  hasPanel = true; fsOk = true;
  // com senha e sem login: o 401 sai antes, sem ETag nem 304 (o hash não vaza e o AuthThrottle conta)
  logged = false;
  x = get(et.c_str());
  assert(x.code == 401 && x.hdr.empty());
  puts("painel: ETag = hash do painel, 304 só com o painel desta versão no LittleFS, carregador sem ETag, login antes");
  return 0;
}
`);
