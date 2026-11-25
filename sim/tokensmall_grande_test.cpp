// Teste dos pacotes de mascote grande (sim/apps-seeds/tokensmall-clawd.json e tokensmall-codex.json): um mascote por
// pacote que reage aos 3 campos do seu assistente, entre os 6 que o companheiro do PC empurra (miniapps/tokensmall).
// Com ASan/UBSan (sim/testes.sh). Confere:
//  - o pacote carrega, lê exatamente os 3 campos esperados e cabe no teto de sprites;
//  - cada humor (sem dados, trabalhando, perto do limite, no limite, descansado) muda o mascote;
//  - os campos do outro assistente não mudam a tela;
//  - com os sprites andando (100 ms por passo, vários ciclos, na volta do millis) o redesenho incremental dá o mesmo
//    quadro que o quadro cheio, em cada estado, nas trocas de estado e nos 3 idiomas.
// Uso: tokensmall_grande_test <pacote> <estados> <campos em ordem alfabética> <campo do outro assistente>
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
  if (!f) { printf("tokensmall_grande_test: sem %s\n", arq); return false; }
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
// o mascote, o chão e o que voa em volta (entre a linha do 7D e o chão)
static uint32_t cena(const uint16_t *p) { return hashRet(p, 40, 30, 160, 118); }

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
  if (argc < 5) { printf("uso: tokensmall_grande_test <pacote> <estados> <campos> <campo do outro>\n"); return 1; }
  std::string pacote, estadosTxt;
  if (!ler(argv[1], pacote) || !ler(argv[2], estadosTxt)) return 1;
  JsonDocument doc;
  if (deserializeJson(doc, estadosTxt)) { printf("tokensmall_grande_test: estados invalidos\n"); return 1; }
  std::vector<std::string> corpos;
  for (JsonObject e : doc["estados"].as<JsonArray>()) {
    std::string c;
    JsonArray envios = e["envios"].as<JsonArray>();
    if (!envios.isNull() && envios.size()) serializeJson(envios[0], c);
    corpos.push_back(c);
  }
  // 0 sem dados, 1 trabalhando, 2 perto do limite, 3 no limite, 4 descansado
  if (corpos.size() != 5) { printf("tokensmall_grande_test: esperava 5 estados, vieram %d\n", (int)corpos.size()); return 1; }

  Tela *tela = new Tela();
  uiBegin(tela);
  std::vector<char> buf(pacote.begin(), pacote.end());
  buf.push_back(0);
  char err[96] = "";
  if (!facePackLoad(buf.data(), pacote.size(), err, sizeof err)) { printf("tokensmall_grande_test: pacote recusado: %s\n", err); return 1; }
  char campos[96];
  facePackFields(campos, sizeof campos);
  std::vector<std::string> v;
  for (const char *p = campos; *p;) { const char *e = strchr(p, ','); v.push_back(e ? std::string(p, e - p) : std::string(p)); p += e ? (e - p) + 1 : strlen(p); }
  std::sort(v.begin(), v.end());
  std::string r;
  for (const std::string &x : v) r += (r.empty() ? "" : ",") + x;
  CHECK(r == argv[3], "campos lidos '%s', esperava '%s'", r.c_str(), argv[3]);
  CHECK(facePackSpriteBytes() > 0 && facePackSpriteBytes() <= 8192, "sprites: %d bytes", (int)facePackSpriteBytes());

  UiState st = estado();
  uiSetLang(0);
  const uint32_t base = 4000000000u - 200;  // os sprites andam atravessando a volta do millis()
  uint32_t quadro[5], m[5];
  for (int e = 0; e < 5; e++) {
    appDataClear();
    if (!corpos[e].empty()) { const char *msg = appDataPut(corpos[e].c_str(), corpos[e].size(), base); CHECK(!msg, "estado %d recusado: %s", e, msg ? msg : ""); }
    st.ms = base + 1000;  // mesmo instante da animacao em todos os estados
    appDataExpire(st.ms);
    facePackRender(st, true);
    quadro[e] = hash(tela->px);
    m[e] = cena(tela->px);
  }
  for (int a = 0; a < 5; a++)
    for (int b = a + 1; b < 5; b++) {
      CHECK(quadro[a] != quadro[b], "estados %d e %d geraram o mesmo quadro", a, b);
      CHECK(m[a] != m[b], "estados %d e %d com o mesmo mascote", a, b);
    }
  // os campos do outro assistente nao mexem na tela
  for (int e = 1; e < 5; e++) {
    appDataClear();
    appDataPut(corpos[e].c_str(), corpos[e].size(), base);
    st.ms = base + 1000;
    facePackRender(st, true);
    uint32_t antes = hash(tela->px);
    std::string alterado;
    { JsonDocument d; deserializeJson(d, corpos[e]); d["values"][argv[4]] = e == 3 ? "0" : "100"; serializeJson(d, alterado); }
    appDataPut(alterado.c_str(), alterado.size(), base);
    facePackRender(st, true);
    CHECK(hash(tela->px) == antes, "estado %d: a tela mudou quando so %s mudou", e, argv[4]);
  }

  // incremental x cheio, com os sprites andando (passo de 100 ms), trocando de estado no meio, nos 3 idiomas
  for (int lang = 0; lang < 3; lang++) {
    uiSetLang(lang);
    st.timeOk = lang != 1;
    uint32_t ms = base;
    const int ordem[] = {0, 1, 2, 3, 4, 3, 1, 0, 4, 2};
    for (int k : ordem) {
      appDataClear();
      if (!corpos[k].empty()) appDataPut(corpos[k].c_str(), corpos[k].size(), ms);
      for (int passo = 0; passo < 30; passo++) {  // 3 s: mais de um ciclo de cada sprite e de cada motion
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
  if (falhas) { printf("tokensmall_grande_test %s: %d falha(s)\n", argv[1], falhas); return 1; }
  printf("tokensmall_grande_test %s: ok\n", argv[1]);
  return 0;
}
