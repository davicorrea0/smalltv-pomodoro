// Gera quadros RGB565 de um miniapp a partir dos estados usados nos testes e na documentacao.
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
  Tela() : Adafruit_GFX(240, 240) { memset(px, 0, sizeof px); }
  void drawPixel(int16_t x, int16_t y, uint16_t cor) override { if (x >= 0 && y >= 0 && x < 240 && y < 240) px[y * 240 + x] = cor; }
};

static bool ler(const char *arq, std::string &texto) {
  FILE *f = fopen(arq, "rb");
  if (!f) return false;
  char bloco[4096];
  for (size_t n; (n = fread(bloco, 1, sizeof bloco, f));) texto.append(bloco, n);
  bool ok = !ferror(f);
  fclose(f);
  return ok;
}

static UiState estado() {
  UiState st;
  memset(&st, 0, sizeof st);
  st.theme = THEME_PACK; st.timeOk = true;
  st.now.tm_hour = 14; st.now.tm_min = 37; st.now.tm_sec = 9;
  st.now.tm_mday = 16; st.now.tm_mon = 10; st.now.tm_wday = 1; st.now.tm_year = 126;
  st.accent = 0xFFFF; st.city = "Cidade"; st.ip = "10.0.0.1";
  return st;
}

static uint32_t hashQuadro(const uint16_t *px) {
  uint32_t h = 2166136261u;
  for (int i = 0; i < 240 * 240; i++) h = (h ^ px[i]) * 16777619u;
  return h;
}

int main(int argc, char **argv) {
  if (argc != 4) { fprintf(stderr, "Uso: previa_miniapp <pacote.json> <estados.json> <pasta de saida>\n"); return 1; }
  std::string pacote, estados;
  if (!ler(argv[1], pacote)) { fprintf(stderr, "Erro: nao foi possivel ler %s\n", argv[1]); return 1; }
  if (!ler(argv[2], estados)) { fprintf(stderr, "Erro: nao foi possivel ler %s\n", argv[2]); return 1; }
  JsonDocument pacoteDoc, estadosDoc;
  DeserializationError erroJson = deserializeJson(pacoteDoc, pacote);
  if (erroJson) { fprintf(stderr, "Erro: pacote JSON invalido: %s\n", erroJson.c_str()); return 1; }
  erroJson = deserializeJson(estadosDoc, estados);
  if (erroJson) { fprintf(stderr, "Erro: estados JSON invalidos: %s\n", erroJson.c_str()); return 1; }
  const char *id = pacoteDoc["id"] | "";
  JsonArray lista = estadosDoc["estados"].as<JsonArray>();
  if (!*id || lista.isNull() || lista.size() == 0) { fprintf(stderr, "Erro: pacote sem id ou arquivo de estados vazio\n"); return 1; }
  Tela tela;
  uiBegin(&tela); uiSetLang(0);
  std::vector<char> buffer(pacote.begin(), pacote.end()); buffer.push_back(0);
  char erro[128] = "";
  if (!facePackLoad(buffer.data(), pacote.size(), erro, sizeof erro)) { fprintf(stderr, "Erro: pacote recusado: %s\n", erro); return 1; }
  UiState st = estado(); st.ms = 1000;
  std::vector<uint32_t> hashes;
  int indice = 0;
  for (JsonObject item : lista) {
    appDataClear();
    for (JsonObject envio : item["envios"].as<JsonArray>()) {
      std::string corpo; serializeJson(envio, corpo);
      const char *e = appDataPut(corpo.c_str(), corpo.size(), st.ms);
      if (e) { fprintf(stderr, "Erro: estado %d, envio recusado: %s\n", indice, e); facePackUnload(); return 1; }
    }
    appDataExpire(st.ms); facePackRender(st, true);
    uint32_t h = hashQuadro(tela.px);
    for (size_t anterior = 0; anterior < hashes.size(); anterior++) if (hashes[anterior] == h) {
      fprintf(stderr, "Erro: estados %zu e %d geraram quadros identicos\n", anterior, indice); facePackUnload(); return 1;
    }
    hashes.push_back(h);
    std::string destino = std::string(argv[3]) + "/" + id + "-" + std::to_string(indice) + ".raw";
    FILE *f = fopen(destino.c_str(), "wb");
    if (!f) { fprintf(stderr, "Erro: nao foi possivel criar %s\n", destino.c_str()); facePackUnload(); return 1; }
    for (uint16_t px : tela.px) { unsigned char bytes[2] = {(unsigned char)(px & 255), (unsigned char)(px >> 8)}; if (fwrite(bytes, 1, 2, f) != 2) { fclose(f); fprintf(stderr, "Erro ao gravar %s\n", destino.c_str()); facePackUnload(); return 1; } }
    fclose(f); indice++;
  }
  facePackUnload();
  printf("previa_miniapp: %s, %d estado(s)\n", id, indice);
  return 0;
}
