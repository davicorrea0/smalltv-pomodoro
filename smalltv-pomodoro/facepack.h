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
const char *facePackId();
const char *facePackName();
const char *facePackDesc();
const char *facePackAuthor();

// Desenha o pacote carregado (chamado por uiRenderClock quando s.theme == THEME_PACK).
void facePackRender(const UiState &s, bool full);

// O firmware fornece o desenho de imagens JPEG do LittleFS (respeitando uiClip).
// Sem isso (simulador), a camada "image" vira um retângulo cinza.
extern bool (*uiDrawImage)(const char *path, int x, int y);
