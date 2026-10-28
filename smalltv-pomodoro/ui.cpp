// ui.cpp — sistema visual "Tomate" para a tela 240x240 do SmallTV.
//
// Princípios (pensados para um IPS de 1,54" visto a ~1 m):
//  - fundo preto puro (o IPS fica realmente escuro) e cartões cinza-escuro arredondados;
//  - números em pixel art sólido (mesma estética do mascote): traços grossos e contínuos,
//    legíveis de longe; sem flicker, porque cada célula é sempre repintada (cor ou fundo);
//  - só a fonte 6x8 embutida, em escala inteira (pixels nítidos, nada borrado);
//  - redesenho incremental: cada área guarda o que mostrou e só repinta se mudou.

#include "ui.h"
#include "assets.h"
#include "ui_kit.h"
#include "facepack.h"

Adafruit_GFX *G = nullptr;
#include <math.h>
#include <stdio.h>
#include <string.h>


// ---------- idioma ----------
static const char WD_L[3][7][4] PROGMEM = {{"DOM", "SEG", "TER", "QUA", "QUI", "SEX", "SAB"},
                                           {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"},
                                           {"DOM", "LUN", "MAR", "MIE", "JUE", "VIE", "SAB"}};
static const char MO_L[3][12][4] PROGMEM = {{"JAN", "FEV", "MAR", "ABR", "MAI", "JUN", "JUL", "AGO", "SET", "OUT", "NOV", "DEZ"},
                                            {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"},
                                            {"ENE", "FEB", "MAR", "ABR", "MAY", "JUN", "JUL", "AGO", "SEP", "OCT", "NOV", "DIC"}};
uint8_t uiLang = 0;
char WD[7][4] = {"DOM", "SEG", "TER", "QUA", "QUI", "SEX", "SAB"};  // português até o uiSetLang
char MO[12][4] = {"JAN", "FEV", "MAR", "ABR", "MAI", "JUN", "JUL", "AGO", "SET", "OUT", "NOV", "DEZ"};
void uiSetLang(uint8_t lang) {
  uiLang = lang < 3 ? lang : 0;
  memcpy_P(WD, WD_L[uiLang], sizeof WD);
  memcpy_P(MO, MO_L[uiLang], sizeof MO);
  uiInvalidate();
}
const char *trP(PGM_P pt, PGM_P en, PGM_P es) {
  static char buf[6][48];
  static uint8_t k = 0;
  char *b = buf[k++ % 6];
  strncpy_P(b, uiLang == 1 ? en : uiLang == 2 ? es : pt, sizeof buf[0] - 1);
  b[sizeof buf[0] - 1] = 0;
  return b;
}
// rótulos dentro do anel do Pomodoro: até 11 caracteres
static const char *phaseLabel(uint8_t p) {
  return p == PH_WORK ? tr("FOCO", "FOCUS", "FOCO") : p == PH_SHORT ? tr("PAUSA", "BREAK", "PAUSA") : p == PH_LONG ? tr("PAUSA LONGA", "LONG BREAK", "PAUSA LARGA") : "";
}

// ---------- texto (fonte 6x8 embutida) ----------
int textW(const char *s, uint8_t sz) {
  int n = strlen(s);
  return n ? n * 6 * sz - sz : 0;
}
void text(int x, int y, uint8_t sz, uint16_t c, const char *s) {
  G->setTextSize(sz);
  G->setTextColor(c);
  G->setCursor(x, y);
  G->print(s);
}
void textC(int cx, int y, uint8_t sz, uint16_t c, const char *s) { text(cx - textW(s, sz) / 2, y, sz, c, s); }
void textR(int rx, int y, uint8_t sz, uint16_t c, const char *s) { text(rx - textW(s, sz), y, sz, c, s); }

void degree(int x, int y, uint8_t sz, uint16_t c) {
  int r = sz + 1;
  G->drawCircle(x + r, y + r, r, c);
  if (sz >= 3) G->drawCircle(x + r, y + r, r - 1, c);
}
// "23°" centralizado em cx
void tempText(int cx, int y, uint8_t sz, uint16_t c, float v) {
  char b[8];
  snprintf(b, sizeof b, "%d", (int)lroundf(v));
  int w = textW(b, sz) + sz + 2 * (sz + 1) + 2;
  int x = cx - w / 2;
  text(x, y, sz, c, b);
  degree(x + textW(b, sz) + sz + 1, y, sz, c);
}

// Linha grossa com pontas arredondadas (dois triângulos + dois círculos).
void thickLine(float x0, float y0, float x1, float y1, float w, uint16_t c) {
  float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy), r = w / 2;
  if (len > 0.01f) {
    float nx = -dy / len * r, ny = dx / len * r;
    G->fillTriangle(lroundf(x0 + nx), lroundf(y0 + ny), lroundf(x1 + nx), lroundf(y1 + ny), lroundf(x1 - nx), lroundf(y1 - ny), c);
    G->fillTriangle(lroundf(x0 + nx), lroundf(y0 + ny), lroundf(x1 - nx), lroundf(y1 - ny), lroundf(x0 - nx), lroundf(y0 - ny), c);
  }
  if (r >= 1) {
    G->fillCircle(lroundf(x0), lroundf(y0), lroundf(r), c);
    G->fillCircle(lroundf(x1), lroundf(y1), lroundf(r), c);
  }
}

// ---------- cache de áreas ----------
bool changed(Cache &c, const char *now) {
  if (strcmp(c.s, now) == 0) return false;
  strncpy(c.s, now, sizeof c.s - 1);
  c.s[sizeof c.s - 1] = 0;
  return true;
}

// Tela na frente: o número do mostrador (0..THEME_COUNT-1) ou uma das telas abaixo. Os números das outras
// telas ficam longe dos mostradores: até a 1.19 o Pomodoro era 10, o mesmo do 11º mostrador (Previsão LCD),
// e ao trocar entre os dois a tela não era limpa (um aparecia por cima do outro).
enum : int8_t { SCR_PACK = 100, SCR_POMO, SCR_MSG, SCR_COLOR, SCR_ERROR, SCR_VCOM };
static_assert(THEME_COUNT < SCR_PACK, "número de mostrador colidiria com o de outra tela");
static int8_t screen = -1;
Cache cStatL, cStatR, cA, cB, cC;
int8_t lastDig[4], lastColon;
uint16_t lastDigCol;
int lastSecW, lastMin, lastHour, lastToday, lastGoal, lastRing, lastDots, lastWxOn;
int8_t lastFace;
uint16_t lastWx;

static void resetCaches() {
  cStatL.s[0] = cStatR.s[0] = cA.s[0] = cB.s[0] = cC.s[0] = 1;  // valor impossível
  cStatL.s[1] = cStatR.s[1] = cA.s[1] = cB.s[1] = cC.s[1] = 0;
  for (auto &d : lastDig) d = -2;
  lastColon = -1;
  lastDigCol = 0xFFFF;
  lastSecW = lastMin = lastHour = lastToday = lastGoal = lastRing = lastDots = lastWxOn = -1;
  lastFace = -1;
  lastWx = 0xFFFF;
}

uint32_t uiEnters = 0;  // quantas vezes a tela inteira foi trocada (main.cpp mede o tempo desse desenho)
static bool enter(int8_t id) {
  if (screen == id) return false;
  screen = id;
  uiEnters++;
  uiScrollEnd();
  resetCaches();
  G->fillScreen(BG);
  return true;
}

void uiBegin(Adafruit_GFX *gfx) {
  G = gfx;
  G->setTextWrap(false);
  uiScrollEnd();
  screen = -1;
}
static bool slideOn = false;  // a próxima tela cheia (screen == -1) substitui um mostrador do firmware
void uiInvalidate() {
  screen = -1;
  slideOn = false;
}
void uiInvalidateSlide() {
  bool was = screen >= 0 && screen < THEME_COUNT;
  screen = -1;
  slideOn = was;
}

// ---------- dígitos em pixel art 5x7 (células sólidas) ----------
static const uint8_t DM[11][7] = {
    {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},
    {0x0E, 0x11, 0x01, 0x06, 0x08, 0x10, 0x1F}, {0x1F, 0x01, 0x02, 0x06, 0x01, 0x11, 0x0E},
    {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}, {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E},
    {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}, {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
    {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}, {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C},
    {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00},  // 10 = sinal de menos
};

// p = tamanho da célula; d < 0 = dígito apagado. Células acesas são quadrados cheios
// (traço contínuo); com drawOff, as apagadas recebem "off" (a cor do fundo), o que
// substitui o dígito anterior sem limpar a área antes (sem flicker).
void dmDigit(int x, int y, int d, int p, uint16_t on, uint16_t off, bool drawOff) {
  for (int row = 0; row < 7; row++) {
    uint8_t bits = d < 0 ? 0 : DM[d][row];
    for (int col = 0; col < 5;) {
      bool lit = bits & (0x10 >> col);
      int run = 1;  // junta células vizinhas iguais num retângulo só (menos chamadas SPI)
      while (col + run < 5 && (bool)(bits & (0x10 >> (col + run))) == lit) run++;
      if (lit || drawOff) G->fillRect(x + col * p, y + row * p, run * p, p, lit ? on : off);
      col += run;
    }
  }
}


void drawTime4(const TimeLayout &L, int a, int b, int c, int d, bool colon, uint16_t on, uint16_t off) {
  int v[4] = {a, b, c, d};
  bool colChanged = on != lastDigCol;
  for (int i = 0; i < 4; i++)
    if (colChanged || lastDig[i] != v[i]) {
      dmDigit(L.x[i], L.y, v[i], L.p, on, off, true);
      lastDig[i] = v[i];
    }
  if (colChanged || lastColon != colon) {
    uint16_t cc = colon ? on : off;
    G->fillRect(L.colonX, L.y + 2 * L.p, L.p, L.p, cc);
    G->fillRect(L.colonX, L.y + 4 * L.p, L.p, L.p, cc);
    lastColon = colon;
  }
  lastDigCol = on;
}

int displayHour(const UiState &s) {
  int h = s.now.tm_hour;
  if (s.hour12) h = h % 12 == 0 ? 12 : h % 12;
  return h;
}

void dateLabel(const UiState &s, char *out, size_t n) {
  if (s.timeOk) snprintf(out, n, "%s %02d %s", WD[s.now.tm_wday], s.now.tm_mday, MO[s.now.tm_mon]);
  else snprintf(out, n, "%s", tr("SINCRONIZANDO", "SYNCING", "SINCRONIZANDO"));
}

// Barra superior: data à esquerda, texto livre (IP/hora) à direita.
void statusBar(const UiState &s, const char *right, uint16_t bg) {
  char l[24];
  dateLabel(s, l, sizeof l);
  if (changed(cStatL, l)) {
    G->fillRect(0, 4, 120, 10, bg);
    text(10, 6, 1, MUTED, l);
  }
  if (changed(cStatR, right)) {
    G->fillRect(120, 4, 120, 10, bg);
    textR(230, 6, 1, MUTED, right);
  }
}

// ---------- ícones de clima ----------
void cloudShape(int cx, int cy, float s, uint16_t c) {
  G->fillCircle(cx - 8 * s, cy + 3 * s, 8 * s, c);
  G->fillCircle(cx + 2 * s, cy - 3 * s, 11 * s, c);
  G->fillCircle(cx + 12 * s, cy + 4 * s, 7 * s, c);
  G->fillRoundRect(cx - 16 * s, cy + 3 * s, 35 * s, 9 * s, 4 * s, c);
}
void sunShape(int cx, int cy, float s) {
  for (int i = 0; i < 8; i++) {
    float a = i * K_PI / 4;
    int x0 = cx + cosf(a) * 13 * s, y0 = cy + sinf(a) * 13 * s;
    int x1 = cx + cosf(a) * 18 * s, y1 = cy + sinf(a) * 18 * s;
    for (int k = -1; k <= 1; k++) G->drawLine(x0 + k, y0, x1 + k, y1, SUN);
  }
  G->fillCircle(cx, cy, 9 * s, SUN);
}
void moonShape(int cx, int cy, float s, uint16_t bg) {
  G->fillCircle(cx, cy, 12 * s, uiRgb(244, 236, 200));
  G->fillCircle(cx + 6 * s, cy - 4 * s, 10 * s, bg);
}
// Desenha sobre uma área já limpa com "bg".
void weatherIcon(int cx, int cy, float s, int c, bool day, uint16_t bg) {
  bool rain = (c >= 51 && c <= 67) || (c >= 80 && c <= 82);
  bool snow = (c >= 71 && c <= 77) || c == 85 || c == 86;
  bool storm = c >= 95;
  if (c == 0) {
    if (day) sunShape(cx, cy, s);
    else moonShape(cx, cy, s, bg);
  } else if (c <= 2) {
    if (day) sunShape(cx - 7 * s, cy - 7 * s, s * 0.75f);
    else moonShape(cx - 7 * s, cy - 7 * s, s * 0.75f, bg);
    cloudShape(cx + 3 * s, cy + 4 * s, s * 0.8f, CLOUD);
  } else if (c == 45 || c == 48) {
    for (int i = 0; i < 4; i++) G->fillRoundRect(cx - 18 * s, cy - 10 * s + i * 7 * s, 36 * s, 3 * s + 1, 2, CLOUD);
  } else {
    cloudShape(cx, cy - 4 * s, s, storm || rain ? DCLOUD : CLOUD);
    if (rain)
      for (int i = -1; i <= 1; i++)
        for (int k = 0; k < 2; k++)
          G->drawLine(cx + i * 9 * s + k, cy + 12 * s, cx + i * 9 * s - 4 * s + k, cy + 20 * s, RAIN);
    if (snow)
      for (int i = -1; i <= 1; i++) G->fillCircle(cx + i * 9 * s, cy + 16 * s, 2 * s + 1, TEXT);
    if (storm) {
      G->fillTriangle(cx + 2 * s, cy + 6 * s, cx - 6 * s, cy + 16 * s, cx + 1 * s, cy + 15 * s, SUN);
      G->fillTriangle(cx - 1 * s, cy + 13 * s, cx + 6 * s, cy + 13 * s, cx - 4 * s, cy + 24 * s, SUN);
    }
  }
}

const char *rainSoonText(uint8_t m) {
  static char b[20];
  if (!m) return "";
  if (m < 15) return tr("chuva logo", "rain soon", "lluvia ya");
  snprintf(b, sizeof b, tr("chuva %u min", "rain %u min", "lluvia %u min"), m);  // curto: cabe ao lado do tomate
  return b;
}

const char *weatherText(int c) {
  if (c == 0) return tr("Ceu limpo", "Clear sky", "Despejado");
  if (c <= 2) return tr("Poucas nuvens", "Few clouds", "Poco nuboso");
  if (c == 3) return tr("Nublado", "Cloudy", "Nublado");
  if (c == 45 || c == 48) return tr("Neblina", "Fog", "Niebla");
  if (c >= 51 && c <= 57) return tr("Garoa", "Drizzle", "Llovizna");
  if ((c >= 61 && c <= 67) || (c >= 80 && c <= 82)) return tr("Chuva", "Rain", "Lluvia");
  if ((c >= 71 && c <= 77) || c == 85 || c == 86) return tr("Neve", "Snow", "Nieve");
  if (c >= 95) return tr("Tempestade", "Storm", "Tormenta");
  return "";
}

// ---------- sprites em pixel art (assets/*.txt -> assets.h) ----------
void uiSprite(const UiSprite *sp, int x, int y, uint8_t sc, uint8_t frame, int32_t bg) {
  UiSprite s;
  memcpy_P(&s, sp, sizeof s);
  const uint8_t *px = s.px + (frame % s.frames) * ((s.w * s.h + 1) / 2);
  auto at = [&](int i) { uint8_t b = pgm_read_byte(px + i / 2); return i & 1 ? b & 15 : b >> 4; };
  for (int r = 0; r < s.h; r++)
    for (int c = 0; c < s.w;) {
      int i = r * s.w + c, v = at(i), run = 1;  // junta pixels vizinhos iguais num retângulo só
      while (c + run < s.w && at(i + run) == v) run++;
      if (v || bg >= 0) G->fillRect(x + c * sc, y + r * sc, run * sc, sc, v ? pgm_read_word(s.pal + v - 1) : (uint16_t)bg);
      c += run;
    }
}

uint8_t uiSpriteFrame(const UiSprite *sp, uint32_t ms) {
  UiSprite s;
  memcpy_P(&s, sp, sizeof s);
  return s.frames > 1 && s.frameMs ? ms / s.frameMs % s.frames : 0;
}

// Mascote: tomate 16x16 com uma expressão por asset.
static const UiSprite *const FACES[] = {&SPR_TOMATE_FELIZ, &SPR_TOMATE_DORMINDO, &SPR_TOMATE_FOCADO, &SPR_TOMATE_RELAX,
                                        &SPR_TOMATE_COMEMORANDO, &SPR_TOMATE_GUARDA_CHUVA, &SPR_TOMATE_CACHECOL, &SPR_TOMATE_OCULOS};

void mascot(int x, int y, int sc, uint8_t face, int32_t bg) { uiSprite(FACES[face], x, y, sc, 0, bg); }

const UiSprite *plantSprite(uint16_t streak) {
  static const UiSprite *const STAGE[8] = {&SPR_PLANTA_SEMENTE, &SPR_PLANTA_BROTO, &SPR_PLANTA_FLOR, &SPR_PLANTA_FLOR,
                                           &SPR_PLANTA_VERDE, &SPR_PLANTA_VERDE, &SPR_PLANTA_MADURA, &SPR_PLANTA_DOURADA};
  return STAGE[streak < 7 ? streak : 7];
}

void streakBadge(int x, int y, uint16_t streak, uint16_t c) {
  if (!streak) return;
  char n[6];
  snprintf(n, sizeof n, "%u", streak);
  uiSprite(&SPR_CHAMA, x, y, 1);
  text(x + 10, y + 2, 1, c, n);
}

// Atividades do foco (UiActivity): objeto ao lado do tomate e título da tela do Pomodoro.
static const UiSprite *const ACTIVITY_SPRITE[ACT_COUNT] = {&SPR_LIVRO, &SPR_CADERNO, &SPR_NOTEBOOK, &SPR_PRANCHETA, &SPR_PALETA_PINTOR};
static const char *activityLabel(uint8_t a) {
  switch (a) {
    case 1: return tr("ESTUDANDO", "STUDYING", "ESTUDIANDO");
    case 2: return tr("PROGRAMANDO", "CODING", "PROGRAMANDO");
    case 3: return tr("TAREFAS", "TASKS", "TAREAS");
    case 4: return tr("DESENHANDO", "DRAWING", "DIBUJANDO");
  }
  return tr("LENDO", "READING", "LEYENDO");
}

void (*uiFrameHook)() = nullptr;     // o firmware usa para ceder tempo ao Wi-Fi entre quadros
uint32_t (*uiMillis)() = nullptr;    // relógio em ms (firmware: millis); sem ele, passos fixos de 20 ms
void (*uiBlit)(int16_t, int16_t, uint16_t *, int16_t, int16_t) = nullptr;

// ---------- rolagem vertical por hardware (ver ui.h) ----------
void (*uiScrollHw)(int16_t, int16_t) = nullptr;
static int16_t scrollRows = 0;  // 0 = parada
bool uiScrollBegin(int rows) {
  if (!uiScrollHw || rows <= 240 || rows > UI_PANEL_ROWS) return false;
  scrollRows = rows;
  uiScrollHw(rows, 0);
  return true;
}
void uiScrollTo(int offset) {
  if (!scrollRows) return;
  offset %= scrollRows;
  if (offset < 0) offset += scrollRows;
  uiScrollHw(scrollRows, offset);
}
void uiScrollEnd() {
  if (!scrollRows) return;
  scrollRows = 0;
  uiScrollHw(0, 0);
}
bool uiScrollActive() { return scrollRows != 0; }

// ---------- recorte (clip) ----------
// Os pacotes de mostrador redesenham só a área que mudou: G passa a ser este embrulho, que
// descarta o que cai fora do retângulo e repassa o resto para a tela de verdade.
UiClip uiClip = {false, 0, 0, 0, 0};
static Adafruit_GFX *realG = nullptr;

class ClipGfx : public Adafruit_GFX {
 public:
  Adafruit_GFX *t = nullptr;
  ClipGfx() : Adafruit_GFX(240, 240) {}
  static bool in(int16_t x, int16_t y) { return x >= uiClip.x0 && x < uiClip.x1 && y >= uiClip.y0 && y < uiClip.y1; }
  static bool rect(int16_t &x, int16_t &y, int16_t &w, int16_t &h) {
    if (x < uiClip.x0) { w -= uiClip.x0 - x; x = uiClip.x0; }
    if (y < uiClip.y0) { h -= uiClip.y0 - y; y = uiClip.y0; }
    if (x + w > uiClip.x1) w = uiClip.x1 - x;
    if (y + h > uiClip.y1) h = uiClip.y1 - y;
    return w > 0 && h > 0;
  }
  void drawPixel(int16_t x, int16_t y, uint16_t c) override { if (in(x, y)) t->drawPixel(x, y, c); }
  void writePixel(int16_t x, int16_t y, uint16_t c) override { if (in(x, y)) t->writePixel(x, y, c); }
  void writeFillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) override { if (rect(x, y, w, h)) t->writeFillRect(x, y, w, h, c); }
  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) override { if (rect(x, y, w, h)) t->fillRect(x, y, w, h, c); }
  void writeFastHLine(int16_t x, int16_t y, int16_t w, uint16_t c) override { int16_t h = 1; if (rect(x, y, w, h)) t->writeFastHLine(x, y, w, c); }
  void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t c) override { int16_t h = 1; if (rect(x, y, w, h)) t->drawFastHLine(x, y, w, c); }
  void writeFastVLine(int16_t x, int16_t y, int16_t h, uint16_t c) override { int16_t w = 1; if (rect(x, y, w, h)) t->writeFastVLine(x, y, h, c); }
  void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t c) override { int16_t w = 1; if (rect(x, y, w, h)) t->drawFastVLine(x, y, h, c); }
  void startWrite() override { t->startWrite(); }
  void endWrite() override { t->endWrite(); }
};
static ClipGfx clipGfx;

void uiClipBegin(int x0, int y0, int x1, int y1) {
  if (!uiClip.on) realG = G;
  uiClip = {true, (int16_t)(x0 < 0 ? 0 : x0), (int16_t)(y0 < 0 ? 0 : y0), (int16_t)(x1 > 240 ? 240 : x1), (int16_t)(y1 > 240 ? 240 : y1)};
  clipGfx.t = realG;
  clipGfx.setTextWrap(false);
  G = &clipGfx;
}

void uiClipEnd() {
  if (!uiClip.on) return;
  uiClip.on = false;
  G = realG;
}

// ---------- desenho fora da tela (uiOffscreen) ----------
// Sem frame buffer (240x240 x 2 bytes não cabe na RAM): G passa a ser esta faixa de memória, que
// recorta tudo para (bx, by, bw, bh). Os blits (fontes vetoriais, imagens) também caem nela.
class BufGfx : public Adafruit_GFX {
 public:
  uint16_t *buf = nullptr;
  int16_t bx = 0, by = 0, bw = 0, bh = 0;
  BufGfx() : Adafruit_GFX(240, 240) {}
  bool rect(int16_t &x, int16_t &y, int16_t &w, int16_t &h) {
    if (x < bx) { w -= bx - x; x = bx; }
    if (y < by) { h -= by - y; y = by; }
    if (x + w > bx + bw) w = bx + bw - x;
    if (y + h > by + bh) h = by + bh - y;
    return w > 0 && h > 0;
  }
  void fill(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) {
    if (!rect(x, y, w, h)) return;
    for (int r = 0; r < h; r++) {
      uint16_t *p = buf + (y - by + r) * bw + (x - bx);
      for (int i = 0; i < w; i++) p[i] = c;
    }
  }
  void put(int16_t x, int16_t y, const uint16_t *px, int16_t w, int16_t h) {
    for (int r = 0; r < h; r++) {
      int yy = y + r, x0 = x > bx ? x : bx, x1 = x + w < bx + bw ? x + w : bx + bw;
      if (yy >= by && yy < by + bh && x1 > x0) memcpy(buf + (yy - by) * bw + (x0 - bx), px + r * w + (x0 - x), (x1 - x0) * 2);
    }
  }
  void drawPixel(int16_t x, int16_t y, uint16_t c) override { fill(x, y, 1, 1, c); }
  void writePixel(int16_t x, int16_t y, uint16_t c) override { fill(x, y, 1, 1, c); }
  void writeFillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) override { fill(x, y, w, h, c); }
  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) override { fill(x, y, w, h, c); }
  void writeFastHLine(int16_t x, int16_t y, int16_t w, uint16_t c) override { fill(x, y, w, 1, c); }
  void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t c) override { fill(x, y, w, 1, c); }
  void writeFastVLine(int16_t x, int16_t y, int16_t h, uint16_t c) override { fill(x, y, 1, h, c); }
  void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t c) override { fill(x, y, 1, h, c); }
};
static BufGfx bufGfx;
static bool offscreen = false;

static void rawBlit(int16_t x, int16_t y, uint16_t *px, int16_t w, int16_t h) {
  if (offscreen) {
    for (int r = 0; r < h; r++) bufGfx.put(x, y + r, px + r * w, w, 1);
    return;
  }
  if (uiBlit) uiBlit(x, y, px, w, h);
  else (uiClip.on ? realG : G)->drawRGBBitmap(x, y, px, w, h);  // genérico: pixel a pixel (simulador)
}

void blit(int16_t x, int16_t y, uint16_t *px, int16_t w, int16_t h) {
  if (!uiClip.on) {
    rawBlit(x, y, px, w, h);
    return;
  }
  for (int r = 0; r < h; r++) {  // recorta linha a linha
    int yy = y + r;
    if (yy < uiClip.y0 || yy >= uiClip.y1) continue;
    int x0 = x > uiClip.x0 ? x : uiClip.x0, x1 = x + w < uiClip.x1 ? x + w : uiClip.x1;
    if (x1 > x0) rawBlit(x0, yy, px + r * w + (x0 - x), x1 - x0, 1);
  }
}

bool uiOffscreen(int x, int y, int w, int h, void (*draw)(void *), void *ctx) {
  if (offscreen) {  // já dentro de uma faixa (troca por rolagem): a cena já recorta para ela
    draw(ctx);
    return true;
  }
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > 240) w = 240 - x;
  if (y + h > 240) h = 240 - y;
  if (w <= 0 || h <= 0) return true;
  const int BYTES = 8192;  // ~8 KB por faixa: só existe durante o desenho
  int rows = BYTES / (2 * w);
  if (rows > h) rows = h;
  uint16_t *b = (uint16_t *)malloc(w * rows * 2);
  if (!b) return false;
  Adafruit_GFX *g0 = G;
  UiClip c0 = uiClip;
  for (int sy = y; sy < y + h; sy += rows) {
    int sh = y + h - sy < rows ? y + h - sy : rows;
    bufGfx.buf = b;
    bufGfx.bx = x;
    bufGfx.by = sy;
    bufGfx.bw = w;
    bufGfx.bh = sh;
    bufGfx.setTextWrap(false);
    uiClip = {true, (int16_t)x, (int16_t)sy, (int16_t)(x + w), (int16_t)(sy + sh)};  // blit() e imagens recortam aqui
    offscreen = true;
    G = &bufGfx;
    draw(ctx);
    G = g0;
    offscreen = false;
    uiClip = c0;
    rawBlit(x, sy, b, w, sh);
    if (uiFrameHook) uiFrameHook();  // cede tempo ao Wi-Fi entre as faixas
  }
  free(b);
  return true;
}

void uiPaint(bool direct, int x, int y, int w, int h, void (*draw)(void *), void *ctx) {
  if (direct || !uiOffscreen(x, y, w, h, draw, ctx)) draw(ctx);
}

// =====================================================================================
// Fonte vetorial "Geo": dígitos como traços (linhas e arcos) numa grade 6x10, em décimos.
// Sem bitmap na flash: serve em qualquer tamanho e espessura (Neon fino, Flip grosso).
// =====================================================================================
struct Stk { int8_t t; int16_t a, b, c, d, e; };  // t=0 linha (a,b)-(c,d); t=1 arco: centro (a,b), raio c, de d a e graus
static const Stk GEO0[] = {{1, 30, 30, 30, 180, 360}, {0, 0, 30, 0, 70, 0}, {0, 60, 30, 60, 70, 0}, {1, 30, 70, 30, 0, 180}};
static const Stk GEO1[] = {{0, 30, 0, 30, 100, 0}, {0, 8, 22, 30, 0, 0}};
static const Stk GEO2[] = {{1, 30, 30, 30, 180, 380}, {0, 58, 40, 0, 100, 0}, {0, 0, 100, 60, 100, 0}};
static const Stk GEO3[] = {{0, 0, 0, 60, 0, 0}, {0, 60, 0, 30, 40, 0}, {1, 30, 70, 30, 270, 510}};
static const Stk GEO4[] = {{0, 45, 100, 45, 0, 0}, {0, 45, 0, 0, 70, 0}, {0, 0, 70, 60, 70, 0}};
static const Stk GEO5[] = {{0, 60, 0, 5, 0, 0}, {0, 5, 0, 5, 48, 0}, {0, 5, 48, 2, 60, 0}, {1, 30, 70, 30, 200, 510}};
static const Stk GEO6[] = {{1, 30, 70, 30, 0, 360}, {0, 1, 62, 42, 0, 0}};
static const Stk GEO7[] = {{0, 0, 0, 60, 0, 0}, {0, 60, 0, 20, 100, 0}};
static const Stk GEO8[] = {{1, 30, 26, 26, 0, 360}, {1, 30, 70, 30, 0, 360}};
static const Stk GEO9[] = {{1, 30, 30, 30, 0, 360}, {0, 59, 38, 18, 100, 0}};
static const Stk *const GEO[10] = {GEO0, GEO1, GEO2, GEO3, GEO4, GEO5, GEO6, GEO7, GEO8, GEO9};
static const uint8_t GEO_N[10] = {4, 2, 3, 3, 3, 4, 2, 2, 2, 2};

// s = pixels por unidade da grade (o dígito ocupa 6s x 10s); w = espessura do traço.
void geoDigit(float x, float y, int d, float s, float w, uint16_t c) {
  if (d < 0 || d > 9) return;
  const float k = s / 10.0f;
  for (int i = 0; i < GEO_N[d]; i++) {
    const Stk &st = GEO[d][i];
    if (st.t == 0) {
      thickLine(x + st.a * k, y + st.b * k, x + st.c * k, y + st.d * k, w, c);
      continue;
    }
    float cx = x + st.a * k, cy = y + st.b * k, r = st.c * k;
    int steps = (st.e - st.d) / 12;
    if (steps < 6) steps = 6;
    float px = cx + r * cosf(st.d * K_PI / 180), py = cy + r * sinf(st.d * K_PI / 180);
    for (int j = 1; j <= steps; j++) {
      float a = (st.d + (st.e - st.d) * j / (float)steps) * K_PI / 180;
      float nx = cx + r * cosf(a), ny = cy + r * sinf(a);
      thickLine(px, py, nx, ny, w, c);
      px = nx;
      py = ny;
    }
  }
}

// Mistura duas cores RGB565 (f=0 -> a, f=1 -> b).
uint16_t mix(uint16_t a, uint16_t b, float f) {
  int ar = (a >> 11) & 31, ag = (a >> 5) & 63, ab = a & 31;
  int br = (b >> 11) & 31, bg = (b >> 5) & 63, bb = b & 31;
  return ((int)(ar + (br - ar) * f) << 11) | ((int)(ag + (bg - ag) * f) << 5) | (int)(ab + (bb - ab) * f);
}

// "23° Poucas nuvens" centralizado (grau desenhado à mão)
void tempDesc(int cy, uint8_t sz, uint16_t ct, uint16_t cd, float t, const char *desc) {
  char n[8];
  snprintf(n, sizeof n, "%d", (int)lroundf(t));
  int degW = 2 * (sz + 1) + sz;
  int w = textW(n, sz) + degW + 6 * sz + textW(desc, sz);
  int x = 120 - w / 2;
  text(x, cy, sz, ct, n);
  degree(x + textW(n, sz) + sz, cy, sz, ct);
  text(x + textW(n, sz) + degW + 6 * sz, cy, sz, cd, desc);
}

// Tabela gerada de faces_list.h: o número do tema indexa a função, o nome e a descrição.
static void (*const FACE_FN[THEME_COUNT])(const UiState &, bool) = {
#define X(id, fn, name, desc) fn,
    UI_FACES(X)
#undef X
};
static const char *const FACE_NAME[THEME_COUNT] = {
#define X(id, fn, name, desc) name,
    UI_FACES(X)
#undef X
};
static const char *const FACE_DESC[THEME_COUNT] = {
#define X(id, fn, name, desc) desc,
    UI_FACES(X)
#undef X
};
const char *uiFaceName(uint8_t t) { return t < THEME_COUNT ? FACE_NAME[t] : ""; }
const char *uiFaceDesc(uint8_t t) { return t < THEME_COUNT ? FACE_DESC[t] : ""; }

// ---------- troca de mostrador por rolagem de hardware ----------
// Anel de 320 linhas: o mostrador antigo (linhas 0..239) sobe e sai pelo topo, passa o vão preto da
// margem (80 linhas) e o novo entra por baixo, parando nas linhas 0..239, onde o offset volta a 0. Nada
// é copiado no fim. O novo é desenhado em faixas de 16 linhas direto nas linhas de RAM que o antigo
// já deixou, atrás da rolagem: a faixa n só é desenhada depois que o antigo saiu da tela (offset
// >= 16*(n+1)) e tem que estar pronta antes de entrar (offset <= 80 + 16*n). Cada faixa roda o
// mostrador inteiro (full) recortado nela: custa ~15 desenhos completos, mas só de CPU de desenho,
// a rolagem em si é um comando por quadro.
struct SlideCtx { const UiState *s; uint8_t t; };
static void slideStrip(void *p) {
  SlideCtx *c = (SlideCtx *)p;
  resetCaches();  // cada faixa é um desenho completo do zero
  G->fillScreen(BG);
  FACE_FN[c->t](*c->s, true);
}
static uint32_t slideNow() {
  static uint32_t fake;  // sem relógio (simulador): 20 ms por consulta
  return uiMillis ? uiMillis() : (fake += 20);
}
static bool slideTo(const UiState &s, uint8_t t) {
  const int R = UI_PANEL_ROWS, STRIP = 16, N = 240 / STRIP, GAP = R - 240;
  const uint32_t MS = 700;
  if (!uiScrollBegin(R)) return false;
  SlideCtx c = {&s, t};
  uint32_t t0 = slideNow();
  int done = 0;  // faixas desenhadas
  bool ok = true;
  for (;;) {
    uint32_t e = slideNow() - t0;
    uint32_t x = (e > MS ? MS : e) * 1024 / MS, sm = x * x * (3072 - 2 * x) >> 20;  // suaviza início e fim
    int want = R * sm / 1024, cap = done < N ? GAP + STRIP * done : R;
    int L = want < cap ? want : cap;
    uiScrollTo(L);  // L == R é offset 0 outra vez, já com o mostrador novo
    if (L >= R) break;
    if (done < N && L >= STRIP * (done + 1)) {
      if (!uiOffscreen(0, STRIP * done, 240, STRIP, slideStrip, &c)) { ok = false; break; }  // sem memória
      done++;
    } else if (uiFrameHook) uiFrameHook();
  }
  uiScrollEnd();
  return ok;  // false: quem chamou desenha o mostrador novo do jeito normal (tela cheia)
}

void uiRenderClock(const UiState &s) {
  if (s.theme == THEME_PACK) {  // mostrador instalado como pacote (facepack.cpp)
    slideOn = false;
    bool full = enter(SCR_PACK);
    facePackRender(s, full);
    return;
  }
  uint8_t t = s.theme < THEME_COUNT ? s.theme : 0;
  if (slideOn) {
    slideOn = false;
    if (screen == -1 && slideTo(s, t)) {
      screen = t;
      return;
    }
  }
  bool full = enter(t);
  FACE_FN[t](s, full);
}

// ---------- Pomodoro: anel de progresso ----------
static const TimeLayout TL_POMO = {{44, 79, 132, 167}, 70, 6, 117};

static uint16_t phaseColor(uint8_t p) { return p == PH_WORK ? WORK : p == PH_SHORT ? SHORT : LONG; }

static const int AX = 120, AY = 120;  // centro do anel

static void ringDot(int deg, uint16_t c) {
  float a = (deg - 90) * K_PI / 180;
  G->fillCircle(AX + cosf(a) * 110, AY + sinf(a) * 110, 6, c);
}

void uiRenderPomodoro(const UiState &s) {
  bool full = enter(SCR_POMO);
  uint16_t col = phaseColor(s.phase);
  // anel: trilho apagado + progresso na cor da fase, desenhado só no trecho novo
  int target = s.totalMs ? (int)((uint64_t)(s.totalMs - s.remainingMs) * 360 / s.totalMs) : 0;
  if (full || target < lastRing) {
    for (int d = 0; d < 360; d += 1) ringDot(d, DIM);
    lastRing = 0;
    ringDot(0, col);
  }
  for (int d = lastRing + 1; d <= target; d++) ringDot(d, col);
  if (target > lastRing) lastRing = target;

  uint8_t act = s.activity < ACT_COUNT ? s.activity : ACT_READ;
  const char *label = s.phase == PH_WORK ? activityLabel(act) : phaseLabel(s.phase);
  if (changed(cA, label)) {
    G->fillRect(50, 44, 140, 18, BG);  // dentro do anel (raio interno 104)
    textC(120, 46, 2, col, label);
  }

  uint32_t secs = (s.remainingMs + 999) / 1000;
  int m = secs / 60 > 99 ? 99 : secs / 60, sec = secs % 60;
  // pausado: os dígitos piscam devagar
  bool show = s.running || (s.timeOk ? s.now.tm_sec % 2 == 0 : true);
  if (show) drawTime4(TL_POMO, m / 10, m % 10, sec / 10, sec % 10, true, col, BG);
  else drawTime4(TL_POMO, -1, -1, -1, -1, false, col, BG);

  int total = s.longEvery ? s.longEvery : 1;
  int done = s.phase == PH_LONG ? total : s.cycleDone % total;
  if (done != lastDots || total != lastGoal) {
    G->fillRect(40, 122, 160, 14, BG);
    int gap = 16, x0 = 120 - (total - 1) * gap / 2;
    for (int i = 0; i < total; i++) {
      if (i < done) G->fillCircle(x0 + i * gap, 129, 4, WORK);
      else G->drawCircle(x0 + i * gap, 129, 4, MUTED);
    }
    lastDots = done;
    lastGoal = total;
  }

  // tomate + objeto da atividade (foco) ou café (pausa); quando uma fase termina, a animação de fim
  int8_t key;  // rosto * 8 + objeto (0..ACT_COUNT-1 = atividade, 7 = café); 64+ = quadro da animação
  if (s.celebrate) key = 64 + uiSpriteFrame(&SPR_TOMATE_FIM, s.ms);
  else key = (!s.running ? FACE_SLEEP : s.phase == PH_WORK ? FACE_FOCUS : FACE_RELAX) * 8 + (s.phase == PH_WORK ? act : 7);
  if (key != lastFace) {
    if ((key >= 64) != (lastFace >= 64)) G->fillRect(84, 136, 72, 44, BG);  // troca de layout
    if (key >= 64) uiSprite(&SPR_TOMATE_FIM, 104, 138, 2, key - 64, BG);    // bg repinta o quadro anterior
    else {
      mascot(86, 144, 2, key / 8, BG);
      uiSprite(key % 8 == 7 ? &SPR_CAFE : ACTIVITY_SPRITE[key % 8], 122, 144, 2, 0, BG);
    }
    lastFace = key;
  }

  char st[24];
  if (!s.running) snprintf(st, sizeof st, "%s", tr("PAUSADO", "PAUSED", "PAUSADO"));
  else if (s.timeOk) {
    struct tm t = s.now, e;
    time_t end = mktime(&t) + secs;
    localtime_r(&end, &e);
    snprintf(st, sizeof st, tr("termina %02d:%02d", "ends %02d:%02d", "termina %02d:%02d"), e.tm_hour, e.tm_min);
  } else st[0] = 0;
  if (changed(cB, st)) {
    G->fillRect(50, 184, 140, 10, BG);
    textC(120, 186, 1, s.running ? MUTED : TEXT, st);
  }
}

// ---------- telas avulsas ----------
void uiMessage(const char *title, const char *l1, const char *l2, const char *l3) {
  uiScrollEnd();
  screen = SCR_MSG;
  G->fillScreen(BG);
  mascot(96, 26, 3, FACE_HAPPY);
  textC(120, 90, 2, TEXT, title);
  textC(120, 118, 2, WORK, l1);
  textC(120, 150, 1, MUTED, l2);
  textC(120, 164, 1, MUTED, l3);
}

// Tela de início: duas etapas (Wi-Fi, hora). done = quantas já terminaram: bolinhas cheias e, enquanto falta
// alguma, uma barra que vai e volta (não há como saber quanto falta); pronto, a barra enche. Desenha só a
// faixa de baixo, sem refazer a tela.
void uiBootProgress(uint32_t ms, uint8_t done) {
  if (screen != SCR_MSG) return;
  const int bx = 40, bw = 160, by = 206, sw = 48;
  for (int i = 0; i < 2; i++) {
    G->fillCircle(112 + i * 16, 192, 4, BG);
    if (i < done) G->fillCircle(112 + i * 16, 192, 4, WORK);
    else G->drawCircle(112 + i * 16, 192, 4, MUTED);
  }
  if (done >= 2) {
    G->fillRect(bx, by, bw, 6, WORK);
    return;
  }
  uint32_t p = ms % 1400;
  int x = bx + (int)((p < 700 ? p : 1400 - p) * (bw - sw) / 700);
  G->fillRect(bx, by, bw, 6, CARD2);
  G->fillRect(x, by, sw, 6, WORK);
}

// Enquanto o relógio não tem hora: quatro pontinhos no alto, um aceso que vai e volta, por cima do mostrador
void uiSyncHint(uint32_t ms) {
  int p = ms / 200 % 6, on = p < 4 ? p : 6 - p;
  for (int i = 0; i < 4; i++) G->fillCircle(108 + i * 8, 5, 2, i == on ? WORK : MUTED);
}

void uiColorTest() {
  uiScrollEnd();
  screen = SCR_COLOR;
  G->fillScreen(BG);
  textC(120, 8, 1, TEXT, tr("TESTE DE CORES", "COLOR TEST", "PRUEBA DE COLOR"));
  const uint16_t c[4] = {0xF800, 0x07E0, 0x001F, 0xFFFF};
  const char *n[4] = {tr("VERMELHO", "RED", "ROJO"), tr("VERDE", "GREEN", "VERDE"), tr("AZUL", "BLUE", "AZUL"), tr("BRANCO", "WHITE", "BLANCO")};
  for (int i = 0; i < 4; i++) {
    G->fillRect(i * 60 + 4, 26, 52, 160, c[i]);
    textC(i * 60 + 30, 194, 1, TEXT, n[i]);
  }
  textC(120, 218, 1, MUTED, tr("o 1o deve ser vermelho e o", "the 1st should be red and the", "la 1a debe ser roja y el"));
  textC(120, 228, 1, MUTED, tr("fundo preto", "background black", "fondo negro"));
}

// Teste anti-fantasma: cinza médio alternado com preto pixel a pixel. Com o VCOM (tensão comum do
// painel) fora do ponto, os pixels "positivos" e "negativos" de cada quadro ficam com brilhos
// diferentes e a estampa tremula; no ponto certo ela fica parada, e a imagem fantasma é a menor.
// Quatro estampas porque o painel pode inverter a polaridade por ponto, por linha ou por coluna.
void uiVcomTest(uint8_t vcom, bool full) {
  static const uint16_t GRAY = uiRgb(128, 128, 128);
  if (full) {
    uiScrollEnd();
    screen = SCR_VCOM;
    G->fillScreen(BG);
    textC(120, 6, 1, TEXT, tr("ANTI-FANTASMA", "ANTI-GHOSTING", "ANTIFANTASMA"));
    for (int q = 0; q < 4; q++) {
      const int x0 = 8 + (q % 2) * 116, y0 = 40 + (q / 2) * 92;
      for (int y = 0; y < 84; y++)
        for (int x = 0; x < 108; x++) {
          bool on = q == 0 ? ((x + y) & 1) : q == 1 ? (y & 1) : q == 2 ? (x & 1) : true;
          if (on) G->drawPixel(x0 + x, y0 + y, GRAY);
        }
    }
    textC(120, 218, 1, MUTED, tr("mova ate a estampa parar", "move until the pattern stops", "mueve hasta que no tiemble"));
    textC(120, 228, 1, MUTED, tr("de tremer", "flickering", "el patron"));
  }
  int mv = 100 + 25 * vcom;  // registrador VCOMS do ST7789: 0,100 V + 25 mV por passo
  char v[20];
  snprintf(v, sizeof v, "VCOM %d%c%03d V", mv / 1000, uiLang == 1 ? '.' : ',', mv % 1000);
  G->fillRect(0, 18, 240, 18, BG);
  textC(120, 20, 2, vcom == UI_VCOM_DEFAULT ? TEXT : SUN, v);
}

void uiError(const char *msg) {
  uiScrollEnd();
  screen = SCR_ERROR;
  G->fillScreen(BG);
  textC(120, 112, 2, MUTED, msg);
}
