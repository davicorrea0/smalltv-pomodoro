// Teste do pacote tokensmall (sim/apps-seeds/tokensmall.json) com os dados que o companheiro do PC empurra
// (miniapps/tokensmall), com ASan/UBSan (sim/testes.sh). Confere: o pacote carrega; sem dados, com os 4 campos,
// com uma janela que passa a "--" (o envio seguinte do companheiro) e vencido; em cada passo o redesenho incremental
// dá o mesmo quadro que o quadro cheio, nos 3 idiomas e sem hora; e que cada campo muda só a sua linha.
// Uso: tokensmall_test <pacote tokensmall.json>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <string>
#include <vector>
#include <ArduinoJson.h>
#include "../smalltv-pomodoro/ui_kit.h"
#include "../smalltv-pomodoro/facepack.h"
#include "../smalltv-pomodoro/appdata.h"

static int falhas = 0;
#define CHECK(c, ...) do { if (!(c)) { falhas++; printf("FALHA %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

class Tela : public Adafruit_GFX {
 public:
  uint16_t px[240 * 240];
  Tela() : Adafruit_GFX(240, 240) {}
  void drawPixel(int16_t x, int16_t y, uint16_t c) override {
    if (x >= 0 && y >= 0 && x < 240 && y < 240) px[y * 240 + x] = c;
  }
};

static uint32_t hash(const uint16_t *p) { uint32_t h = 2166136261u; for (int i = 0; i < 240 * 240; i++) h = (h ^ p[i]) * 16777619u; return h; }
static const char *put(const char *s, uint32_t now) { return appDataPut(s, strlen(s), now); }

static bool carregar(const char *arq) {
  FILE *f = fopen(arq, "rb");
  if (!f) { printf("tokensmall_test: sem %s\n", arq); return false; }
  std::string s;
  char b[4096];
  for (size_t k; (k = fread(b, 1, sizeof b, f));) s.append(b, k);
  fclose(f);
  std::vector<char> buf(s.begin(), s.end());
  buf.push_back(0);
  char err[96] = "";
  if (!facePackLoad(buf.data(), s.size(), err, sizeof err)) { printf("tokensmall_test: pacote recusado: %s\n", err); return false; }
  // contrato congelado com o companheiro (miniapps/tokensmall): redesenhar a tela não pode mudar a lista de campos
  char campos[96];
  facePackFields(campos, sizeof campos);
  std::vector<std::string> v;
  for (const char *p = campos; *p;) { const char *e = strchr(p, ','); v.push_back(e ? std::string(p, e - p) : std::string(p)); p += e ? (e - p) + 1 : strlen(p); }
  std::sort(v.begin(), v.end());
  std::string r;
  for (const std::string &x : v) r += (r.empty() ? "" : ",") + x;
  CHECK(r == "tk_c5,tk_c5r,tk_c7,tk_x5,tk_x5r,tk_x7", "campos lidos '%s', esperado tk_c5,tk_c5r,tk_c7,tk_x5,tk_x5r,tk_x7 (contrato com o companheiro)", campos);
  return true;
}

static UiState estado() {
  UiState st;
  memset(&st, 0, sizeof st);
  st.theme = THEME_PACK;
  st.timeOk = true;
  st.now.tm_hour = 14; st.now.tm_min = 37; st.now.tm_sec = 9; st.now.tm_mday = 16; st.now.tm_mon = 10; st.now.tm_wday = 1; st.now.tm_year = 126;
  st.accent = 0xFFFF; st.city = "Cidade"; st.ip = "10.0.0.1";
  return st;
}

static bool lerCorpos(const char *arq, std::vector<std::string> &corpos) {
  FILE *f = fopen(arq, "rb");
  if (!f) { printf("tokensmall_test: sem %s\n", arq); return false; }
  std::string texto; char bloco[4096];
  for (size_t n; (n = fread(bloco, 1, sizeof bloco, f));) texto.append(bloco, n);
  fclose(f);
  JsonDocument doc;
  DeserializationError erro = deserializeJson(doc, texto);
  if (erro) { printf("tokensmall_test: estados invalidos: %s\n", erro.c_str()); return false; }
  for (JsonObject estado : doc["estados"].as<JsonArray>()) {
    JsonArray envios = estado["envios"].as<JsonArray>();
    std::string corpo;
    if (!envios.isNull() && envios.size()) serializeJson(envios[0], corpo);
    corpos.push_back(corpo);
  }
  return corpos.size() >= 4 && !corpos[1].empty() && !corpos[2].empty() && !corpos[3].empty();
}

static std::string trocar(const std::string &corpo, const char *campo, const char *valor) {
  JsonDocument doc;
  if (deserializeJson(doc, corpo)) return std::string();
  doc["values"][campo] = valor;
  std::string alterado;
  serializeJson(doc, alterado);
  return alterado;
}

int main(int argc, char **argv) {
  const char *arq = argc > 1 ? argv[1] : "apps-seeds/tokensmall.json";
  std::vector<std::string> corpos;
  if (!lerCorpos("miniapps-estados/tokensmall.json", corpos)) return 1;
  Tela *tela = new Tela();
  uiBegin(tela);
  if (!carregar(arq)) return 1;
  appDataClear();
  UiState st = estado();
  st.ms = 4000000000u;  // perto da volta do millis()
  uiSetLang(0);
  facePackRender(st, true);
  const uint32_t semDados = hash(tela->px);

  // o que o companheiro manda: sempre os 6 campos ("" nos dois campos do 5H sem leitura, "--" no 7D)
  struct Passo { std::string corpo; uint32_t dt; };
  const Passo passos[] = {
    {corpos[1], 300},
    {std::string(), 700},
    {trocar(corpos[1], "tk_c5", "43"), 60000},
    {corpos[2], 60000},
    {corpos[3], 1000},
    {std::string(), 899000},  // quase vence
    {std::string(), 2000},    // venceu: tudo "--"
    {std::string(), 100},
  };
  uint32_t comDados = 0, venceuC5 = 0, vencido = 0, antesC5 = 0;
  int i = 0;
  for (const Passo &p : passos) {
    if (!p.corpo.empty()) { const char *e = put(p.corpo.c_str(), st.ms); CHECK(!e, "passo %d recusado: %s", i, e ? e : ""); }
    st.ms += p.dt;
    appDataExpire(st.ms);
    facePackRender(st, false);  // incremental
    uint32_t inc = hash(tela->px);
    facePackRender(st, true);   // quadro cheio do mesmo estado
    uint32_t cheio = hash(tela->px);
    CHECK(inc == cheio, "passo %d: incremental != quadro cheio (uma area suja perdeu uma mudanca)", i);
    if (i == 0) comDados = cheio;
    if (i == 2) antesC5 = cheio;
    if (i == 3) venceuC5 = cheio;
    if (i == 6) vencido = cheio;
    i++;
  }
  CHECK(comDados != semDados, "os valores nao mudaram a tela");
  CHECK(antesC5 != comDados, "c5 novo nao mudou a tela");
  CHECK(venceuC5 != antesC5, "c5 sem leitura nao mudou a tela");
  CHECK(vencido == semDados, "vencido deveria ficar igual a sem dados");

  // idiomas e hora ausente também têm de bater
  for (int lang = 0; lang < 3; lang++) {
    uiSetLang(lang);
    st.timeOk = lang != 1;
    std::string idioma = trocar(trocar(corpos[1], "tk_c5", "100"), "tk_x5", "");
    put(idioma.c_str(), st.ms);
    st.ms += 50;
    facePackRender(st, false);
    uint32_t inc = hash(tela->px);
    facePackRender(st, true);
    CHECK(inc == hash(tela->px), "idioma %d: incremental != cheio", lang);
  }
  uiSetLang(0);
  facePackUnload();
  delete tela;
  if (falhas) { printf("tokensmall_test: %d falha(s)\n", falhas); return 1; }
  printf("tokensmall_test: ok\n");
  return 0;
}
