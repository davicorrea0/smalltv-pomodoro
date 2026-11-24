// Teste do pacote tokensmall-arcade (sim/apps-seeds/tokensmall-arcade.json): mascotes animados que reagem aos 6 campos que o
// companheiro do PC empurra (miniapps/tokensmall). Com ASan/UBSan (sim/testes.sh). Confere:
//  - o pacote carrega, lê exatamente os 6 campos do contrato e cabe nos limites (sprites, camadas);
//  - cada humor (sem dados, trabalhando, perto do limite, no limite, descansado) muda a tela;
//  - cada assistente reage só ao seu limite: o quarto do Claude não muda quando só o Codex muda, e vice-versa;
//  - com os sprites andando (100 ms por passo, ao longo de vários ciclos e na volta do millis) o redesenho incremental
//    dá o mesmo quadro que o quadro cheio, em cada estado, nas trocas de estado e nos 3 idiomas.
// Uso: tokensmall_arcade_test <pacote> <estados>
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
  Tela() : Adafruit_GFX(240, 240) { memset(px, 0, sizeof px); }
  void drawPixel(int16_t x, int16_t y, uint16_t c) override {
    if (x >= 0 && y >= 0 && x < 240 && y < 240) px[y * 240 + x] = c;
  }
};

static bool ler(const char *arq, std::string &s) {
  FILE *f = fopen(arq, "rb");
  if (!f) { printf("tokensmall_arcade_test: sem %s\n", arq); return false; }
  char b[4096];
  for (size_t k; (k = fread(b, 1, sizeof b, f));) s.append(b, k);
  fclose(f);
  return true;
}

// hash de um retângulo da tela
static uint32_t hashRet(const uint16_t *p, int x, int y, int w, int h) {
  uint32_t r = 2166136261u;
  for (int j = y; j < y + h; j++) for (int i = x; i < x + w; i++) r = (r ^ p[j * 240 + i]) * 16777619u;
  return r;
}
static uint32_t hash(const uint16_t *p) { return hashRet(p, 0, 0, 240, 240); }
// o quarto (sprite, 72 x 60) e o cartão inteiro de cada assistente: Claude em cima, Codex embaixo
static uint32_t cenaC(const uint16_t *p) { return hashRet(p, 16, 22, 72, 60); }
static uint32_t cenaX(const uint16_t *p) { return hashRet(p, 16, 138, 72, 60); }
static uint32_t cartaoC(const uint16_t *p) { return hashRet(p, 8, 4, 224, 112); }
static uint32_t cartaoX(const uint16_t *p) { return hashRet(p, 8, 120, 224, 112); }

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
  const char *arq = argc > 1 ? argv[1] : "apps-seeds/tokensmall-arcade.json";
  const char *arqEstados = argc > 2 ? argv[2] : "miniapps-estados/tokensmall-arcade.json";
  std::string pacote, estadosTxt;
  if (!ler(arq, pacote) || !ler(arqEstados, estadosTxt)) return 1;
  JsonDocument doc;
  if (deserializeJson(doc, estadosTxt)) { printf("tokensmall_arcade_test: estados invalidos\n"); return 1; }
  std::vector<std::string> corpos;
  for (JsonObject e : doc["estados"].as<JsonArray>()) {
    std::string c;
    JsonArray envios = e["envios"].as<JsonArray>();
    if (!envios.isNull() && envios.size()) serializeJson(envios[0], c);
    corpos.push_back(c);
  }
  // 0 sem dados, 1 trabalhando, 2 perto do limite, 3 no limite, 4 descansado, 5 Claude sem leitura (Codex trabalhando),
  // 6 Claude no limite com Codex descansado
  if (corpos.size() < 7) { printf("tokensmall_arcade_test: esperava 7 estados, vieram %d\n", (int)corpos.size()); return 1; }

  Tela *tela = new Tela();
  uiBegin(tela);
  std::vector<char> buf(pacote.begin(), pacote.end());
  buf.push_back(0);
  char err[96] = "";
  if (!facePackLoad(buf.data(), pacote.size(), err, sizeof err)) { printf("tokensmall_arcade_test: pacote recusado: %s\n", err); return 1; }
  char campos[96];
  facePackFields(campos, sizeof campos);
  std::vector<std::string> v;
  for (const char *p = campos; *p;) { const char *e = strchr(p, ','); v.push_back(e ? std::string(p, e - p) : std::string(p)); p += e ? (e - p) + 1 : strlen(p); }
  std::sort(v.begin(), v.end());
  std::string r;
  for (const std::string &x : v) r += (r.empty() ? "" : ",") + x;
  CHECK(r == "tk_c5,tk_c5r,tk_c7,tk_x5,tk_x5r,tk_x7", "campos lidos '%s' (contrato com o companheiro: so os 6 de sempre)", campos);
  CHECK(facePackSpriteBytes() > 0 && facePackSpriteBytes() <= 8192, "sprites: %d bytes", (int)facePackSpriteBytes());

  UiState st = estado();
  uiSetLang(0);
  const uint32_t base = 4000000000u - 200;  // os sprites andam atravessando a volta do millis()
  uint32_t quadro[7], cC[7], cX[7], kC[7], kX[7];
  for (int e = 0; e < 7; e++) {
    appDataClear();
    if (!corpos[e].empty()) { const char *m = appDataPut(corpos[e].c_str(), corpos[e].size(), base); CHECK(!m, "estado %d recusado: %s", e, m ? m : ""); }
    st.ms = base + 1000;  // mesmo instante da animacao em todos os estados
    appDataExpire(st.ms);
    facePackRender(st, true);
    quadro[e] = hash(tela->px); cC[e] = cenaC(tela->px); cX[e] = cenaX(tela->px); kC[e] = cartaoC(tela->px); kX[e] = cartaoX(tela->px);
  }
  for (int a = 0; a < 7; a++) for (int b = a + 1; b < 7; b++) CHECK(quadro[a] != quadro[b], "estados %d e %d geraram o mesmo quadro", a, b);
  // humores: no mesmo instante, cada assistente muda de sprite so pelo seu limite
  CHECK(cC[0] != cC[1] && cC[1] != cC[3] && cC[1] != cC[4] && cC[3] != cC[4] && cC[0] != cC[3] && cC[0] != cC[4], "Claude: humores iguais");
  CHECK(cX[0] != cX[1] && cX[1] != cX[3] && cX[1] != cX[4] && cX[3] != cX[4] && cX[0] != cX[3] && cX[0] != cX[4], "Codex: humores iguais");
  CHECK(cC[2] != cC[1], "Claude perto do limite (gota de suor) igual ao normal");
  CHECK(cX[2] != cX[1], "Codex perto do limite (gota de suor) igual ao normal");
  CHECK(cC[6] == cC[3], "Claude no limite deveria ter o mesmo quarto, tenha o Codex o humor que tiver");
  CHECK(cX[6] == cX[4], "Codex descansado deveria ter o mesmo quarto, tenha o Claude o humor que tiver");
  CHECK(cC[5] == cC[0], "Claude sem leitura deveria dormir como no estado sem dados");
  CHECK(cX[5] != cX[0] && cX[5] == cX[1], "Codex a 42%% deveria trabalhar como no estado trabalhando");
  CHECK(kX[6] != kX[3] && kC[6] != kC[4], "cartoes iguais em estados diferentes");
  // o cartao de um assistente nao muda quando so o outro muda (Codex de 63 para 64 nao mexe no cartao do Claude)
  appDataClear();
  appDataPut(corpos[1].c_str(), corpos[1].size(), base);
  st.ms = base + 1000;
  facePackRender(st, true);
  uint32_t kC1 = cartaoC(tela->px);
  std::string alterado;
  { JsonDocument d; deserializeJson(d, corpos[1]); d["values"]["tk_x5"] = "64"; serializeJson(d, alterado); }
  appDataPut(alterado.c_str(), alterado.size(), base);
  facePackRender(st, true);
  CHECK(cartaoC(tela->px) == kC1, "o cartao do Claude mudou quando so o percentual do Codex mudou");

  // incremental x cheio, com os sprites andando (passo de 10 fps), trocando de estado no meio, nos 3 idiomas
  for (int lang = 0; lang < 3; lang++) {
    uiSetLang(lang);
    st.timeOk = lang != 1;
    uint32_t ms = base;
    const int ordem[] = {0, 1, 2, 3, 4, 5, 6, 3, 1, 0};
    for (int k : ordem) {
      appDataClear();
      if (!corpos[k].empty()) appDataPut(corpos[k].c_str(), corpos[k].size(), ms);
      for (int passo = 0; passo < 30; passo++) {  // 3 s: mais de um ciclo de cada sprite
        ms += 100;
        st.ms = ms;
        appDataExpire(ms);
        facePackRender(st, false);
        uint32_t inc = hash(tela->px);
        facePackRender(st, true);
        if (inc != hash(tela->px)) { CHECK(false, "idioma %d estado %d passo %d: incremental != cheio", lang, k, passo); break; }
      }
    }
  }
  uiSetLang(0);
  facePackUnload();
  delete tela;
  if (falhas) { printf("tokensmall_arcade_test: %d falha(s)\n", falhas); return 1; }
  printf("tokensmall_arcade_test: ok\n");
  return 0;
}
