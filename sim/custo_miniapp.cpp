// Custo de um miniapp: bytes dos sprites na RAM e pixels escritos por quadro (média e pior) em cada estado,
// em 300 quadros de 100 ms com uma virada de minuto, dividido pelo desenho completo (a medida do #114/#98).
// Um quadro "incremental" é o que o relógio desenha a cada 100 ms; "cheio" é o desenho completo.
// Compilar (a partir de sim/, com GFX e JSON apontando para as bibliotecas, como no sim/build.sh; g++ de 32 bits ou zig c++):
//   g++ -m32 -std=c++17 -O1 -w -DARDUINO=100 -DARDUINOJSON_ENABLE_ARDUINO_STRING=0 -DARDUINOJSON_ENABLE_ARDUINO_STREAM=0 //     -DARDUINOJSON_ENABLE_ARDUINO_PRINT=0 -DARDUINOJSON_ENABLE_PROGMEM=0 -Ishim -I"$GFX" -I"$JSON" custo_miniapp.cpp //     ../smalltv-pomodoro/ui.cpp ../smalltv-pomodoro/face_*.cpp ../smalltv-pomodoro/facepack.cpp ../smalltv-pomodoro/appdata.cpp //     ../smalltv-pomodoro/fonts.cpp ../smalltv-pomodoro/trig.cpp ../smalltv-pomodoro/src/qrcodegen/qrcodegen.cpp //     "$GFX/Adafruit_GFX.cpp" -o out/custo_miniapp
// Uso: custo_miniapp <pacote.json> <estados.json>   (os estados são os mesmos de sim/miniapps-estados)
#include <ArduinoJson.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include "../smalltv-pomodoro/ui_kit.h"
#include "../smalltv-pomodoro/facepack.h"
#include "../smalltv-pomodoro/appdata.h"

class Tela : public Adafruit_GFX {
 public:
  uint16_t px[240 * 240];
  long escritos = 0;
  Tela() : Adafruit_GFX(240, 240) { memset(px, 0, sizeof px); }
  void drawPixel(int16_t x, int16_t y, uint16_t cor) override {
    if (x >= 0 && y >= 0 && x < 240 && y < 240) { px[y * 240 + x] = cor; escritos++; }
  }
};

static bool ler(const char *arq, std::string &t) {
  FILE *f = fopen(arq, "rb");
  if (!f) return false;
  char b[4096];
  for (size_t n; (n = fread(b, 1, sizeof b, f));) t.append(b, n);
  fclose(f);
  return true;
}

int main(int argc, char **argv) {
  if (argc != 3) { fprintf(stderr, "Uso: custo_miniapp <pacote.json> <estados.json>\n"); return 1; }
  std::string pacote, estados;
  if (!ler(argv[1], pacote) || !ler(argv[2], estados)) { fprintf(stderr, "nao foi possivel ler os arquivos\n"); return 1; }
  JsonDocument ed;
  if (deserializeJson(ed, estados)) { fprintf(stderr, "estados invalidos\n"); return 1; }
  Tela tela;
  uiBegin(&tela); uiSetLang(0);
  std::vector<char> buf(pacote.begin(), pacote.end()); buf.push_back(0);
  char erro[128] = "";
  if (!facePackLoad(buf.data(), pacote.size(), erro, sizeof erro)) { fprintf(stderr, "pacote recusado: %s\n", erro); return 1; }
  printf("arquivo %zu B, sprites na RAM %u B\n", pacote.size(), (unsigned)facePackSpriteBytes());
  UiState st; memset(&st, 0, sizeof st);
  st.theme = THEME_PACK; st.timeOk = true; st.accent = 0xFFFF; st.city = "Cidade"; st.ip = "10.0.0.1";
  st.now.tm_hour = 14; st.now.tm_min = 37; st.now.tm_sec = 0; st.now.tm_mday = 16; st.now.tm_mon = 10; st.now.tm_wday = 1; st.now.tm_year = 126;
  int i = 0;
  for (JsonObject item : ed["estados"].as<JsonArray>()) {
    appDataClear();
    st.ms = 1000;
    for (JsonObject envio : item["envios"].as<JsonArray>()) {
      std::string corpo; serializeJson(envio, corpo);
      const char *e = appDataPut(corpo.c_str(), corpo.size(), st.ms);
      if (e) { fprintf(stderr, "estado %d: envio recusado: %s\n", i, e); return 1; }
    }
    appDataExpire(st.ms);
    tela.escritos = 0; facePackRender(st, true);
    long cheio = tela.escritos;
    double soma = 0, pior = 0; int q = 300;
    st.now.tm_min = 37; st.now.tm_sec = 0;
    for (int k = 0; k < q; k++) {
      st.ms += 100;
      int seg = (k / 10) % 60;
      st.now.tm_sec = seg;
      if (k == 150) { st.now.tm_min = 38; }  // virada de minuto no meio
      appDataExpire(st.ms);
      tela.escritos = 0; facePackRender(st, false);
      double r = cheio ? (double)tela.escritos / cheio : 0;
      soma += r; if (r > pior) pior = r;
    }
    st.now.tm_min = 37;
    printf("estado %d (%s): cheio %ld px, quadro medio %.3f, pior %.3f\n", i, item["n"] | "", cheio, soma / q, pior);
    i++;
  }
  facePackUnload();
  return 0;
}
