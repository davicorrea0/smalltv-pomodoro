// Portal de Wi-Fi: compila os handlers REAIS de GET / e POST /save (extraídos do fonte) com dublês do servidor e do Wi-Fi,
// e confere o HTML (lista de redes visível, escapes, varredura em andamento, sem rede, três idiomas) e a gravação.
const fs = require('fs'), path = require('path');
const source = require('./fonte.cjs');
function between(a, b, from = 0) {
  const i = source.indexOf(a, from), j = source.indexOf(b, i + a.length);
  if (i < 0 || j < 0) throw Error('não achei: ' + a + ' ... ' + b);
  return source.slice(i, j);
}
const helpers = between('static void htmlEscapeAppend(', '\n}\n') + '\n}\n' + between('static void addTr(', 'void startPortal() {');   // htmlEscapeAppend, addTr, ADD_TR, PORTAL_HEAD
const pageLangStruct = between('struct PageLang {', '\n};') + '\n};';
const startPortal = source.indexOf('void startPortal() {');
const indexBody = between('server.on("/", HTTP_GET, [] {\n', '\n  });\n  server.on("/scan"', startPortal).replace(/^server\.on\("\/", HTTP_GET, \[\] \{\n/, '');
const saveBody = between('server.on("/save", HTTP_POST, [] {\n', '\n  });\n  // qualquer outro', startPortal).replace(/^server\.on\("\/save", HTTP_POST, \[\] \{\n/, '');
const out = process.argv[2];
if (!out) throw Error('output.cpp required');
const header = path.relative(path.dirname(path.resolve(out)), path.resolve(__dirname, '../smalltv-pomodoro/security.h')).replaceAll('\\', '/');
fs.writeFileSync(out, `#include "${header}"
#include <string>
#include <vector>
#include <map>
#include <cassert>
#include <cstdio>
#define F(s) (s)
#define FPSTR(s) (s)
#define PSTR(s) (s)
#define PROGMEM
typedef const char *PGM_P;
struct String : std::string {
 using std::string::string; using std::string::operator=;
 String(const std::string &s) : std::string(s) {}
 void reserve(size_t n) { std::string::reserve(n); }
 size_t length() const { return size(); }
};
#define strlcpy(d, s, n) (snprintf((d), (n), "%s", (s)))
const int CONTENT_LENGTH_UNKNOWN = -1, WIFI_SCAN_RUNNING = -1, WIFI_SCAN_FAILED = -2;
static uint8_t uiLang = 0, browserLang = 0;
uint8_t pageLang() { return browserLang; }
String formTokenField() { return "<input type=hidden name=csrf value=\\"tok\\">"; }
static std::vector<std::string> chunks, calls;
static std::map<std::string, std::string> headers, args;
static int status = 0;
struct { void sendHeader(const String &k, const String &v) { headers[k] = v; }
 void setContentLength(int) { calls.push_back("len"); }
 void send(int code, const char *, const char *body) { status = code; calls.push_back(std::string("send:") + body); }
 void send(int code, const char *t, const String &body) { send(code, t, body.c_str()); }
 void sendContent(const String &c) { chunks.push_back(c); }
 String arg(const char *k) { return args[k]; } } server;
struct {
 int8_t state = WIFI_SCAN_FAILED; std::vector<std::pair<std::string, int>> nets;
 int scanComplete() { return state; }
 String SSID(int i) { return nets[i].first; }
 int RSSI(int i) { return nets[i].second; }
 void setAutoReconnect(bool b) { calls.push_back(b ? "ar1" : "ar0"); }
 void persistent(bool b) { calls.push_back(b ? "p1" : "p0"); }
 void begin(const char *s, const char *p) { calls.push_back(std::string("begin:") + s + "/" + p); }
} WiFi;
bool formOk() { return true; }
bool deny() { calls.push_back("deny"); return false; }
void delay(int) {}
void safeRestart() { calls.push_back("restart"); }
${pageLangStruct}
${helpers}
void index() {
${indexBody}
}
void save() {
${saveBody}
}
static std::string page() { std::string s; for (auto &c : chunks) s += c; return s; }
static bool has(const std::string &s, const char *p) { return s.find(p) != std::string::npos; }
static void get() { chunks.clear(); calls.clear(); headers.clear(); status = 0; index(); }
int main() {
 for (uint8_t l = 0; l < 3; l++) {  // idioma da página: o do navegador; o da tela volta ao fim
  uiLang = 0; browserLang = l; WiFi.state = WIFI_SCAN_RUNNING; WiFi.nets.clear(); get();
  std::string p = page();
  assert(uiLang == 0 && status == 200 && headers["Cache-Control"] == "no-store");
  assert(calls.size() == 2 && calls[0] == "len" && calls[1] == "send:" && chunks.back().empty() && chunks[chunks.size() - 2].size() > 0 && has(p, "<form method=post action=/save>"));
  assert(has(p, l == 0 ? "Procurando redes" : l == 1 ? "Looking for networks" : "Buscando redes"));
  assert(has(p, "setTimeout") && !has(p, "type=radio") && has(p, "name=h") && has(p, "name=csrf") && has(p, "href=/scan"));
  WiFi.state = WIFI_SCAN_FAILED; get(); p = page();  // varredura que falhou, ou nenhuma rede: avisa e lembra os 2,4 GHz
  assert(has(p, "2,4 GHz") || has(p, "2.4 GHz")); assert(!has(p, "setTimeout") && !has(p, "type=radio") && has(p, "href=/scan"));
  assert(has(p, l == 0 ? "Atualizar lista" : l == 1 ? "Refresh list" : "Actualizar lista") && has(p, l == 0 ? "Senha" : l == 1 ? "Password" : "Contrase&ntilde;a"));
  WiFi.state = 0; get(); assert(has(page(), "2,4 GHz") || has(page(), "2.4 GHz"));
 }
 browserLang = 0;
 WiFi.state = 5; WiFi.nets = {{"casa", -70}, {"", -30}, {"bar<&\\"x\\">", -50}, {"casa", -60}, {"longe", -90}}; get();
 std::string p = page();
 assert(!has(p, "setTimeout") && has(p, "Escolha a sua rede") && !has(p, "datalist") && !has(p, "Nenhuma rede"));
 size_t a = p.find("value=\\"bar&lt;&amp;&quot;x&quot;&gt;\\">bar&lt;&amp;&quot;x&quot;&gt;</label>"), b = p.find("value=\\"casa\\">casa</label>"), c = p.find("value=\\"longe\\">longe</label>");
 assert(a != std::string::npos && b != std::string::npos && c != std::string::npos && a < b && b < c);  // mais forte primeiro, sem escapar errado
 assert(p.find("value=\\"casa\\"", b + 1) == std::string::npos && !has(p, "value=\\"\\""));  // sem repetida nem oculta
 assert(has(p, "<input type=radio name=s value=\\"casa\\">") && has(p, "name=s value"));
 WiFi.state = 40; WiFi.nets.clear(); for (int i = 0; i < 40; i++) WiFi.nets.push_back({"rede" + std::to_string(i), -30 - i}); get(); p = page();
 int radios = 0; for (size_t i = 0; (i = p.find("type=radio", i)) != std::string::npos; i++) radios++;
 assert(radios == 12 && has(p, "rede0\\"") && has(p, "rede11\\"") && !has(p, "rede12\\""));  // no máximo 12
 for (size_t i = 0; i + 1 < chunks.size(); i++) assert(chunks[i].size() < 1100);  // em partes, nunca a página inteira numa String
 assert(chunks.size() > 3 && chunks.back().empty());
 // POST /save: o nome digitado vale mais que a rede marcada; sem nome não grava nem reinicia
 auto post = [](const char *h, const char *s, const char *pass) { calls.clear(); args = {{"h", h}, {"s", s}, {"p", pass}}; save(); };
 post("Oculta", "Casa", "segredo12"); assert(status == 200);
 assert(calls.size() >= 5 && calls[1] == "ar1" && calls[2] == "p1" && calls[3] == "begin:Oculta/segredo12" && calls[4] == "restart");  // política volta ao padrão antes de gravar
 post("", "Casa", ""); assert(calls[3] == "begin:Casa/" && calls[4] == "restart");
 post("", "", "x"); assert(status == 400 && calls.size() == 1 && has(calls[0], "send:") && !has(calls[0], "begin"));
 post("", std::string(33, 'a').c_str(), "x"); assert(status == 400 && calls.size() == 1);
 post(std::string(32, 'a').c_str(), "", "x"); assert(status == 200 && calls[3].rfind("begin:", 0) == 0);
 for (uint8_t l = 0; l < 3; l++) { browserLang = l; uiLang = 0; post("", "", ""); assert(uiLang == 0 && has(calls[0], l == 0 ? "Escolha uma rede" : l == 1 ? "Pick a network" : "Elige una red")); }
 puts("portal: lista visível, varredura assíncrona, escapes, idiomas e gravação da rede ok");
}
`);
