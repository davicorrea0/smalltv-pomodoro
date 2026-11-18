// facepack.h — mostradores instaláveis: um arquivo JSON descreve o mostrador em camadas e
// este motor desenha, sem precisar regravar o firmware. Formato: docs/PACOTES.md
#pragma once
#include "ui.h"

static const uint8_t THEME_PACK = 200;  // cfg.theme quando o mostrador ativo é um pacote

// Carrega um pacote a partir do JSON em memória (o buffer pode ser alterado durante a leitura).
// Em caso de erro devolve false e uma mensagem em err.
bool facePackLoad(char *json, size_t len, char *err, size_t errLen);
#ifdef ESP8266
#include <FS.h>
// Idem, lendo o arquivo em passadas (sem carregá-lo inteiro na memória). A posição do arquivo é redefinida.
bool facePackLoadFile(File &f, char *err, size_t errLen);
#endif
void facePackUnload();
bool facePackLoaded();
uint16_t facePackSpriteBytes();  // bytes dos sprites na RAM (compactados), para testes e diagnóstico
const char *facePackId();
const char *facePackName();
const char *facePackDesc();
const char *facePackAuthor();
// campos de fora que o pacote carregado lê, "a,b,c" (vazio = mostrador comum; algum = miniapp)
void facePackFields(char *out, size_t n);
// Os mesmos campos achados direto no texto do .json, sem carregar o pacote (para os .txt de antes da 4ª linha):
// "{x.campo}" num texto e "x.campo" / "!x.campo" / "x.campo=..." no começo de uma string (when, value). Alimente byte a
// byte; a lista fica em out. Pode achar a mais (um texto que começa com "x.campo"), nunca a menos que o motor.
struct FieldScan {
  char out[100];
  void begin() { out[0] = 0; o = 0; w = 0; mode = 0; kl = 0; }
  void feed(char c);
 private:
  uint32_t w;      // os 4 últimos bytes
  uint8_t mode;    // 0 procurando; 1 depois de "{x." (fecha com '}'); 2 depois de "x. numa string (fecha com '"' ou '=')
  uint8_t kl, o;
  char k[10];
};

// Desenha o pacote carregado (chamado por uiRenderClock quando s.theme == THEME_PACK).
void facePackRender(const UiState &s, bool full);

// O firmware fornece o desenho de imagens JPEG do LittleFS (respeitando uiClip).
// Sem isso (simulador), a camada "image" vira um retângulo cinza.
extern bool (*uiDrawImage)(const char *path, int x, int y);
