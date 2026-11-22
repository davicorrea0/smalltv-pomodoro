// Teste do POST /api/apps/data (appdata.cpp) e do {x.campo} nos pacotes (facepack.cpp), com ASan/UBSan (sim/testes.sh).
// Cobre: os envios inválidos recusados (e o armazém intacto depois deles), os limites exatos (16 campos no armazém, 8 por
// envio), ttl por campo, armazém cheio, atomicidade, ttl com a volta do millis() (49,7 dias) e o "ressuscitar" após 24,8 dias, o pacote
// bento-casa desenhado (valor / "--" / vencido) e a equivalência do redesenho incremental com o quadro cheio, e um fuzz
// do parser (corpos estragados; o sanitizer aborta em erro de memória e as invariantes do armazém são conferidas).
// Uso: appdata_test <pacote bento-casa.json> [casos de fuzz]
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include <dirent.h>
#include <set>
#include "../smalltv-pomodoro/ui_kit.h"
#include "../smalltv-pomodoro/facepack.h"
#include "../smalltv-pomodoro/appdata.h"

static int falhas = 0;
#define CHECK(c, ...) do { if (!(c)) { falhas++; printf("FALHA %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static const char *put(const std::string &s, uint32_t now) { return appDataPut(s.c_str(), s.size(), now); }
static std::string get(const char *k, uint32_t now) { const char *v = appDataGet(k, now); return v ? v : "<nada>"; }

class Tela : public Adafruit_GFX {
 public:
  uint16_t px[240 * 240];
  Tela() : Adafruit_GFX(240, 240) {}
  void drawPixel(int16_t x, int16_t y, uint16_t c) override {
    if (x >= 0 && y >= 0 && x < 240 && y < 240) px[y * 240 + x] = c;
  }
};

static uint32_t semente = 0xA99DA7Au;
static uint32_t rnd(uint32_t n) {
  semente ^= semente << 13; semente ^= semente >> 17; semente ^= semente << 5;
  return n ? semente % n : 0;
}

static void invalidos() {
  appDataClear();
  const uint32_t T = 1000;
  CHECK(!put("{\"ttl\":900,\"values\":{\"temp\":22,\"hum\":58,\"porta\":\"FECHADA\"}}", T), "envio valido recusado");
  const char *ruins[] = {
    // os 13 do plano: vazio, "{", [], ttl 5, sem values, values vazio, 9 campos num envio, nome com maiúscula, nome de 16, valor de 16 B, array, objeto, float
    "", "{", "[]", "{\"ttl\":5,\"values\":{\"a\":1}}", "{\"ttl\":900}", "{\"ttl\":900,\"values\":{}}",
    "{\"ttl\":900,\"values\":{\"a\":1,\"b\":2,\"c\":3,\"d\":4,\"e\":5,\"f\":6,\"g\":7,\"h\":8,\"i\":9}}", "{\"ttl\":900,\"values\":{\"Temp\":1}}",
    "{\"ttl\":900,\"values\":{\"abcdefghijklmnop\":1}}", "{\"ttl\":900,\"values\":{\"a\":\"0123456789abcdef\"}}",
    "{\"ttl\":900,\"values\":{\"a\":[1]}}", "{\"ttl\":900,\"values\":{\"a\":{\"b\":1}}}", "{\"ttl\":900,\"values\":{\"a\":1.5}}",
    // além dos 13
    "null", "{\"ttl\":900,\"values\":{\"abcdefghi\":1}}",
    "{\"ttl\":900,\"values\":{\"a\":null}}", "{\"ttl\":900,\"values\":{\"a\":true}}", "{\"ttl\":900,\"values\":{\"\":1}}",
    "{\"ttl\":900,\"values\":{\"a-b\":1}}", "{\"ttl\":900,\"values\":{\"a\":1},\"x\":1}", "{\"ttl\":9.5,\"values\":{\"a\":1}}",
    "{\"ttl\":\"900\",\"values\":{\"a\":1}}", "{\"ttl\":900,\"values\":[1]}", "{\"ttl\":900,\"values\":\"a\"}", "{\"ttl\":86401,\"values\":{\"a\":1}}",
    "{\"ttl\":-10,\"values\":{\"a\":1}}", "{\"ttl\":900,\"values\":{\"a\":2147483648}}", "{\"ttl\":900,\"values\":{\"a\":1e3}}",
    "{\"ttl\":900,\"values\":{\"ok\":1,\"mau\":1.5}}",  // um campo ruim não aplica o bom
    "{\"ttl\":900,\"values\":{\"temp\":99,\"A\":1}}",
  };
  for (const char *r : ruins) {
    const char *e = put(r, T + 5);
    CHECK(e && *e, "deveria recusar com motivo: %s", r);
  }
  CHECK(get("temp", T + 10) == "22" && get("hum", T + 10) == "58" && get("porta", T + 10) == "FECHADA", "recusas mexeram no armazem");
  CHECK(get("ok", T + 10) == "<nada>" && get("a", T + 10) == "<nada>", "campo bom de um envio ruim foi aplicado");
  std::string grande = "{\"ttl\":900,\"values\":{\"temp\":23}}";
  grande.append(APPDATA_BODY_MAX + 1 - grande.size(), ' ');  // 513 B, JSON válido
  CHECK(grande.size() == APPDATA_BODY_MAX + 1 && put(grande, T + 20), "513 B deveria ser recusado");
  CHECK(get("temp", T + 20) == "22", "corpo de 513 B mexeu no armazem");
}

static void limites() {
  appDataClear();
  uint32_t t = 5;
  CHECK(!put("{\"ttl\":10,\"values\":{\"a\":1}}", t), "ttl 10");
  CHECK(!put("{\"ttl\":86400,\"values\":{\"a\":1}}", t), "ttl 86400");
  CHECK(put("{\"ttl\":9,\"values\":{\"a\":1}}", t), "ttl 9");
  CHECK(!put("{\"ttl\":900,\"values\":{\"abcdefgh\":\"0123456789abcde\"}}", t), "nome de 8 e valor de 15 B");
  CHECK(get("abcdefgh", t) == "0123456789abcde", "valor de 15 B");
  CHECK(!put("{\"ttl\":900,\"values\":{\"a_1\":-2147483647,\"b\":2147483647}}", t), "inteiros extremos");
  CHECK(get("a_1", t) == "-2147483647" && get("b", t) == "2147483647", "inteiros guardados como texto");
  appDataClear();
  CHECK(!put("{\"ttl\":900,\"values\":{\"a\":1,\"b\":2,\"c\":3,\"d\":4,\"e\":5,\"f\":6,\"g\":7,\"h\":8}}", t), "8 campos num envio");
  CHECK(!put("{\"ttl\":900,\"values\":{\"i\":1,\"j\":2,\"k\":3,\"l\":4,\"m\":5,\"n\":6,\"o\":7,\"p\":8}}", t), "16 no armazem");
  CHECK(get("a", t) == "1" && get("p", t) == "8", "16 campos guardados");
  CHECK(!put("{\"ttl\":900,\"values\":{\"a\":11,\"p\":88}}", t), "armazem cheio, mas so campos que ja existem");
  CHECK(put("{\"ttl\":900,\"values\":{\"a\":12,\"q\":1}}", t), "17o campo deveria ser recusado");
  CHECK(get("a", t) == "11" && get("q", t) == "<nada>", "recusa por armazem cheio aplicou algo");
  appDataClear();
  std::string s = "{\"ttl\":900,\"values\":{\"a\":\"";
  s += std::string(APPDATA_BODY_MAX - s.size() - 3, 'z');  // 512 B exatos; o valor passa de 15: recusa pelo valor, não pelo corpo
  s += "\"}}";
  CHECK(s.size() == APPDATA_BODY_MAX, "montagem do corpo de 512 B (%zu)", s.size());
  const char *e = put(s, t);
  CHECK(e && strstr(e, "valor"), "corpo de 512 B com valor grande: %s", e ? e : "(aceito)");
  std::string ok = "{\"ttl\":900,\"values\":{\"a\":1}}";
  ok.append(APPDATA_BODY_MAX - ok.size(), ' ');
  CHECK(!put(ok, t), "corpo de 512 B valido");
  CHECK(!put("{\"ttl\":900,\"values\":{\"a\":\"\\u00e7\"}}", t), "UTF-8 curto");
}

static void parcial_e_atomico() {
  appDataClear();
  uint32_t t = 100000;
  CHECK(!put("{\"ttl\":100,\"values\":{\"temp\":22,\"hum\":58}}", t), "1");
  CHECK(!put("{\"ttl\":100,\"values\":{\"porta\":\"ABERTA\"}}", t + 50000), "2");  // parcial: conserva temp e hum
  CHECK(get("temp", t + 50001) == "22" && get("hum", t + 50001) == "58" && get("porta", t + 50001) == "ABERTA", "parcial conserva os outros");
  CHECK(!put("{\"ttl\":100,\"values\":{\"temp\":23}}", t + 60000), "3");
  CHECK(get("temp", t + 60001) == "23" && get("hum", t + 60001) == "58", "troca de um campo");
  // ttl por campo: cada um vence no prazo do envio que o gravou (hum em t+100 s, porta em t+150 s, temp em t+160 s)
  CHECK(get("hum", t + 99999) == "58" && get("hum", t + 100000) == "<nada>", "hum vence no seu prazo");
  CHECK(get("porta", t + 149999) == "ABERTA" && get("porta", t + 150000) == "<nada>", "porta vence no seu prazo");
  CHECK(get("temp", t + 159999) == "23" && get("temp", t + 160000) == "<nada>", "temp renovado pelo envio 3");
  // dois remetentes dividem o armazém: um não apaga o outro
  appDataClear();
  CHECK(!put("{\"ttl\":900,\"values\":{\"estado\":\"LAVANDO\",\"pct\":70,\"min\":18}}", t), "lavadora");
  CHECK(!put("{\"ttl\":900,\"values\":{\"tk_c5\":\"42% 2h13\",\"tk_c7\":\"9%\"}}", t + 10), "tokensmall");
  CHECK(!put("{\"ttl\":900,\"values\":{\"estado\":\"PRONTA\",\"fim\":\"14:52\"}}", t + 20), "campo novo de um remetente");
  CHECK(get("estado", t + 30) == "PRONTA" && get("fim", t + 30) == "14:52" && get("pct", t + 30) == "70" && get("tk_c5", t + 30) == "42% 2h13",
        "os campos dos outros remetentes ficam");
  // um campo vencido libera o lugar (e não volta com o envio seguinte)
  appDataClear();
  CHECK(!put("{\"ttl\":10,\"values\":{\"a\":1,\"b\":2,\"c\":3,\"d\":4,\"e\":5,\"f\":6,\"g\":7,\"h\":8}}", t), "8 curtos");
  CHECK(!put("{\"ttl\":900,\"values\":{\"i\":1,\"j\":2,\"k\":3,\"l\":4,\"m\":5,\"n\":6,\"o\":7,\"p\":8}}", t), "8 longos");
  CHECK(put("{\"ttl\":900,\"values\":{\"novo\":1}}", t + 9999), "ainda cheio");
  CHECK(!put("{\"ttl\":900,\"values\":{\"novo\":1}}", t + 10000), "vencidos liberam lugar");
  CHECK(get("novo", t + 10001) == "1" && get("a", t + 10001) == "<nada>" && get("p", t + 10001) == "8", "lugar reaproveitado");
  CHECK(!put("{\"ttl\":900,\"values\":{\"a\":9}}", t + 10002), "a de novo");
  CHECK(get("a", t + 10003) == "9" && get("b", t + 10003) == "<nada>", "o vencido nao ressuscitou");
  // appDataAt lista só os vivos, com o tempo que falta
  int vivos = 0;
  const char *k, *v;
  uint32_t left;
  for (int i = 0; appDataAt(i, t + 10003, &k, &v, &left); i++) {
    vivos++;
    CHECK(left > 0 && left <= 900, "appDataAt: %s com %u s", k, (unsigned)left);
  }
  CHECK(vivos == 10, "appDataAt: %d vivos (esperado 10)", vivos);
}

static void volta_do_millis() {
  appDataClear();
  uint32_t t0 = 0xFFFFFF00u;
  CHECK(!put("{\"ttl\":600,\"values\":{\"a\":7}}", t0), "envio perto da volta");
  CHECK(get("a", t0) == "7", "no instante do envio");
  CHECK(get("a", t0 + 1000) == "7", "1 s depois (ja deu a volta)");
  CHECK(get("a", t0 + 599000) == "7", "599 s");
  CHECK(get("a", t0 + 600000) == "<nada>", "600 s: vencido");
  CHECK(get("a", t0 + 601000) == "<nada>", "601 s");
  // sem o appDataExpire, passados 2^31 ms o int32 voltaria a ser positivo e o dado reviveria
  uint32_t longe = t0 + 600000u + 0x80000000u + 5000u;
  appDataExpire(t0 + 700000);
  CHECK(get("a", longe) == "<nada>", "dado vencido ressuscitou apos 24,8 dias");
  // o ttl máximo (24 h) cabe com folga na faixa com sinal
  CHECK(!put("{\"ttl\":86400,\"values\":{\"a\":1}}", 0x7FFFFF00u), "ttl maximo");
  CHECK(get("a", 0x7FFFFF00u + 86399000u) == "1" && get("a", 0x7FFFFF00u + 86400000u) == "<nada>", "ttl maximo na virada do sinal");
  CHECK(!put("{\"ttl\":10,\"values\":{\"a\":2}}", 3), "envio apos a volta");
  CHECK(get("a", 5000) == "2", "valor apos a volta");
}

static uint32_t hash(const uint16_t *p) { uint32_t h = 2166136261u; for (int i = 0; i < 240 * 240; i++) h = (h ^ p[i]) * 16777619u; return h; }

static bool carregar(const char *arq) {
  FILE *f = fopen(arq, "rb");
  if (!f) { printf("appdata_test: sem %s\n", arq); return false; }
  std::string s;
  char b[4096];
  for (size_t k; (k = fread(b, 1, sizeof b, f));) s.append(b, k);
  fclose(f);
  std::vector<char> buf(s.begin(), s.end());
  buf.push_back(0);
  char err[96] = "";
  if (!facePackLoad(buf.data(), s.size(), err, sizeof err)) { printf("appdata_test: bento-casa recusado: %s\n", err); return false; }
  return true;
}

static UiState estado() {
  UiState st;
  memset(&st, 0, sizeof st);
  st.theme = THEME_PACK;
  st.timeOk = true;
  st.now.tm_hour = 14; st.now.tm_min = 5; st.now.tm_sec = 9; st.now.tm_mday = 13; st.now.tm_mon = 10; st.now.tm_wday = 5; st.now.tm_year = 126;
  st.accent = 0xFFFF; st.city = "Cidade"; st.ip = "10.0.0.1";
  return st;
}

// o pacote desenhado: sem dados, com dados, parcial, vencido; e o incremental igual ao quadro cheio em cada passo
static void pacote(Tela *tela, const char *arq) {
  if (!carregar(arq)) { falhas++; return; }
  appDataClear();
  UiState st = estado();
  st.ms = 4000000000u;  // perto da volta do millis()
  uiSetLang(0);
  facePackRender(st, true);
  uint32_t semDados = hash(tela->px);
  struct Passo { const char *corpo; uint32_t dt; };
  const Passo passos[] = {
    {"{\"ttl\":120,\"values\":{\"temp\":22,\"hum\":58,\"porta\":\"FECHADA\"}}", 300},
    {nullptr, 700},
    {"{\"ttl\":120,\"values\":{\"porta\":\"ABERTA\"}}", 100},
    {"{\"ttl\":120,\"values\":{\"hum\":61}}", 100},
    {nullptr, 119000},  // quase vence
    {"{\"ttl\":10,\"values\":{\"temp\":-3}}", 2000},
    {nullptr, 12000},   // vencido: tudo "--"
    {nullptr, 100},
  };
  uint32_t comDados = 0, vencido = 0;
  int i = 0;
  for (const Passo &p : passos) {
    if (p.corpo) { const char *e = put(p.corpo, st.ms); CHECK(!e, "passo %d recusado: %s", i, e ? e : ""); }
    st.ms += p.dt;
    appDataExpire(st.ms);
    facePackRender(st, false);  // incremental
    uint32_t inc = hash(tela->px);
    facePackRender(st, true);   // quadro cheio do mesmo estado
    uint32_t cheio = hash(tela->px);
    CHECK(inc == cheio, "passo %d: incremental != quadro cheio (areas sujas perderam uma mudanca)", i);
    if (i == 0) comDados = cheio;
    if (i == 6) vencido = cheio;
    i++;
  }
  CHECK(comDados != semDados, "os valores nao mudaram a tela");
  CHECK(vencido == semDados, "vencido deveria ficar igual a sem dados (tudo '--')");
  // idiomas e hora ausente também têm de bater
  for (int lang = 0; lang < 3; lang++) {
    uiSetLang(lang);
    st.timeOk = lang != 1;
    put("{\"ttl\":100,\"values\":{\"temp\":20,\"hum\":50,\"porta\":\"ABERTA\"}}", st.ms);
    st.ms += 50;
    facePackRender(st, false);
    uint32_t inc = hash(tela->px);
    facePackRender(st, true);
    CHECK(inc == hash(tela->px), "idioma %d: incremental != cheio", lang);
  }
  uiSetLang(0);
  facePackUnload();
}

// "when" e "value" ligados a campos: cada camada aparece ou some conforme o dado, a barra acompanha o número, e o
// incremental bate com o quadro cheio em cada passo. Referências inválidas recusam o pacote na carga.
static bool carregarTexto(const std::string &js, char *err, size_t n) {
  std::vector<char> buf(js.begin(), js.end());
  buf.push_back(0);
  return facePackLoad(buf.data(), js.size(), err, n);
}

static void referencias(Tela *tela) {
  const std::string js =
    "{\"format\":2,\"id\":\"ref\",\"bg\":\"#000000\",\"layers\":["
    "{\"type\":\"rect\",\"x\":0,\"y\":0,\"w\":10,\"h\":10,\"color\":\"#ff0000\",\"when\":\"x.a=ON\"},"
    "{\"type\":\"rect\",\"x\":20,\"y\":0,\"w\":10,\"h\":10,\"color\":\"#00ff00\",\"when\":\"!x.a=ON\"},"
    "{\"type\":\"rect\",\"x\":40,\"y\":0,\"w\":10,\"h\":10,\"color\":\"#0000ff\",\"when\":\"x.b\"},"
    "{\"type\":\"rect\",\"x\":60,\"y\":0,\"w\":10,\"h\":10,\"color\":\"#ffff00\",\"when\":\"!x.b\"},"
    "{\"type\":\"bar\",\"x\":0,\"y\":50,\"w\":100,\"h\":10,\"value\":\"x.p\",\"max\":100,\"color\":\"#ffffff\",\"track\":\"#404040\"},"
    "{\"type\":\"text\",\"x\":0,\"y\":80,\"font\":\"pixel\",\"size\":2,\"color\":\"#ffffff\",\"text\":\"{x.a} {x.p}\",\"bg\":\"#000000\",\"when\":\"x.a\"}"
    "]}";
  char err[96] = "";
  if (!carregarTexto(js, err, sizeof err)) { CHECK(false, "pacote de referencias recusado: %s", err); return; }
  const uint16_t VERM = uiRgb(255, 0, 0), VERDE = uiRgb(0, 255, 0), AZUL = uiRgb(0, 0, 255), AMAR = uiRgb(255, 255, 0);
  const uint16_t BRANCO = uiRgb(255, 255, 255), TRILHO = uiRgb(64, 64, 64), FUNDO = 0;
  auto px = [&](int x, int y) { return tela->px[y * 240 + x]; };
  appDataClear();
  UiState st = estado();
  st.ms = 0xFFFFF000u;  // atravessa a volta do millis() no meio do teste
  facePackRender(st, true);
  CHECK(px(5, 5) == FUNDO && px(25, 5) == VERDE && px(45, 5) == FUNDO && px(65, 5) == AMAR, "sem dados: so os negados");
  CHECK(px(10, 55) == TRILHO, "sem dados: barra vazia");
  struct Passo { const char *corpo; uint16_t a, na, b, nb; int cheio, vazio; };  // cores esperadas; colunas da barra
  const Passo passos[] = {
    {"{\"ttl\":60,\"values\":{\"a\":\"ON\",\"b\":\"1\",\"p\":50}}", VERM, FUNDO, AZUL, FUNDO, 40, 60},
    {"{\"ttl\":60,\"values\":{\"a\":\"OFF\",\"b\":\"0\",\"p\":100}}", FUNDO, VERDE, FUNDO, AMAR, 95, -1},
    {"{\"ttl\":60,\"values\":{\"a\":\"on\",\"b\":\"\",\"p\":-7}}", FUNDO, VERDE, FUNDO, AMAR, -1, 5},
    {"{\"ttl\":60,\"values\":{\"a\":\"ON\",\"b\":\"sim\",\"p\":250}}", VERM, FUNDO, AZUL, FUNDO, 95, -1},
    {"{\"ttl\":60,\"values\":{\"p\":\"texto\"}}", VERM, FUNDO, AZUL, FUNDO, -1, 5},
  };
  int i = 0;
  for (const Passo &p : passos) {
    const char *e = put(p.corpo, st.ms);
    CHECK(!e, "referencias, passo %d recusado: %s", i, e ? e : "");
    st.ms += 1000;
    appDataExpire(st.ms);
    facePackRender(st, false);
    uint32_t inc = hash(tela->px);
    CHECK(px(5, 5) == p.a && px(25, 5) == p.na && px(45, 5) == p.b && px(65, 5) == p.nb, "referencias, passo %d: when errado", i);
    if (p.cheio >= 0) CHECK(px(p.cheio, 55) == BRANCO, "referencias, passo %d: barra deveria estar cheia em x=%d", i, p.cheio);
    if (p.vazio >= 0) CHECK(px(p.vazio, 55) == TRILHO, "referencias, passo %d: barra deveria estar vazia em x=%d", i, p.vazio);
    facePackRender(st, true);
    CHECK(inc == hash(tela->px), "referencias, passo %d: incremental != quadro cheio", i);
    i++;
  }
  st.ms += 61000;  // vence: volta tudo ao "sem dados"
  appDataExpire(st.ms);
  facePackRender(st, false);
  CHECK(px(5, 5) == FUNDO && px(25, 5) == VERDE && px(45, 5) == FUNDO && px(65, 5) == AMAR && px(10, 55) == TRILHO,
        "vencido: deveria voltar ao sem dados");
  facePackUnload();

  // carga: referências inválidas recusam o pacote; 8 diferentes cabem (repetidas não contam); formato 1 recusa
  auto pacoteCom = [](int fmt, const std::string &camadas) {
    return "{\"format\":" + std::to_string(fmt) + ",\"id\":\"r\",\"layers\":[" + camadas + "]}";
  };
  auto rect = [](const std::string &when) { return "{\"type\":\"rect\",\"w\":5,\"h\":5,\"when\":\"" + when + "\"}"; };
  const char *ruins[] = {"x.", "x.Abc", "x.a-b", "x.abcdefghi", "x.a=0123456789abcdef", "!day", "!", "!x.", "xa", "y.a"};
  for (const char *w : ruins) {
    CHECK(!carregarTexto(pacoteCom(2, rect(w)), err, sizeof err), "when \"%s\" deveria recusar", w);
    facePackUnload();
  }
  CHECK(!carregarTexto(pacoteCom(2, "{\"type\":\"text\",\"text\":\"a\",\"value\":\"x.a\"}"), err, sizeof err), "value x. em texto deveria recusar");
  CHECK(!carregarTexto(pacoteCom(2, "{\"type\":\"bar\",\"w\":9,\"h\":3,\"value\":\"x.A\"}"), err, sizeof err), "value x.A deveria recusar");
  CHECK(!carregarTexto(pacoteCom(1, rect("x.a")), err, sizeof err) && strstr(err, "format"), "formato 1 com x. deveria pedir o 2: %s", err);
  std::string oito, nove;
  for (int k = 0; k < 8; k++) oito += (k ? "," : "") + rect("x.c" + std::to_string(k)) + "," + rect("!x.c" + std::to_string(k));
  nove = oito + "," + rect("x.c8");
  CHECK(carregarTexto(pacoteCom(2, oito), err, sizeof err), "8 referencias (e as negadas repetidas) deveriam caber: %s", err);
  facePackUnload();
  CHECK(!carregarTexto(pacoteCom(2, nove), err, sizeof err), "9 referencias deveriam recusar");
  facePackUnload();
  CHECK(carregarTexto(pacoteCom(2, rect("x.a=") + "," + rect("x.abcdefgh=0123456789abcde")), err, sizeof err), "x.a= e limites exatos: %s", err);
  facePackUnload();
}

// a lista de campos de fora de cada pacote (4ª linha do .txt e "x" do /api/faces): when/value e {x.campo}, sem repetir
static void campos() {
  char err[96], l[100];
  auto carrega = [&](const std::string &camadas) {
    std::string js = "{\"format\":2,\"id\":\"c\",\"layers\":[" + camadas + "]}";
    std::vector<char> b(js.begin(), js.end());
    b.push_back(0);
    bool ok = facePackLoad(b.data(), js.size(), err, sizeof err);
    CHECK(ok, "campos: carga recusada: %s", err);
    facePackFields(l, sizeof l);
    facePackUnload();
    return std::string(l);
  };
  auto texto = [](const char *t) { return std::string("{\"type\":\"text\",\"text\":\"") + t + "\"}"; };
  CHECK(carrega(texto("{HH}:{MM}")) == "", "mostrador comum nao tem campos");
  CHECK(carrega(texto("{x.temp} C") + "," + texto("{x.hum}% {x.temp}")) == "temp,hum", "texto: %s", l);
  CHECK(carrega("{\"type\":\"rect\",\"w\":5,\"h\":5,\"when\":\"!x.lv_est=PRONTA\"}," + texto("{x.lv_est}") +
                ",{\"type\":\"bar\",\"w\":9,\"h\":3,\"value\":\"x.lv_pct\"}") == "lv_est,lv_pct", "when/value: %s", l);
  CHECK(carrega(texto("{x.} {x.Abc} {x.abcdefghi}") + "," + texto("{x.a-b} x.solto {x.ok}")) == "ok", "chaves invalidas: %s", l);
  // não passa do tamanho dado
  facePackFields(l, 1);
  CHECK(l[0] == 0, "sem pacote: vazio");
}

// o leitor do .json (FieldScan, para os .txt de antes da 4ª linha) acha os mesmos campos que o motor em cada pacote
static std::set<std::string> lista(const char *l) {
  std::set<std::string> r;
  for (const char *p = l; *p;) { const char *e = strchr(p, ','); r.insert(std::string(p, e ? e - p : strlen(p))); p += (e ? e - p + 1 : strlen(p)); }
  return r;
}
static void varredura() {
  int n = 0, iguais = 0;
  for (const char *pasta : {"../faces", "apps-seeds"}) {
    DIR *d = opendir(pasta);
    for (struct dirent *e; d && (e = readdir(d));) {
      std::string nome = e->d_name;
      if (nome.size() < 6 || nome.substr(nome.size() - 5) != ".json" || nome == "catalog.json") continue;
      FILE *f = fopen((std::string(pasta) + "/" + nome).c_str(), "rb");
      std::string s;
      char b[4096];
      for (size_t k; f && (k = fread(b, 1, sizeof b, f));) s.append(b, k);
      if (f) fclose(f);
      FieldScan sc;
      sc.begin();
      for (char c : s) sc.feed(c);
      std::vector<char> buf(s.begin(), s.end());
      buf.push_back(0);
      char err[96], l[100];
      if (!facePackLoad(buf.data(), s.size(), err, sizeof err)) continue;
      facePackFields(l, sizeof l);
      facePackUnload();
      n++;
      std::set<std::string> m = lista(l), v = lista(sc.out);
      for (const std::string &k : m) CHECK(v.count(k), "varredura de %s perdeu %s (motor: %s, varredura: %s)", nome.c_str(), k.c_str(), l, sc.out);
      if (m == v) iguais++;
      else printf("appdata_test: varredura de %s achou a mais: %s (motor: %s)\n", nome.c_str(), sc.out, l);
    }
    if (d) closedir(d);
  }
  CHECK(n > 40, "varredura: poucos pacotes (%d)", n);
  // casos à mão: chave longa, maiúscula, sem fechar, "!x.", "=valor", repetido
  FieldScan sc;
  sc.begin();
  const char *t = "{\"layers\":[{\"text\":\"{x.temp} {x.Temp} {x.nomegrandedemais} {x.ab\"},{\"when\":\"!x.lv_est=PRONTA\"},"
                  "{\"value\":\"x.pct\"},{\"when\":\"x.temp\"},{\"text\":\"ax.no x.no\"}]}";
  for (const char *p = t; *p; p++) sc.feed(*p);
  CHECK(!strcmp(sc.out, "temp,lv_est,pct"), "varredura a mao: %s", sc.out);
  printf("appdata_test: varredura do .json igual ao motor em %d de %d pacotes\n", iguais, n);
}

// os miniapps de docs/exemplos: cada um carrega e, numa sequência de envios como a do Home Assistant (estados que
// trocam, campos que vencem, os três dividindo o armazém), o incremental bate com o quadro cheio e a tela muda
static void exemplos(Tela *tela) {
  struct Caso { const char *arq; const char *envios[4]; };
  const Caso casos[] = {
    {"../docs/exemplos/casa-lavadora.json",
     {"{\"ttl\":60,\"values\":{\"lv_est\":\"LAVANDO\",\"lv_txt\":\"ENXAGUE\",\"lv_pct\":70,\"lv_min\":18}}",
      "{\"ttl\":60,\"values\":{\"lv_est\":\"PRONTA\",\"lv_txt\":\"AS 14:52\",\"lv_pct\":100,\"lv_min\":0}}",
      "{\"ttl\":60,\"values\":{\"lv_est\":\"DESLIGADA\",\"lv_txt\":\"\",\"lv_pct\":0,\"lv_min\":0}}", nullptr}},
    {"../docs/exemplos/casa-luzes.json",
     {"{\"ttl\":60,\"values\":{\"lz_n\":2,\"lz_1\":\"ACESA\",\"lz_2\":\"\",\"lz_3\":\"ACESA\"}}",
      "{\"ttl\":60,\"values\":{\"lz_n\":0,\"lz_1\":\"\",\"lz_2\":\"\",\"lz_3\":\"\"}}",
      "{\"ttl\":60,\"values\":{\"lz_n\":1,\"lz_2\":\"ACESA\"}}", nullptr}},
    {"../docs/exemplos/casa-tomadas.json",
     {"{\"ttl\":60,\"values\":{\"tm_1\":1,\"tm_w1\":640,\"tm_2\":0,\"tm_w2\":0}}",
      "{\"ttl\":60,\"values\":{\"tm_1\":0,\"tm_w1\":0,\"tm_2\":1,\"tm_w2\":9}}", nullptr, nullptr}},
  };
  appDataClear();
  UiState st = estado();
  st.ms = 0xFFFF0000u;  // atravessa a volta do millis()
  for (const Caso &c : casos) {
    if (!carregar(c.arq)) { falhas++; continue; }
    facePackRender(st, true);
    uint32_t antes = hash(tela->px), primeiro = 0;
    for (int i = 0; i < 4 && c.envios[i]; i++) {
      const char *e = put(c.envios[i], st.ms);
      CHECK(!e, "%s, envio %d recusado: %s", c.arq, i, e ? e : "");
      st.ms += 1000;
      appDataExpire(st.ms);
      facePackRender(st, false);
      uint32_t inc = hash(tela->px);
      facePackRender(st, true);
      CHECK(inc == hash(tela->px), "%s, envio %d: incremental != quadro cheio", c.arq, i);
      if (i == 0) primeiro = inc;
    }
    CHECK(primeiro != antes, "%s: os valores nao mudaram a tela", c.arq);
    st.ms += 61000;  // tudo vence: volta ao desenho sem dados
    appDataExpire(st.ms);
    facePackRender(st, false);
    uint32_t inc = hash(tela->px);
    facePackRender(st, true);
    CHECK(inc == hash(tela->px) && inc == antes, "%s: vencido deveria voltar ao sem dados", c.arq);
    facePackUnload();
  }
  // os três juntos cabem no armazém (12 dos 16 campos)
  appDataClear();
  for (const Caso &c : casos) CHECK(!put(c.envios[0], st.ms), "%s nao coube junto com os outros", c.arq);
}

static const char *PARTES[] = {"ttl", "values", "temp", "hum", "porta", "ABERTA", "a", "x", "900", "10", "86400", "-1", "1.5", "null", "[]", "{}", "\"\"", "true"};

static void fuzz(long casos) {
  appDataClear();
  const std::string base[] = {
    "{\"ttl\":900,\"values\":{\"temp\":22,\"hum\":58,\"porta\":\"FECHADA\"}}",
    "{\"ttl\":10,\"values\":{\"a\":1}}",
    "{\"ttl\":86400,\"values\":{\"abcdefgh\":\"0123456789abcde\",\"b\":-5,\"c_1\":0,\"d\":\"\"}}",
  };
  static const char *const NOMES[] = {"temp", "hum", "porta", "a", "abcdefgh", "b", "c_1", "d"};
  long aceitos = 0;
  uint32_t now = 1;
  for (long c = 0; c < casos; c++) {
    std::string s = base[rnd(3)];
    for (int k = 1 + rnd(4); k-- && !s.empty();) {
      size_t a = rnd(s.size());
      switch (rnd(6)) {
        case 0: s.resize(a); break;
        case 1: s[a] = "{}[]\",:0123456789-.aztx #\\u"[rnd(27)]; break;
        case 2: s.erase(a, 1 + rnd(6)); break;
        case 3: s.insert(a, PARTES[rnd(sizeof PARTES / sizeof *PARTES)]); break;
        case 4: s.insert(a, s.substr(rnd(s.size()), 1 + rnd(60))); break;
        default: s[a] = (char)rnd(256); break;
      }
    }
    if (rnd(40) == 0) s.append(APPDATA_BODY_MAX + rnd(64), ' ');
    now += rnd(3) ? rnd(5000) : rnd(0x40000000u);
    const char *e = put(s, now);
    if (!e) {
      aceitos++;
      CHECK(s.size() <= APPDATA_BODY_MAX, "aceitou corpo de %zu B", s.size());
    } else CHECK(*e, "recusa sem motivo");
    for (const char *k : NOMES) {
      const char *v = appDataGet(k, now);
      CHECK(!v || strlen(v) <= (size_t)APPDATA_VALUE, "valor grande demais guardado em %s", k);
    }
  }
  printf("appdata_test: fuzz do parser: %ld casos, %ld aceitos\n", casos, aceitos);
}

int main(int argc, char **argv) {
  const char *arq = argc > 1 ? argv[1] : "apps-seeds/bento-casa.json";
  long casos = argc > 2 ? atol(argv[2]) : 5000;
  Tela *tela = new Tela();
  uiBegin(tela);
  invalidos();
  limites();
  parcial_e_atomico();
  volta_do_millis();
  pacote(tela, arq);
  referencias(tela);
  campos();
  varredura();
  exemplos(tela);
  fuzz(casos);
  delete tela;
  if (falhas) { printf("appdata_test: %d falha(s)\n", falhas); return 1; }
  printf("appdata_test: ok\n");
  return 0;
}
