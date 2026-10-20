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

extern const char *const WD[7];   // "DOM".."SAB"
extern const char *const MO[12];  // "JAN".."DEZ"

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

// ---------- dígitos ----------
// Pixel art 5x7 sólido: p = tamanho da célula (dígito = 5p x 7p); d < 0 apaga; DM_MINUS = "-".
static const int DM_MINUS = 10;
void dmDigit(int x, int y, int d, int p, uint16_t on, uint16_t off, bool drawOff);
// HH:MM em pixel art com redesenho incremental (usa lastDig/lastColon)
struct TimeLayout { int x[4]; int y; int p; int colonX; };
void drawTime4(const TimeLayout &L, int a, int b, int c, int d, bool colon, uint16_t on, uint16_t off);
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
