// Quadros de um miniapp ANIMADO em instantes diferentes (os sprites com "frames" andam com o relogio do aparelho).
// Uso: previa_animada <pacote.json> <estados.json> <indice do estado> <pasta de saida> <quadros> <passo em ms>
// Grava <pasta>/q000.raw ... (240x240 RGB565 little-endian). Quem monta o PNG/GIF e sim/gif_miniapp.py.
#include <ArduinoJson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include "../smalltv-pomodoro/ui_kit.h"
#include "../smalltv-pomodoro/facepack.h"
#include "../smalltv-pomodoro/appdata.h"

class Tela : public Adafruit_GFX {
 public:
  uint16_t px[240 * 240];
  Tela() : Adafruit_GFX(240, 240) { memset(px, 0, sizeof px); }
  void drawPixel(int16_t x, int16_t y, uint16_t cor) override { if (x >= 0 && y >= 0 && x < 240 && y < 240) px[y * 240 + x] = cor; }
};

static bool ler(const char *arq, std::string &texto) {
  FILE *f = fopen(arq, "rb");
  if (!f) return false;
  char bloco[4096];
  for (size_t n; (n = fread(bloco, 1, sizeof bloco, f));) texto.append(bloco, n);
  fclose(f);
  return true;
}

int main(int argc, char **argv) {
  if (argc != 7) { fprintf(stderr, "Uso: previa_animada <pacote.json> <estados.json> <estado> <pasta> <quadros> <passo ms>\n"); return 1; }
  std::string pacote, estados;
  if (!ler(argv[1], pacote) || !ler(argv[2], estados)) { fprintf(stderr, "Erro: nao foi possivel ler os arquivos\n"); return 1; }
  JsonDocument estadosDoc;
  if (deserializeJson(estadosDoc, estados)) { fprintf(stderr, "Erro: estados JSON invalidos\n"); return 1; }
  JsonArray lista = estadosDoc["estados"].as<JsonArray>();
  int indice = atoi(argv[3]), n = atoi(argv[5]), passo = atoi(argv[6]);
  if (indice < 0 || (size_t)indice >= lista.size() || n < 1 || passo < 1) { fprintf(stderr, "Erro: estado, quadros ou passo invalidos\n"); return 1; }
  Tela *tela = new Tela();
  uiBegin(tela); uiSetLang(0);
  std::vector<char> buffer(pacote.begin(), pacote.end()); buffer.push_back(0);
  char erro[128] = "";
  if (!facePackLoad(buffer.data(), pacote.size(), erro, sizeof erro)) { fprintf(stderr, "Erro: pacote recusado: %s\n", erro); return 1; }
  UiState st;
  memset(&st, 0, sizeof st);
  st.theme = THEME_PACK; st.timeOk = true;
  st.now.tm_hour = 14; st.now.tm_min = 37; st.now.tm_sec = 9; st.now.tm_mday = 16; st.now.tm_mon = 10; st.now.tm_wday = 1; st.now.tm_year = 126;
  st.accent = 0xFFFF; st.city = "Cidade"; st.ip = "10.0.0.1";
  st.ms = 1000;
  appDataClear();
  for (JsonObject envio : lista[indice]["envios"].as<JsonArray>()) {
    std::string corpo; serializeJson(envio, corpo);
    const char *e = appDataPut(corpo.c_str(), corpo.size(), st.ms);
    if (e) { fprintf(stderr, "Erro: envio recusado: %s\n", e); return 1; }
  }
  for (int i = 0; i < n; i++) {
    st.ms = 1000 + (uint32_t)i * passo;
    appDataExpire(st.ms);
    facePackRender(st, true);
    char destino[512];
    snprintf(destino, sizeof destino, "%s/q%03d.raw", argv[4], i);
    FILE *f = fopen(destino, "wb");
    if (!f) { fprintf(stderr, "Erro: nao foi possivel criar %s\n", destino); return 1; }
    for (uint16_t px : tela->px) { unsigned char b[2] = {(unsigned char)(px & 255), (unsigned char)(px >> 8)}; fwrite(b, 1, 2, f); }
    fclose(f);
  }
  facePackUnload();
  delete tela;
  return 0;
}
