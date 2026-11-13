// Teste do POST /api/apps/data (appdata.cpp) e do {x.campo} nos pacotes (facepack.cpp), com ASan/UBSan (sim/testes.sh).
// EXPERIMENTO exp/dados-ha. Cobre: os envios inválidos recusados (e o armazém intacto depois deles), os limites exatos,
// atualização parcial, atomicidade, ttl com a volta do millis() (49,7 dias) e o "ressuscitar" após 24,8 dias, o pacote
// bento-casa desenhado (valor / "--" / vencido) e a equivalência do redesenho incremental com o quadro cheio, e um fuzz
// do parser (corpos estragados; o sanitizer aborta em erro de memória e as invariantes do armazém são conferidas).
// Uso: appdata_test <pacote bento-casa.json> [casos de fuzz]
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
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
    // os 13 do plano: vazio, "{", [], ttl 5, sem values, values vazio, 5 campos, nome com maiúscula, nome de 16, valor de 16 B, array, objeto, float
    "", "{", "[]", "{\"ttl\":5,\"values\":{\"a\":1}}", "{\"ttl\":900}", "{\"ttl\":900,\"values\":{}}",
    "{\"ttl\":900,\"values\":{\"a\":1,\"b\":2,\"c\":3,\"d\":4,\"e\":5}}", "{\"ttl\":900,\"values\":{\"Temp\":1}}",
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
  CHECK(!put("{\"ttl\":900,\"values\":{\"a\":1,\"b\":2,\"c\":3,\"d\":4}}", t), "4 campos");
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
  // todo envio aceito renova o ttl do conjunto inteiro
  CHECK(get("hum", t + 60000 + 99000) == "58" && get("hum", t + 60000 + 100000) == "<nada>", "renova o ttl do conjunto");
  // 4 campos no conjunto e 1 novo que não cabe: recusa sem mudar nada
  CHECK(!put("{\"ttl\":100,\"values\":{\"d\":4}}", t + 70000), "4o campo");
  const char *e = put("{\"ttl\":100,\"values\":{\"e\":5,\"temp\":99}}", t + 70001);
  CHECK(e && *e, "5o campo distinto deveria ser recusado");
  CHECK(get("temp", t + 70002) == "23" && get("e", t + 70002) == "<nada>", "recusa por falta de espaco aplicou algo");
  // conjunto vencido é esvaziado antes de aplicar: o dado velho não ressuscita com o ttl novo
  CHECK(!put("{\"ttl\":100,\"values\":{\"novo\":1}}", t + 70000 + 200000), "6");
  CHECK(get("novo", t + 270001) == "1" && get("temp", t + 270001) == "<nada>" && get("d", t + 270001) == "<nada>", "dado vencido ressuscitou");
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
  fuzz(casos);
  delete tela;
  if (falhas) { printf("appdata_test: %d falha(s)\n", falhas); return 1; }
  printf("appdata_test: ok\n");
  return 0;
}
