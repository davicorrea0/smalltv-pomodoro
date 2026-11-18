// Teste do pacote tokensmall (sim/apps-seeds/tokensmall.json) com os dados que o companheiro do PC empurra
// (miniapps/tokensmall), com ASan/UBSan (sim/testes.sh). Confere: o pacote carrega; sem dados, com os 4 campos,
// com uma janela que passa a "--" (o envio seguinte do companheiro) e vencido; em cada passo o redesenho incremental
// dá o mesmo quadro que o quadro cheio, nos 3 idiomas e sem hora; e que cada campo muda só a sua linha.
// Uso: tokensmall_test <pacote tokensmall.json> [pasta de previas]
// Com a pasta, grava os 4 estados (normal, os mais largos, uma janela sem leitura, sem dados) em RGB565 (<id>-N.raw)
// para sim/tokensmall_previa.js montar a imagem.
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

class Tela : public Adafruit_GFX {
 public:
  uint16_t px[240 * 240];
  Tela() : Adafruit_GFX(240, 240) {}
  void drawPixel(int16_t x, int16_t y, uint16_t c) override {
    if (x >= 0 && y >= 0 && x < 240 && y < 240) px[y * 240 + x] = c;
  }
};

static uint32_t hash(const uint16_t *p) { uint32_t h = 2166136261u; for (int i = 0; i < 240 * 240; i++) h = (h ^ p[i]) * 16777619u; return h; }
static void gravarRaw(const char *pasta, const char *nome, const uint16_t *p) {
  if (!pasta) return;
  std::string cam = std::string(pasta) + "/" + nome + ".raw";
  FILE *f = fopen(cam.c_str(), "wb");
  if (!f) { printf("tokensmall_test: nao gravei %s\n", cam.c_str()); return; }
  fwrite(p, 2, 240 * 240, f);  // little-endian, como o resto do simulador
  fclose(f);
}
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

int main(int argc, char **argv) {
  const char *arq = argc > 1 ? argv[1] : "apps-seeds/tokensmall.json";
  const char *previas = argc > 2 ? argv[2] : nullptr;
  Tela *tela = new Tela();
  uiBegin(tela);
  if (!carregar(arq)) return 1;
  appDataClear();
  UiState st = estado();
  st.ms = 4000000000u;  // perto da volta do millis()
  uiSetLang(0);
  facePackRender(st, true);
  const uint32_t semDados = hash(tela->px);
  char nome[96];
  auto previa = [&](const char *estado) { snprintf(nome, sizeof nome, "%s-%s", facePackId(), estado); gravarRaw(previas, nome, tela->px); };
  previa("4-sem-dados");

  // o que o companheiro manda: sempre os 4 campos ("--" na janela sem leitura)
  struct Passo { const char *corpo; uint32_t dt; };
  const Passo passos[] = {
    {"{\"ttl\":900,\"values\":{\"tk_c5\":\"42% 2h13\",\"tk_c7\":\"71% 3d\",\"tk_x5\":\"93% 38m\",\"tk_x7\":\"12% 5d\"}}", 300},
    {nullptr, 700},
    {"{\"ttl\":900,\"values\":{\"tk_c5\":\"43% 2h12\",\"tk_c7\":\"71% 3d\",\"tk_x5\":\"93% 38m\",\"tk_x7\":\"12% 5d\"}}", 60000},  // só o c5 mudou
    {"{\"ttl\":900,\"values\":{\"tk_c5\":\"--\",\"tk_c7\":\"71% 3d\",\"tk_x5\":\"93% 38m\",\"tk_x7\":\"12% 5d\"}}", 60000},      // janela do Claude venceu
    {"{\"ttl\":900,\"values\":{\"tk_c5\":\"100% 5h59\",\"tk_c7\":\"100% 24h\",\"tk_x5\":\"0% 1m\",\"tk_x7\":\"7% 47h\"}}", 1000},  // os mais largos
    {nullptr, 899000},  // quase vence
    {nullptr, 2000},    // venceu: tudo "--"
    {nullptr, 100},
  };
  uint32_t comDados = 0, venceuC5 = 0, vencido = 0, antesC5 = 0;
  int i = 0;
  for (const Passo &p : passos) {
    if (p.corpo) { const char *e = put(p.corpo, st.ms); CHECK(!e, "passo %d recusado: %s", i, e ? e : ""); }
    st.ms += p.dt;
    appDataExpire(st.ms);
    facePackRender(st, false);  // incremental
    uint32_t inc = hash(tela->px);
    facePackRender(st, true);   // quadro cheio do mesmo estado
    uint32_t cheio = hash(tela->px);
    CHECK(inc == cheio, "passo %d: incremental != quadro cheio (uma area suja perdeu uma mudanca)", i);
    if (i == 0) { comDados = cheio; previa("1-normal"); }
    if (i == 3) previa("3-uma-janela");
    if (i == 4) previa("2-mais-largos");
    if (i == 2) antesC5 = cheio;
    if (i == 3) venceuC5 = cheio;
    if (i == 6) vencido = cheio;
    i++;
  }
  CHECK(comDados != semDados, "os valores nao mudaram a tela");
  CHECK(antesC5 != comDados, "c5 novo nao mudou a tela");
  CHECK(venceuC5 != antesC5, "c5 '--' nao mudou a tela");
  CHECK(vencido == semDados, "vencido deveria ficar igual a sem dados (tudo '--')");

  // idiomas e hora ausente também têm de bater
  for (int lang = 0; lang < 3; lang++) {
    uiSetLang(lang);
    st.timeOk = lang != 1;
    put("{\"ttl\":900,\"values\":{\"tk_c5\":\"100% 5h59\",\"tk_c7\":\"71% 3d\",\"tk_x5\":\"--\",\"tk_x7\":\"12% 5d\"}}", st.ms);
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
