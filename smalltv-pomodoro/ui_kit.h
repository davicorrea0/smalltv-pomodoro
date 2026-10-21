// ui_kit.h — kit de desenho para os mostradores (uso interno de ui.cpp e face_*.cpp).
//
// Um mostrador é uma função  void themeX(const UiState &s, bool full)  chamada a cada ~100 ms.
// full = true na primeira chamada (tela já limpa com BG): desenhe tudo. Nas seguintes,
// redesenhe só o que mudou, usando os caches abaixo. Guia completo: docs/CRIAR_MOSTRADOR.md
#pragma once
#include "ui.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

// ---------- tela e constantes ----------
extern Adafruit_GFX *G;  // a tela (no simulador, um framebuffer)
static const float K_PI = 3.14159265f;

// paleta comum
static const uint16_t BG     = uiRgb(0, 0, 0);
static const uint16_t CARD   = uiRgb(22, 22, 26);
static const uint16_t CARD2  = uiRgb(44, 44, 50);
static const uint16_t TEXT   = uiRgb(242, 242, 242);
static const uint16_t MUTED  = uiRgb(125, 125, 135);
static const uint16_t DIM    = uiRgb(30, 30, 34);
static const uint16_t WORK   = uiRgb(255, 90, 71);
static const uint16_t SHORT  = uiRgb(62, 207, 122);
static const uint16_t LONG   = uiRgb(79, 141, 255);
static const uint16_t SUN    = uiRgb(255, 196, 40);
static const uint16_t CLOUD  = uiRgb(214, 220, 230);
static const uint16_t DCLOUD = uiRgb(120, 126, 138);
static const uint16_t RAIN   = uiRgb(90, 170, 255);

// ---------- idioma (uiSetLang) ----------
extern char WD[7][4];   // dia da semana abreviado no idioma atual ("TER", "TUE", "MAR")
extern char MO[12][4];  // mês abreviado ("OUT", "OCT", "OCT")

// ---------- texto (fonte 6x8 embutida, escala inteira; só ASCII) ----------
int textW(const char *s, uint8_t sz);
void text(int x, int y, uint8_t sz, uint16_t c, const char *s);
void textC(int cx, int y, uint8_t sz, uint16_t c, const char *s);  // centralizado em cx
void textR(int rx, int y, uint8_t sz, uint16_t c, const char *s);  // alinhado à direita em rx
void degree(int x, int y, uint8_t sz, uint16_t c);                 // símbolo de grau desenhado
void tempText(int cx, int y, uint8_t sz, uint16_t c, float v);     // "23°" centralizado
void tempDesc(int cy, uint8_t sz, uint16_t ct, uint16_t cd, float t, const char *desc);  // "23° Chuva"

// ---------- formas ----------
void thickLine(float x0, float y0, float x1, float y1, float w, uint16_t c);  // pontas redondas
uint16_t mix(uint16_t a, uint16_t b, float f);                                  // f=0 -> a, f=1 -> b
void blit(int16_t x, int16_t y, uint16_t *px, int16_t w, int16_t h);            // bloco RGB565 (rápido)

// ---------- dígitos ----------
// Pixel art 5x7 sólido: p = tamanho da célula (dígito = 5p x 7p); d < 0 apaga; DM_MINUS = "-".
static const int DM_MINUS = 10;
void dmDigit(int x, int y, int d, int p, uint16_t on, uint16_t off, bool drawOff);
// HH:MM em pixel art com redesenho incremental (usa lastDig/lastColon)
struct TimeLayout { int x[4]; int y; int p; int colonX; };
void drawTime4(const TimeLayout &L, int a, int b, int c, int d, bool colon, uint16_t on, uint16_t off);
// DSEG7 (relógio LCD): dígito de 0,618h x h; segmentos apagados em "off"
void dsegDigit(int x, int y, int h, int d, uint16_t on, uint16_t off);
// Fontes vetoriais suavizadas sobre bg (só dígitos, ':' e espaço), altura h (fonts.cpp)
struct VecFont {
  const int16_t (*pt)[2];  // pontos dos contornos (célula de 720 de altura)
  const uint16_t *cend;    // fim (exclusivo) de cada contorno
  const uint8_t *glyph;    // primeiro contorno de 0-9 e ':' (+ fim)
  const uint16_t *adv;     // avanço de 0-9 e ':'
};
extern const VecFont VF_BEBAS, VF_OUTFIT, VF_RIGHTEOUS;
void vecText(const VecFont &f, int x, int y, int h, const char *s, uint16_t fg, uint16_t bg);
int vecWidth(const VecFont &f, const char *s, int h);
uint16_t mix16(uint16_t a, uint16_t b, int n);  // n/16 de b sobre a
// Fonte vetorial "Geo": dígito de 6s x 10s com traço de espessura w
void geoDigit(float x, float y, int d, float s, float w, uint16_t c);

// ---------- hora e data ----------
int displayHour(const UiState &s);                       // respeita o formato 12 h
void dateLabel(const UiState &s, char *out, size_t n);   // "TER 20 OUT"
void statusBar(const UiState &s, const char *right, uint16_t bg);  // data à esquerda, texto à direita

// ---------- clima ----------
void weatherIcon(int cx, int cy, float s, int code, bool day, uint16_t bg);  // s=1 -> ~40 px
const char *weatherText(int code);                                          // "Chuva", "Nublado"...
void cloudShape(int cx, int cy, float s, uint16_t c);
void sunShape(int cx, int cy, float s);
void moonShape(int cx, int cy, float s, uint16_t bg);

// ---------- mascote (sprites de assets/, ver assets.h) ----------
enum Face : uint8_t { FACE_HAPPY, FACE_SLEEP, FACE_FOCUS, FACE_RELAX, FACE_CHEER, FACE_UMBRELLA, FACE_SCARF, FACE_SHADES };
void mascot(int x, int y, int sc, uint8_t face, int32_t bg = UI_TRANSPARENT);  // tomate 16x16 x sc
// Planta do tomate conforme a sequência: semente, broto, flor, tomate verde, maduro e dourado (7+ dias)
const UiSprite *plantSprite(uint16_t streak);
void streakBadge(int x, int y, uint16_t streak, uint16_t c);  // chama + número; nada se streak == 0

// ---------- redesenho incremental ----------
// Cada área guarda o que mostrou e só repinta quando muda. Tudo é zerado ao trocar de tela.
struct Cache { char s[40]; };
bool changed(Cache &c, const char *now);  // true (e guarda) se o texto mudou
extern Cache cStatL, cStatR, cA, cB, cC;
extern int8_t lastDig[4], lastColon;
extern uint16_t lastDigCol;
extern int lastSecW, lastMin, lastHour, lastToday, lastGoal, lastRing, lastDots, lastWxOn;
extern int8_t lastFace;
extern uint16_t lastWx;  // compare com s.wx.version para saber se o clima mudou

// ---------- mostradores registrados (faces_list.h) ----------
#define X(id, fn, name, desc) void fn(const UiState &s, bool full);
UI_FACES(X)
#undef X
