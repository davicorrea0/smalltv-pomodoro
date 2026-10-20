// ui.h — telas do SmallTV (240x240). Só depende do Adafruit_GFX: o firmware e o
// simulador de PC (sim/) compilam este mesmo código, então o que o simulador mostra
// é o que aparece no aparelho.
#pragma once
#include <Adafruit_GFX.h>
#include <stdint.h>
#include <time.h>

enum UiTheme : uint8_t {
  THEME_MATRIX = 0, THEME_TOMATO = 1, THEME_ANALOG = 2, THEME_WEATHER = 3,
  THEME_NEON = 4, THEME_FLIP = 5, THEME_LCD = 6, THEME_WORDS = 7, THEME_COUNT
};
enum UiPhase : uint8_t { PH_IDLE = 0, PH_WORK, PH_SHORT, PH_LONG };

struct UiWeather {
  bool ok;
  float t, tmin, tmax;
  int hum, code;
  bool day;
  uint16_t version;  // muda a cada atualização: força redesenho do clima
};

struct UiState {
  bool timeOk;
  struct tm now;
  bool hour12;
  // Pomodoro
  uint8_t phase;
  bool running;
  uint32_t remainingMs, totalMs;
  uint8_t cycleDone, longEvery;
  uint16_t today;
  uint8_t dailyGoal;
  // clima
  bool weatherOn;
  UiWeather wx;
  const char *city;
  // geral
  uint8_t theme;
  uint16_t accent;  // cor dos números do relógio (RGB565)
  const char *ip;
  uint32_t ms;     // millis(): anda as animações
  bool celebrate;  // uma fase do Pomodoro acabou de terminar: toca a animação de fim
};

constexpr uint16_t uiRgb(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

// Sprite em pixel art. Os sprites são gerados de assets/*.txt por assets/gerar.js em
// assets.h (SPR_<NOME>) e ficam na flash (PROGMEM), com 4 bits por pixel.
struct UiSprite {
  uint8_t w, h, frames;
  uint16_t frameMs;     // duração de cada quadro (animações)
  const uint16_t *pal;  // cor dos índices 1..15 (0 = transparente)
  const uint8_t *px;    // índices, linha a linha, um quadro depois do outro
};
static const int32_t UI_TRANSPARENT = -1;

// Desenha o quadro "frame" com pixels de scale x scale em (x, y). Pixels transparentes
// ficam como estão, ou recebem "bg" (útil para repintar por cima do desenho anterior).
void uiSprite(const UiSprite *s, int x, int y, uint8_t scale = 1, uint8_t frame = 0, int32_t bg = UI_TRANSPARENT);
uint8_t uiSpriteFrame(const UiSprite *s, uint32_t ms);  // quadro da animação no instante ms

void uiBegin(Adafruit_GFX *gfx);
void uiInvalidate();                         // redesenha tudo na próxima chamada
void uiRenderClock(const UiState &s);        // tela de relógio no tema escolhido
void uiRenderPomodoro(const UiState &s);     // anel de progresso do Pomodoro
void uiMessage(const char *title, const char *l1, const char *l2, const char *l3);
void uiColorTest();                          // barras R/G/B/branco para ajustar o painel
void uiError(const char *msg);               // aviso centralizado (ex.: GIF inválido)
