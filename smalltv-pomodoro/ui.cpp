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
#include <math.h>
#include <stdio.h>
#include <string.h>

static Adafruit_GFX *G = nullptr;
static const float K_PI = 3.14159265f;

// ---------- paleta ----------
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

static const char *WD[7] = {"DOM", "SEG", "TER", "QUA", "QUI", "SEX", "SAB"};
static const char *MO[12] = {"JAN", "FEV", "MAR", "ABR", "MAI", "JUN", "JUL", "AGO", "SET", "OUT", "NOV", "DEZ"};
static const char *PHASE_LABEL[4] = {"", "FOCO", "PAUSA", "PAUSA LONGA"};

// ---------- texto (fonte 6x8 embutida) ----------
static int textW(const char *s, uint8_t sz) {
  int n = strlen(s);
  return n ? n * 6 * sz - sz : 0;
}
static void text(int x, int y, uint8_t sz, uint16_t c, const char *s) {
  G->setTextSize(sz);
  G->setTextColor(c);
  G->setCursor(x, y);
  G->print(s);
}
static void textC(int cx, int y, uint8_t sz, uint16_t c, const char *s) { text(cx - textW(s, sz) / 2, y, sz, c, s); }
static void textR(int rx, int y, uint8_t sz, uint16_t c, const char *s) { text(rx - textW(s, sz), y, sz, c, s); }

static void degree(int x, int y, uint8_t sz, uint16_t c) {
  int r = sz + 1;
  G->drawCircle(x + r, y + r, r, c);
  if (sz >= 3) G->drawCircle(x + r, y + r, r - 1, c);
}
// "23°" centralizado em cx
static void tempText(int cx, int y, uint8_t sz, uint16_t c, float v) {
  char b[8];
  snprintf(b, sizeof b, "%d", (int)lroundf(v));
  int w = textW(b, sz) + sz + 2 * (sz + 1) + 2;
  int x = cx - w / 2;
  text(x, y, sz, c, b);
  degree(x + textW(b, sz) + sz + 1, y, sz, c);
}

// Linha grossa com pontas arredondadas (dois triângulos + dois círculos).
static void thickLine(float x0, float y0, float x1, float y1, float w, uint16_t c) {
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
struct Cache { char s[40]; };
static bool changed(Cache &c, const char *now) {
  if (strcmp(c.s, now) == 0) return false;
  strncpy(c.s, now, sizeof c.s - 1);
  c.s[sizeof c.s - 1] = 0;
  return true;
}

static int8_t screen = -1;  // 0..3 temas, 10 Pomodoro, 20+ telas avulsas
static Cache cStatL, cStatR, cA, cB, cC;
static int8_t lastDig[4], lastColon;
static uint16_t lastDigCol;
static int lastSecW, lastMin, lastHour, lastToday, lastGoal, lastRing, lastDots, lastWxOn;
static int8_t lastFace;
static uint16_t lastWx;

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

static bool enter(int8_t id) {
  if (screen == id) return false;
  screen = id;
  resetCaches();
  G->fillScreen(BG);
  return true;
}

void uiBegin(Adafruit_GFX *gfx) {
  G = gfx;
  G->setTextWrap(false);
  screen = -1;
}
void uiInvalidate() { screen = -1; }

// ---------- dígitos em pixel art 5x7 (células sólidas) ----------
static const uint8_t DM[11][7] = {
    {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},
    {0x0E, 0x11, 0x01, 0x06, 0x08, 0x10, 0x1F}, {0x1F, 0x01, 0x02, 0x06, 0x01, 0x11, 0x0E},
    {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}, {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E},
    {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}, {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
    {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}, {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C},
    {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00},  // 10 = sinal de menos
};
static const int DM_MINUS = 10;

// p = tamanho da célula; d < 0 = dígito apagado. Células acesas são quadrados cheios
// (traço contínuo); com drawOff, as apagadas recebem "off" (a cor do fundo), o que
// substitui o dígito anterior sem limpar a área antes (sem flicker).
static void dmDigit(int x, int y, int d, int p, uint16_t on, uint16_t off, bool drawOff) {
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

struct TimeLayout { int x[4]; int y; int p; int colonX; };

static void drawTime4(const TimeLayout &L, int a, int b, int c, int d, bool colon, uint16_t on, uint16_t off) {
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

static int displayHour(const UiState &s) {
  int h = s.now.tm_hour;
  if (s.hour12) h = h % 12 == 0 ? 12 : h % 12;
  return h;
}

static void dateLabel(const UiState &s, char *out, size_t n) {
  if (s.timeOk) snprintf(out, n, "%s %02d %s", WD[s.now.tm_wday], s.now.tm_mday, MO[s.now.tm_mon]);
  else snprintf(out, n, "SINCRONIZANDO");
}

// Barra superior: data à esquerda, texto livre (IP/hora) à direita.
static void statusBar(const UiState &s, const char *right, uint16_t bg) {
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
static void cloudShape(int cx, int cy, float s, uint16_t c) {
  G->fillCircle(cx - 8 * s, cy + 3 * s, 8 * s, c);
  G->fillCircle(cx + 2 * s, cy - 3 * s, 11 * s, c);
  G->fillCircle(cx + 12 * s, cy + 4 * s, 7 * s, c);
  G->fillRoundRect(cx - 16 * s, cy + 3 * s, 35 * s, 9 * s, 4 * s, c);
}
static void sunShape(int cx, int cy, float s) {
  for (int i = 0; i < 8; i++) {
    float a = i * K_PI / 4;
    int x0 = cx + cosf(a) * 13 * s, y0 = cy + sinf(a) * 13 * s;
    int x1 = cx + cosf(a) * 18 * s, y1 = cy + sinf(a) * 18 * s;
    for (int k = -1; k <= 1; k++) G->drawLine(x0 + k, y0, x1 + k, y1, SUN);
  }
  G->fillCircle(cx, cy, 9 * s, SUN);
}
static void moonShape(int cx, int cy, float s, uint16_t bg) {
  G->fillCircle(cx, cy, 12 * s, uiRgb(244, 236, 200));
  G->fillCircle(cx + 6 * s, cy - 4 * s, 10 * s, bg);
}
// Desenha sobre uma área já limpa com "bg".
static void weatherIcon(int cx, int cy, float s, int c, bool day, uint16_t bg) {
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

static const char *weatherText(int c) {
  if (c == 0) return "Ceu limpo";
  if (c <= 2) return "Poucas nuvens";
  if (c == 3) return "Nublado";
  if (c == 45 || c == 48) return "Neblina";
  if (c >= 51 && c <= 57) return "Garoa";
  if ((c >= 61 && c <= 67) || (c >= 80 && c <= 82)) return "Chuva";
  if ((c >= 71 && c <= 77) || c == 85 || c == 86) return "Neve";
  if (c >= 95) return "Tempestade";
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
enum Face : uint8_t { FACE_HAPPY, FACE_SLEEP, FACE_FOCUS, FACE_RELAX, FACE_CHEER, FACE_UMBRELLA, FACE_SCARF, FACE_SHADES };
static const UiSprite *const FACES[] = {&SPR_TOMATE_FELIZ, &SPR_TOMATE_DORMINDO, &SPR_TOMATE_FOCADO, &SPR_TOMATE_RELAX,
                                        &SPR_TOMATE_COMEMORANDO, &SPR_TOMATE_GUARDA_CHUVA, &SPR_TOMATE_CACHECOL, &SPR_TOMATE_OCULOS};

static void mascot(int x, int y, int sc, uint8_t face, int32_t bg = UI_TRANSPARENT) { uiSprite(FACES[face], x, y, sc, 0, bg); }

// ---------- tema 0: MATRIZ ----------
static const TimeLayout TL_CLOCK = {{20, 66, 136, 182}, 30, 8, 116};

static void weatherCard(const UiState &s, int x, int y) {
  const int w = 108, h = 120, cx = x + w / 2;
  G->fillRoundRect(x, y, w, h, 12, CARD);
  char label[16];
  snprintf(label, sizeof label, "%.15s", s.city && s.city[0] ? s.city : "CLIMA");
  text(x + 12, y + 10, 1, MUTED, label);
  if (s.weatherOn && s.wx.ok) {
    weatherIcon(cx, y + 42, 0.8f, s.wx.code, s.wx.day, CARD);
    tempText(cx, y + 70, 3, TEXT, s.wx.t);
    char mm[20];
    snprintf(mm, sizeof mm, "%d / %d", (int)lroundf(s.wx.tmin), (int)lroundf(s.wx.tmax));
    textC(cx, y + 102, 1, MUTED, mm);
  } else {
    textC(cx, y + 50, 3, DIM, "--");
    textC(cx, y + 92, 1, MUTED, s.weatherOn ? "buscando..." : "desligado");
  }
}

static void focusCard(const UiState &s, int x, int y) {
  const int w = 108, h = 120, cx = x + w / 2;
  G->fillRoundRect(x, y, w, h, 12, CARD);
  text(x + 12, y + 10, 1, MUTED, "FOCOS HOJE");
  int goal = s.dailyGoal ? s.dailyGoal : 1;
  bool done = s.today >= goal;
  char n[8];
  snprintf(n, sizeof n, "%u", s.today);
  if (done) {  // meta batida: o número vai para a esquerda e o tomate comemora ao lado
    textC(x + 36, y + 30, 5, TEXT, n);
    mascot(x + 68, y + 34, 2, FACE_CHEER);
  } else {
    mascot(x + w - 22, y + 6, 1, FACE_HAPPY);
    textC(cx, y + 30, 5, TEXT, n);
  }
  char g[20];
  snprintf(g, sizeof g, "meta %d", goal);
  textC(cx, y + 76, 1, MUTED, g);
  const int bx = x + 12, bw = w - 24, by = y + 92;
  G->fillRoundRect(bx, by, bw, 8, 4, CARD2);
  int fill = (done ? goal : s.today) * bw / goal;
  if (fill > 0) G->fillRoundRect(bx, by, fill < 8 ? 8 : fill, 8, 4, done ? SHORT : WORK);
  if (done) textC(cx, y + 106, 1, SHORT, "meta batida!");
  else {
    snprintf(g, sizeof g, "faltam %d", goal - s.today);
    textC(cx, y + 106, 1, MUTED, g);
  }
}

static void themeMatrix(const UiState &s, bool full) {
  statusBar(s, s.ip ? s.ip : "", BG);
  if (!s.timeOk) {
    drawTime4(TL_CLOCK, -1, -1, -1, -1, true, s.accent, BG);
  } else {
    int h = displayHour(s);
    drawTime4(TL_CLOCK, h / 10, h % 10, s.now.tm_min / 10, s.now.tm_min % 10, s.now.tm_sec % 2 == 0, s.accent, BG);
    // barra de segundos: anda um pouco a cada segundo e zera na virada do minuto
    int w = (s.now.tm_sec + 1) * 200 / 60;
    if (w < lastSecW || lastSecW < 0) {
      G->fillRect(20, 96, 200, 3, DIM);
      lastSecW = 0;
    }
    if (w > lastSecW) {
      G->fillRect(20 + lastSecW, 96, w - lastSecW, 3, s.accent);
      lastSecW = w;
    }
  }
  if (full || lastWx != s.wx.version || lastWxOn != (int)s.weatherOn) {
    weatherCard(s, 8, 112);
    lastWx = s.wx.version;
    lastWxOn = s.weatherOn;
  }
  if (full || lastToday != s.today || lastGoal != s.dailyGoal) {
    focusCard(s, 124, 112);
    lastToday = s.today;
    lastGoal = s.dailyGoal;
  }
}

// ---------- tema 1: TOMATE (cena em pixel art) ----------
// Céu de 0 a GROUND_Y; chão (grama + terra) de GROUND_Y até o fim. Relógio grande no topo.
static const int GROUND_Y = 196;
struct Rgb { uint8_t r, g, b; };
static void skyColors(int hour, Rgb &top, Rgb &bot) {
  if (hour >= 6 && hour < 8) { top = {59, 74, 140}; bot = {244, 162, 97}; }        // amanhecer
  else if (hour >= 8 && hour < 17) { top = {46, 134, 222}; bot = {154, 208, 245}; } // dia
  else if (hour >= 17 && hour < 19) { top = {91, 58, 140}; bot = {242, 120, 92}; }  // entardecer
  else { top = {7, 11, 31}; bot = {27, 37, 80}; }                                   // noite
}
static bool isNight(int hour) { return hour >= 19 || hour < 6; }
static uint16_t skyAt(int y, int hour) {
  Rgb t, b;
  skyColors(hour, t, b);
  int band = y / 10;
  float f = band / (GROUND_Y / 10 - 1.0f);
  if (f > 1) f = 1;
  return uiRgb(t.r + (b.r - t.r) * f, t.g + (b.g - t.g) * f, t.b + (b.b - t.b) * f);
}
// Pinta o céu (faixas de 10 px) num retângulo, com estrelas se for noite.
static void skyRect(int x, int y, int w, int h, int hour) {
  for (int by = y; by < y + h;) {
    int end = (by / 10 + 1) * 10;
    if (end > y + h) end = y + h;
    G->fillRect(x, by, w, end - by, skyAt(by, hour));
    by = end;
  }
  if (isNight(hour))
    for (int i = 0; i < 28; i++) {
      int sx = (i * 97 + 13) % 240, sy = (i * 53 + 7) % (GROUND_Y - 30);
      if (sx >= x && sx < x + w && sy >= y && sy < y + h) {
        G->drawPixel(sx, sy, i % 3 ? uiRgb(200, 200, 230) : TEXT);
        if (i % 5 == 0 && sx + 1 < x + w) G->drawPixel(sx + 1, sy, uiRgb(150, 150, 190));
      }
    }
}

// Expressão do tomate na cena: dorme à noite; de dia reage à chuva, à meta e à temperatura.
static uint8_t sceneFace(const UiState &s, bool night, bool rain, bool snow) {
  bool show = s.weatherOn && s.wx.ok;
  if (night) return FACE_SLEEP;
  if (rain) return FACE_UMBRELLA;
  if (s.dailyGoal && s.today >= s.dailyGoal) return FACE_CHEER;
  if (snow || (show && s.wx.t <= 12)) return FACE_SCARF;
  if (show && s.wx.t >= 30) return FACE_SHADES;
  return FACE_HAPPY;
}

static void tomatoScene(const UiState &s) {
  int hour = s.timeOk ? s.now.tm_hour : 12;
  skyRect(0, 0, 240, GROUND_Y, hour);
  // sol ou lua num arco que acompanha a hora (fica abaixo da área do relógio)
  float hf = hour + 0.5f;  // meio da hora: a cena só é redesenhada quando a hora muda
  bool night = isNight(hour);
  float f = night ? fmodf(hf - 19 + 24, 24) / 11.0f : (hf - 6) / 13.0f;
  if (f < 0) f = 0;
  if (f > 1) f = 1;
  int bx = 24 + f * 192, by = 178 - sinf(f * K_PI) * 66;  // arco abaixo do relógio (topo do astro >= 95)
  uiSprite(night ? &SPR_LUA : &SPR_SOL, bx - 16, by - 16, 2);
  // clima no céu
  int c = s.wx.code;
  bool show = s.weatherOn && s.wx.ok;
  bool cloudy = show && c >= 2 && c != 45 && c != 48;
  bool rain = show && ((c >= 51 && c <= 67) || (c >= 80 && c <= 82) || c >= 95);
  bool snow = show && ((c >= 71 && c <= 77) || c == 85 || c == 86);
  if (cloudy) {
    const UiSprite *cl = rain ? &SPR_NUVEM_CHUVA : &SPR_NUVEM;
    uiSprite(cl, 16, 100, 3);
    uiSprite(cl, 172, 106, 3);
  }
  if (rain)
    for (int i = 0; i < 16; i++) G->fillRect((i * 37 + 20) % 220 + 10, 126 + (i * 23) % 60, 2, 6, RAIN);
  if (snow)
    for (int i = 0; i < 18; i++) G->fillRect((i * 41 + 9) % 228 + 6, 124 + (i * 29) % 64, 3, 3, TEXT);
  if (show && (c == 45 || c == 48))
    for (int i = 0; i < 4; i++) G->fillRect(0, 124 + i * 16, 240, 4, uiRgb(190, 196, 206));
  // chão: grama + faixa fina de terra com textura de pixels
  G->fillRect(0, GROUND_Y, 240, 6, uiRgb(76, 175, 80));
  G->fillRect(0, GROUND_Y + 6, 240, 4, uiRgb(46, 125, 50));
  G->fillRect(0, GROUND_Y + 10, 240, 240 - GROUND_Y - 10, uiRgb(92, 60, 42));
  for (int i = 0; i < 12; i++) G->fillRect((i * 53 + 21) % 230, GROUND_Y - 4, 2, 4, uiRgb(76, 175, 80));  // tufos
  // mascote em pé na grama
  mascot(88, GROUND_Y + 8 - 64, 4, sceneFace(s, night, rain, snow));
  if (night) {
    text(150, GROUND_Y - 58, 2, uiRgb(220, 220, 255), "z");
    text(164, GROUND_Y - 72, 1, uiRgb(220, 220, 255), "z");
  }
  // balão com a temperatura
  if (show) {
    const uint16_t bub = uiRgb(250, 250, 250);
    const int by0 = GROUND_Y - 52;
    G->fillRoundRect(166, by0, 62, 30, 9, bub);
    G->fillTriangle(168, by0 + 10, 168, by0 + 22, 156, by0 + 20, bub);
    tempText(197, by0 + 8, 2, uiRgb(40, 40, 46), s.wx.t);
  }
}

static void tomatoBottom(const UiState &s) {
  const uint16_t soil = uiRgb(92, 60, 42);
  G->fillRect(0, GROUND_Y + 10, 240, 240 - GROUND_Y - 10, soil);
  for (int i = 0; i < 14; i++) G->fillRect((i * 29 + 7) % 236, GROUND_Y + 12 + (i * 7) % 24, 3, 3, uiRgb(74, 47, 33));
  char b[40];
  snprintf(b, sizeof b, "%u %s hoje  %s", s.today, s.today == 1 ? "foco" : "focos", s.ip ? s.ip : "");
  // texto com contorno escuro para ler por cima da textura da terra
  textC(121, GROUND_Y + 22, 1, uiRgb(50, 30, 20), b);
  textC(120, GROUND_Y + 21, 1, uiRgb(255, 226, 190), b);
}

static void tomatoClock(const UiState &s) {
  int hour = s.timeOk ? s.now.tm_hour : 12;
  skyRect(10, 2, 222, 92, hour);
  // dígitos grandes (mesmo tamanho do tema Matriz) com sombra; o céu aparece em volta
  const int X[4] = {20, 66, 136, 182}, Y = 8, P = 8;
  const uint16_t shadow = uiRgb(10, 16, 36);
  int v[4] = {-1, -1, -1, -1};
  if (s.timeOk) {
    int h = displayHour(s);
    v[0] = h / 10; v[1] = h % 10; v[2] = s.now.tm_min / 10; v[3] = s.now.tm_min % 10;
  }
  for (int i = 0; i < 4; i++) dmDigit(X[i] + 2, Y + 2, v[i], P, shadow, 0, false);
  for (int i = 0; i < 4; i++) dmDigit(X[i], Y, v[i], P, TEXT, 0, false);
  for (int k = 0; k < 2; k++) {
    G->fillRect(118, Y + 2 * P + k * 2 * P + 2, P, P, shadow);
    G->fillRect(116, Y + 2 * P + k * 2 * P, P, P, TEXT);
  }
  char d[24];
  dateLabel(s, d, sizeof d);
  textC(122, 74, 2, shadow, d);
  textC(120, 72, 2, TEXT, d);
}

static void themeTomato(const UiState &s, bool full) {
  int hour = s.timeOk ? s.now.tm_hour : -1;
  int minute = s.timeOk ? s.now.tm_min : -1;
  int goalDone = s.dailyGoal && s.today >= s.dailyGoal;  // o tomate comemora a meta batida
  bool scene = full || hour != lastHour || lastWx != s.wx.version || lastWxOn != (int)s.weatherOn || goalDone != lastGoal;
  if (scene) {
    tomatoScene(s);
    lastHour = hour;
    lastWx = s.wx.version;
    lastWxOn = s.weatherOn;
    lastGoal = goalDone;
  }
  if (scene || minute != lastMin) {
    tomatoClock(s);
    lastMin = minute;
  }
  if (scene || lastToday != s.today) {
    tomatoBottom(s);
    lastToday = s.today;
  }
}

// ---------- tema 2: ANALÓGICO ----------
static const int AX = 120, AY = 120;
static float lastHourA = -1, lastMinA = -1;

// Ponteiro "bastão": espessura constante e ponta redonda (a ponta fina some na tela pequena).
// Apagar = desenhar a mesma geometria com a cor do fundo.
static void hand(float a, int len, int width, uint16_t color) {
  float sa = sinf(a), ca = cosf(a);
  thickLine(AX - sa * 14, AY + ca * 14, AX + sa * len, AY - ca * len, width, color);
}

static void chip(int cy, int w, int h) { G->fillRoundRect(AX - w / 2, cy - h / 2, w, h, h / 2, CARD); }

static void analogChips(const UiState &s) {
  G->fillRect(40, 56, 160, 28, BG);   // faixa da data (marcas ficam em x<36 e x>204)
  G->fillRect(34, 154, 172, 24, BG);  // faixa do clima
  char d[24];
  if (s.timeOk) snprintf(d, sizeof d, "%s %02d", WD[s.now.tm_wday], s.now.tm_mday);
  else snprintf(d, sizeof d, "--");
  chip(70, textW(d, 2) + 24, 26);
  textC(AX, 63, 2, TEXT, d);
  if (s.weatherOn && s.wx.ok) {
    char t[8];
    snprintf(t, sizeof t, "%d", (int)lroundf(s.wx.t));
    const char *desc = weatherText(s.wx.code);
    int w = textW(t, 1) + 6 + 8 + textW(desc, 1);
    chip(166, w + 24, 20);
    int x = AX - w / 2;
    text(x, 163, 1, TEXT, t);
    degree(x + textW(t, 1) + 1, 162, 1, TEXT);
    text(x + textW(t, 1) + 14, 163, 1, MUTED, desc);
  }
}

static void themeAnalog(const UiState &s, bool full) {
  if (full) {
    for (int i = 0; i < 60; i++) {
      float a = i * K_PI / 30, sa = sinf(a), ca = cosf(a);
      if (i % 5) {  // minutos: ponto discreto
        G->fillCircle(AX + sa * 108, AY - ca * 108, 1, MUTED);
        continue;
      }
      // horas: traço grosso; 12/3/6/9 mais longos e na cor de destaque
      bool q = i % 15 == 0;
      thickLine(AX + sa * (q ? 92 : 97), AY - ca * (q ? 92 : 97), AX + sa * 108, AY - ca * 108, q ? 6 : 4, q ? WORK : TEXT);
    }
    lastHourA = lastMinA = -1;
  }
  int minute = s.timeOk ? s.now.tm_min : 0;
  if (!full && minute == lastMin && lastWx == s.wx.version && lastWxOn == (int)s.weatherOn) return;
  if (lastMinA >= 0) {  // apaga os ponteiros antigos
    hand(lastHourA, 56, 8, BG);
    hand(lastMinA, 84, 5, BG);
  }
  analogChips(s);
  lastMinA = minute * K_PI / 30;
  lastHourA = ((s.timeOk ? s.now.tm_hour % 12 : 10) + minute / 60.0f) * K_PI / 6;
  hand(lastHourA, 56, 8, TEXT);
  hand(lastMinA, 84, 5, s.accent);
  G->fillCircle(AX, AY, 8, WORK);
  G->fillCircle(AX, AY, 3, BG);
  lastMin = minute;
  lastWx = s.wx.version;
  lastWxOn = s.weatherOn;
}

// ---------- tema 3: CLIMA ----------
static void miniCard(int x, const char *label, const char *value, bool deg) {
  const int y = 160, w = 70, h = 72, cx = x + w / 2;
  G->fillRoundRect(x, y, w, h, 12, CARD);
  textC(cx, y + 12, 1, MUTED, label);
  int vw = textW(value, 3) + (deg ? 10 : 0);
  text(cx - vw / 2, y + 34, 3, TEXT, value);
  if (deg) degree(cx - vw / 2 + textW(value, 3) + 3, y + 34, 2, TEXT);
}

static void themeWeather(const UiState &s, bool full) {
  char hm[8] = "--:--";
  if (s.timeOk) snprintf(hm, sizeof hm, "%02d:%02d", displayHour(s), s.now.tm_min);
  char l[24];
  dateLabel(s, l, sizeof l);
  if (changed(cStatL, l)) {
    G->fillRect(0, 4, 140, 10, BG);
    text(10, 7, 1, MUTED, l);
  }
  if (changed(cC, hm)) {
    G->fillRect(150, 0, 90, 22, BG);
    textR(230, 4, 2, s.accent, hm);
  }
  if (!(full || lastWx != s.wx.version || lastWxOn != (int)s.weatherOn)) return;
  lastWx = s.wx.version;
  lastWxOn = s.weatherOn;
  G->fillRect(0, 24, 240, 216, BG);
  if (!(s.weatherOn && s.wx.ok)) {
    textC(120, 96, 2, MUTED, s.weatherOn ? "Buscando clima" : "Clima desligado");
    textC(120, 124, 1, MUTED, "defina a cidade no painel");
    return;
  }
  weatherIcon(64, 82, 1.6f, s.wx.code, s.wx.day, BG);
  int v = (int)lroundf(s.wx.t);
  if (v < -9) v = -9;
  if (v > 99) v = 99;
  const int P = 7, Y = 52;
  if (v < 0) {
    dmDigit(124, Y, DM_MINUS, P, TEXT, BG, true);
    dmDigit(164, Y, -v, P, TEXT, BG, true);
  } else if (v < 10) {
    dmDigit(144, Y, v, P, TEXT, BG, true);
  } else {
    dmDigit(124, Y, v / 10, P, TEXT, BG, true);
    dmDigit(164, Y, v % 10, P, TEXT, BG, true);
  }
  G->drawCircle(214, Y + 6, 6, TEXT);
  G->drawCircle(214, Y + 6, 5, TEXT);
  char desc[24];
  snprintf(desc, sizeof desc, "%s", weatherText(s.wx.code));
  textC(120, 124, 2, TEXT, desc);
  if (s.city && s.city[0]) {
    char c[24];
    snprintf(c, sizeof c, "%.20s", s.city);
    textC(120, 144, 1, MUTED, c);
  }
  char a[8], b[8], h[8];
  snprintf(a, sizeof a, "%d", (int)lroundf(s.wx.tmin));
  snprintf(b, sizeof b, "%d", (int)lroundf(s.wx.tmax));
  snprintf(h, sizeof h, "%d%%", s.wx.hum);
  miniCard(8, "MIN", a, true);
  miniCard(85, "MAX", b, true);
  miniCard(162, "UMIDADE", h, false);
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
static void geoDigit(float x, float y, int d, float s, float w, uint16_t c) {
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
static uint16_t mix(uint16_t a, uint16_t b, float f) {
  int ar = (a >> 11) & 31, ag = (a >> 5) & 63, ab = a & 31;
  int br = (b >> 11) & 31, bg = (b >> 5) & 63, bb = b & 31;
  return ((int)(ar + (br - ar) * f) << 11) | ((int)(ag + (bg - ag) * f) << 5) | (int)(ab + (bb - ab) * f);
}

// "23° Poucas nuvens" centralizado (grau desenhado à mão)
static void tempDesc(int cy, uint8_t sz, uint16_t ct, uint16_t cd, float t, const char *desc) {
  char n[8];
  snprintf(n, sizeof n, "%d", (int)lroundf(t));
  int degW = 2 * (sz + 1) + sz;
  int w = textW(n, sz) + degW + 6 * sz + textW(desc, sz);
  int x = 120 - w / 2;
  text(x, cy, sz, ct, n);
  degree(x + textW(n, sz) + sz, cy, sz, ct);
  text(x + textW(n, sz) + degW + 6 * sz, cy, sz, cd, desc);
}

// ---------- tema 4: NEON ----------
// Tubos de neon: halo largo e escuro + tubo na cor + núcleo claro. Horas rosa, minutos ciano.
static const uint16_t NEON_A = uiRgb(255, 64, 190), NEON_B = uiRgb(40, 220, 255);
static const int NEON_X[4] = {6, 60, 134, 188}, NEON_Y = 36;
static const float NEON_S = 8.0f;  // dígito 48 x 80

static void neonDigit(int x, int d, uint16_t c) {
  // halo largo e discreto, tubo grosso (legível de longe) e núcleo quase branco
  geoDigit(x, NEON_Y, d, NEON_S, 13, mix(BG, c, 0.18f));
  geoDigit(x, NEON_Y, d, NEON_S, 7, c);
  geoDigit(x, NEON_Y, d, NEON_S, 3, mix(c, TEXT, 0.75f));
}

static void neonDot(int y, bool on) {
  G->fillCircle(120, y, 7, on ? mix(BG, TEXT, 0.18f) : BG);
  if (on) {
    G->fillCircle(120, y, 4, TEXT);
    G->fillCircle(120, y, 2, uiRgb(255, 255, 255));
  }
}

static void themeNeon(const UiState &s, bool full) {
  char d[24];
  dateLabel(s, d, sizeof d);
  if (changed(cStatL, d)) {
    G->fillRect(0, 8, 240, 18, BG);
    textC(120, 10, 2, uiRgb(150, 150, 170), d);
  }
  int h = s.timeOk ? displayHour(s) : -1, m = s.timeOk ? s.now.tm_min : -1;
  if (h != lastDig[0]) {  // o par inteiro: o halo de um dígito encosta no vizinho
    G->fillRect(0, NEON_Y - 8, 116, 80 + 16, BG);  // halo de 13 px passa 7 px do dígito
    if (h >= 0) {
      neonDigit(NEON_X[0], h / 10, NEON_A);
      neonDigit(NEON_X[1], h % 10, NEON_A);
    }
    lastDig[0] = h;
  }
  if (m != lastDig[1]) {
    G->fillRect(126, NEON_Y - 8, 114, 80 + 16, BG);
    if (m >= 0) {
      neonDigit(NEON_X[2], m / 10, NEON_B);
      neonDigit(NEON_X[3], m % 10, NEON_B);
    }
    lastDig[1] = m;
  }
  bool colon = !s.timeOk || s.now.tm_sec % 2 == 0;
  if (colon != lastColon) {
    neonDot(NEON_Y + 26, colon);
    neonDot(NEON_Y + 56, colon);
    lastColon = colon;
  }
  if (full || lastWx != s.wx.version || lastWxOn != (int)s.weatherOn) {
    G->fillRect(0, 132, 240, 30, BG);
    G->drawFastHLine(40, 134, 160, mix(BG, NEON_B, 0.35f));
    if (s.weatherOn && s.wx.ok) tempDesc(144, 2, NEON_B, uiRgb(170, 230, 245), s.wx.t, weatherText(s.wx.code));
    lastWx = s.wx.version;
    lastWxOn = s.weatherOn;
  }
  if (full || lastToday != s.today) {
    G->fillRect(0, 172, 240, 24, BG);
    char f[24];
    snprintf(f, sizeof f, "%u %s hoje", s.today, s.today == 1 ? "foco" : "focos");
    int w = textW(f, 2) + 22;
    mascot(120 - w / 2, 172, 1, s.dailyGoal && s.today >= s.dailyGoal ? FACE_CHEER : FACE_HAPPY);
    text(120 - w / 2 + 22, 175, 2, NEON_A, f);
    lastToday = s.today;
  }
  if (changed(cStatR, s.ip ? s.ip : "")) {
    G->fillRect(0, 222, 240, 10, BG);
    textC(120, 224, 1, MUTED, cStatR.s);
  }
}

// ---------- tema 5: FLIP ----------
static const uint16_t FLIP_TOP = uiRgb(42, 42, 47), FLIP_BOT = uiRgb(31, 31, 35);

static void flipCard(int x, int v) {
  const int y = 40, w = 105, h = 130;
  G->fillRect(x - 5, y, w + 10, h, BG);  // a ponta redonda do traço pode passar da borda do cartão
  G->fillRoundRect(x, y, w, h, 12, FLIP_BOT);
  G->fillRoundRect(x, y, w, 70, 12, FLIP_TOP);
  G->fillRect(x, y + 65, w, 5, FLIP_BOT);
  if (v >= 0) {
    geoDigit(x + 12, y + 35, v / 10, 6, 10, TEXT);  // dígito 36 x 60, com respiro dentro do cartão
    geoDigit(x + 57, y + 35, v % 10, 6, 10, TEXT);
  }
  // corte da "folha" + dobradiças
  G->fillRect(x, y + 64, w, 2, BG);
  G->fillCircle(x, y + 65, 4, BG);
  G->fillCircle(x + w - 1, y + 65, 4, BG);
}

static void pill(int x, int w) { G->fillRoundRect(x, 186, w, 28, 14, CARD); }

static void themeFlip(const UiState &s, bool full) {
  char d[24];
  dateLabel(s, d, sizeof d);
  if (changed(cStatL, d)) {
    G->fillRect(0, 8, 240, 18, BG);
    textC(120, 12, 2, MUTED, d);
  }
  int h = s.timeOk ? displayHour(s) : -1, m = s.timeOk ? s.now.tm_min : -1;
  if (h != lastDig[0]) {
    flipCard(10, h);
    lastDig[0] = h;
  }
  if (m != lastDig[1]) {
    flipCard(125, m);
    lastDig[1] = m;
  }
  if (full || lastWx != s.wx.version || lastWxOn != (int)s.weatherOn) {
    G->fillRect(8, 184, 110, 32, BG);
    pill(10, 105);
    if (s.weatherOn && s.wx.ok) {
      weatherIcon(34, 199, 0.42f, s.wx.code, s.wx.day, CARD);
      tempText(80, 193, 2, TEXT, s.wx.t);
    } else textC(62, 197, 1, MUTED, "sem clima");
    lastWx = s.wx.version;
    lastWxOn = s.weatherOn;
  }
  if (full || lastToday != s.today || lastGoal != s.dailyGoal) {
    G->fillRect(122, 184, 112, 32, BG);
    pill(125, 105);
    mascot(137, 192, 1, s.today >= s.dailyGoal ? FACE_CHEER : FACE_HAPPY);
    char f[16];
    snprintf(f, sizeof f, "%u/%u", s.today, s.dailyGoal);
    text(160, 193, 2, s.today >= s.dailyGoal ? SHORT : TEXT, f);
    lastToday = s.today;
    lastGoal = s.dailyGoal;
  }
  if (changed(cStatR, s.ip ? s.ip : "")) {
    G->fillRect(0, 224, 240, 10, BG);
    textC(120, 226, 1, MUTED, cStatR.s);
  }
}

// ---------- tema 6: LCD RETRÔ ----------
// Relógio de pulso dos anos 80: tela verde-acinzentada, 7 segmentos com os apagados visíveis.
static const uint16_t LCD_BG = uiRgb(166, 180, 140), LCD_ON = uiRgb(18, 24, 16), LCD_GHOST = uiRgb(161, 175, 136);  // fantasma = só um vestígio, como no LCD real

// DSEG7 Classic Bold (keshikan, SIL Open Font License 1.1) — os 7 segmentos do "8",
// em unidades da fonte: x 0..618, y 0..1000 (de cima para baixo). Ordem a b c d e f g.
static const int16_t DSEG_PT[][2] = {
  {138, 124}, {45, 31}, {76, 0}, {542, 0}, {573, 31}, {480, 124},  // a
  {494, 138}, {587, 45}, {618, 76}, {618, 490}, {560, 490}, {556, 486}, {494, 425},  // b
  {494, 576}, {556, 515}, {560, 511}, {618, 511}, {618, 924}, {587, 955}, {494, 862},  // c
  {480, 876}, {573, 969}, {542, 1000}, {76, 1000}, {45, 969}, {138, 876},  // d
  {124, 862}, {31, 955}, {0, 924}, {0, 510}, {58, 510}, {62, 515}, {124, 576},  // e
  {124, 424}, {62, 485}, {58, 490}, {0, 490}, {0, 76}, {31, 45}, {124, 138},  // f
  {138, 438}, {478, 438}, {541, 501}, {480, 562}, {138, 562}, {76, 500},  // g
};
static const uint8_t DSEG_START[8] = {0, 6, 13, 20, 26, 33, 40, 46};
static const uint8_t DSEG_MASK[10] = {0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x27, 0x7f, 0x6f};

// Preenche um polígono convexo (um segmento) linha a linha, amostrando no centro do pixel:
// assim as folgas finas entre segmentos vizinhos ficam limpas em qualquer tamanho.
static void fillConvex(const float *px, const float *py, int n, uint16_t c) {
  float y0 = py[0], y1 = py[0];
  for (int i = 1; i < n; i++) {
    if (py[i] < y0) y0 = py[i];
    if (py[i] > y1) y1 = py[i];
  }
  for (int y = (int)ceilf(y0 - 0.5f); y <= (int)floorf(y1 - 0.5f); y++) {
    float sy = y + 0.5f, xl = 1e9f, xr = -1e9f;
    for (int i = 0; i < n; i++) {
      int j = (i + 1) % n;
      float ya = py[i], yb = py[j];
      if ((ya <= sy && sy < yb) || (yb <= sy && sy < ya)) {
        float x = px[i] + (sy - ya) * (px[j] - px[i]) / (yb - ya);
        if (x < xl) xl = x;
        if (x > xr) xr = x;
      }
    }
    int l = (int)ceilf(xl - 0.5f), r = (int)floorf(xr - 0.5f);
    if (r >= l) G->drawFastHLine(l, y, r - l + 1, c);
  }
}

// Dígito no estilo DSEG7 (relógio LCD): h = altura em px, largura = 0,618·h.
// Cada segmento é pintado aceso ou "fantasma": troca de dígito sem limpar a área.
static void dsegDigit(int x, int y, int h, int d, uint16_t on, uint16_t off) {
  const float k = h / 1000.0f;
  float px[8], py[8];
  for (int sgm = 0; sgm < 7; sgm++) {
    int n = DSEG_START[sgm + 1] - DSEG_START[sgm];
    for (int i = 0; i < n; i++) {
      px[i] = x + DSEG_PT[DSEG_START[sgm] + i][0] * k;
      py[i] = y + DSEG_PT[DSEG_START[sgm] + i][1] * k;
    }
    bool lit = d >= 0 && d <= 9 && (DSEG_MASK[d] >> sgm & 1);
    fillConvex(px, py, n, lit ? on : off);
  }
}

static void themeLcd(const UiState &s, bool full) {
  if (full) {
    textC(120, 9, 1, uiRgb(170, 170, 182), "TOMATE   ALARM   CHRONO");
    G->fillRoundRect(8, 24, 224, 186, 14, LCD_BG);
    G->drawFastHLine(22, 25, 196, uiRgb(140, 154, 116));  // sombra interna
    G->drawFastHLine(22, 26, 196, uiRgb(152, 165, 128));
  }
  // linha de cima: dia da semana + data
  char a[8] = "---", b[8] = "--.--";
  if (s.timeOk) {
    snprintf(a, sizeof a, "%s", WD[s.now.tm_wday]);
    snprintf(b, sizeof b, "%02d.%02d", s.now.tm_mday, s.now.tm_mon + 1);
  }
  char ab[24];
  snprintf(ab, sizeof ab, "%s|%s|%d", a, b, s.hour12 && s.timeOk ? (s.now.tm_hour < 12 ? 1 : 2) : 0);
  if (changed(cStatL, ab)) {
    G->fillRect(18, 34, 204, 18, LCD_BG);
    text(22, 36, 2, LCD_ON, a);
    textR(218, 36, 2, LCD_ON, b);
    if (s.hour12 && s.timeOk) text(66, 40, 1, LCD_ON, s.now.tm_hour < 12 ? "AM" : "PM");
  }
  // horas e minutos grandes, em itálico
  int v[4] = {-1, -1, -1, -1};
  if (s.timeOk) {
    int h = displayHour(s);
    v[0] = h / 10; v[1] = h % 10; v[2] = s.now.tm_min / 10; v[3] = s.now.tm_min % 10;
    if (v[0] == 0) v[0] = -1;  // sem zero à esquerda, como nos relógios de pulso
  }
  // dígitos DSEG7 de 78 px (48 de largura), centrados nos dois-pontos em x=120
  const int X[4] = {10, 65, 127, 182}, Y = 62, H = 78;
  for (int i = 0; i < 4; i++)
    if (full || lastDig[i] != v[i]) {
      dsegDigit(X[i], Y, H, v[i], LCD_ON, LCD_GHOST);
      lastDig[i] = v[i];
    }
  bool colon = !s.timeOk || s.now.tm_sec % 2 == 0;
  if (full || colon != lastColon) {
    G->fillCircle(120, Y + 24, 4, colon ? LCD_ON : LCD_GHOST);
    G->fillCircle(120, Y + 54, 4, colon ? LCD_ON : LCD_GHOST);
    lastColon = colon;
  }
  // segundos pequenos
  int sec = s.timeOk ? s.now.tm_sec : -1;
  if (full || sec != lastSecW) {
    dsegDigit(172, 158, 36, sec < 0 ? -1 : sec / 10, LCD_ON, LCD_GHOST);
    dsegDigit(198, 158, 36, sec < 0 ? -1 : sec % 10, LCD_ON, LCD_GHOST);
    lastSecW = sec;
  }
  // temperatura (7 segmentos médios) e focos com o tomate monocromático
  if (full || lastWx != s.wx.version || lastWxOn != (int)s.weatherOn || lastToday != s.today) {
    G->fillRect(16, 152, 150, 52, LCD_BG);
    if (s.weatherOn && s.wx.ok) {
      int t = (int)lroundf(s.wx.t);
      if (t >= 0 && t <= 99) {
        dsegDigit(20, 158, 36, t >= 10 ? t / 10 : -1, LCD_ON, LCD_GHOST);
        dsegDigit(46, 158, 36, t % 10, LCD_ON, LCD_GHOST);
      } else {
        char n[8];
        snprintf(n, sizeof n, "%d", t);
        text(20, 160, 3, LCD_ON, n);
      }
      degree(74, 156, 1, LCD_ON);
      text(82, 156, 2, LCD_ON, "C");
    }
    uiSprite(&SPR_TOMATE_LCD, 96, 158, 2, 0, LCD_BG);  // tomate monocromático, no estilo do LCD
    char f[12];
    snprintf(f, sizeof f, "x%u", s.today);
    text(132, 172, 2, LCD_ON, f);
    lastWx = s.wx.version;
    lastWxOn = s.weatherOn;
    lastToday = s.today;
  }
  if (changed(cStatR, s.ip ? s.ip : "")) {
    G->fillRect(0, 216, 240, 12, BG);
    textC(120, 220, 1, MUTED, cStatR.s);
  }
}

// ---------- tema 7: PALAVRAS ----------
// A hora por extenso ("SAO DUAS E MEIA DA TARDE"), arredondada a 5 minutos.
// Acentos: '\x01' = Ê, '\x02' = Ã, '\x03' = É (a fonte embutida só tem ASCII; o sinal é desenhado).
struct Tok { const char *s; uint16_t c; };
static const char *HOURW[12] = {"", "UMA", "DUAS", "TR\x01S", "QUATRO", "CINCO", "SEIS", "SETE", "OITO", "NOVE", "DEZ", "ONZE"};
static const char *const MIN_E[7][4] = {{}, {"E", "CINCO"}, {"E", "DEZ"}, {"E", "QUINZE"}, {"E", "VINTE"}, {"E", "VINTE", "E", "CINCO"}, {"E", "MEIA"}};
static const char *const MIN_PARA[5][3] = {{"VINTE", "E", "CINCO"}, {"VINTE"}, {"QUINZE"}, {"DEZ"}, {"CINCO"}};

static void wordText(int x, int y, uint8_t sz, uint16_t c, const char *str) {
  for (const char *p = str; *p; p++, x += 6 * sz) {
    char ch = *p, base = ch == '\x01' || ch == '\x03' ? 'E' : ch == '\x02' ? 'A' : ch;
    G->drawChar(x, y, base, c, c, sz);
    float u = sz;
    if (ch == '\x01') {  // circunflexo
      thickLine(x + 0.8f * u, y - 2, x + 2.5f * u, y - 2 - 1.6f * u, 2, c);
      thickLine(x + 2.5f * u, y - 2 - 1.6f * u, x + 4.2f * u, y - 2, 2, c);
    } else if (ch == '\x02') {  // til
      thickLine(x + 0.5f * u, y - 3, x + 1.6f * u, y - 2 - 1.5f * u, 2, c);
      thickLine(x + 1.6f * u, y - 2 - 1.5f * u, x + 3.2f * u, y - 3, 2, c);
      thickLine(x + 3.2f * u, y - 3, x + 4.4f * u, y - 2 - 1.5f * u, 2, c);
    } else if (ch == '\x03') {  // agudo
      thickLine(x + 2.0f * u, y - 2, x + 3.4f * u, y - 2 - 1.8f * u, 2, c);
    }
  }
}

static int buildPhrase(const UiState &s, Tok *t, uint16_t hourC) {
  int n = 0;
  int h = s.now.tm_hour, r = (s.now.tm_min + 2) / 5 * 5;
  if (r == 60) { r = 0; h = (h + 1) % 24; }
  bool para = r >= 35;
  int hh = para ? (h + 1) % 24 : h;
  const char *hw = hh == 0 ? "MEIA-NOITE" : hh == 12 ? "MEIO-DIA" : HOURW[hh % 12];
  bool singular = hh == 0 || hh == 12 || hh % 12 == 1;
  if (!para) {
    t[n++] = {singular ? "\x03" : "S\x02O", MUTED};
    t[n++] = {hw, hourC};
    if (r == 0) {
      if (hh != 0 && hh != 12) t[n++] = {"EM PONTO", MUTED};
    } else {
      for (int i = 0; i < 4 && MIN_E[r / 5][i]; i++) t[n++] = {MIN_E[r / 5][i], i % 2 ? TEXT : MUTED};
    }
  } else {
    for (int i = 0; i < 3 && MIN_PARA[(r - 35) / 5][i]; i++)
      t[n++] = {MIN_PARA[(r - 35) / 5][i], strcmp(MIN_PARA[(r - 35) / 5][i], "E") ? TEXT : MUTED};
    t[n++] = {"PARA", MUTED};
    t[n++] = {hh == 12 ? "O" : hh == 0 || hh % 12 == 1 ? "A" : "AS", MUTED};
    t[n++] = {hw, hourC};
  }
  if (hh != 0 && hh != 12) {
    t[n++] = {"DA", MUTED};
    t[n++] = {hh < 5 ? "MADRUGADA" : hh < 12 ? "MANH\x02" : hh < 18 ? "TARDE" : "NOITE", MUTED};
  }
  return n;
}

static void themeWords(const UiState &s, bool full) {
  statusBar(s, s.ip ? s.ip : "", BG);
  if (!s.timeOk) return;
  Tok t[16];
  int n = buildPhrase(s, t, WORK);
  char key[40] = "";
  for (int i = 0; i < n; i++) {
    strncat(key, t[i].s, sizeof key - strlen(key) - 2);
    strncat(key, " ", sizeof key - strlen(key) - 1);
  }
  if (changed(cA, key)) {
    G->fillRect(0, 24, 240, 184, BG);
    // quebra em linhas de até 12 caracteres (tamanho 3 = 18 px por caractere)
    int lineStart[8], lineLen[8], lines = 0, len = 0;
    for (int i = 0; i < n && lines <= 8; i++) {
      int wl = strlen(t[i].s);
      if (lines == 0 || len + 1 + wl > 12) {
        if (lines == 8) break;
        lineStart[lines] = i;
        lineLen[lines++] = 0;
        len = wl;
      } else len += 1 + wl;
      lineLen[lines - 1]++;
    }
    const int LH = 36, total = lines * LH - 12;
    int y = 30 + (174 - total) / 2;
    for (int l = 0; l < lines; l++, y += LH) {
      int chars = 0;
      for (int k = 0; k < lineLen[l]; k++) chars += strlen(t[lineStart[l] + k].s) + (k ? 1 : 0);
      int x = 120 - (chars * 18 - 3) / 2;
      for (int k = 0; k < lineLen[l]; k++) {
        const Tok &tk = t[lineStart[l] + k];
        wordText(x, y, 3, tk.c, tk.s);
        x += (strlen(tk.s) + 1) * 18;
      }
    }
  }
  char hm[8];
  snprintf(hm, sizeof hm, "%02d:%02d", displayHour(s), s.now.tm_min);
  if (changed(cC, hm)) {
    G->fillRect(0, 212, 240, 18, BG);
    textC(120, 214, 2, uiRgb(90, 90, 100), hm);
  }
}

void uiRenderClock(const UiState &s) {
  uint8_t t = s.theme < THEME_COUNT ? s.theme : THEME_MATRIX;
  bool full = enter(t);
  switch (t) {
    case THEME_TOMATO: themeTomato(s, full); break;
    case THEME_ANALOG: themeAnalog(s, full); break;
    case THEME_WEATHER: themeWeather(s, full); break;
    case THEME_NEON: themeNeon(s, full); break;
    case THEME_FLIP: themeFlip(s, full); break;
    case THEME_LCD: themeLcd(s, full); break;
    case THEME_WORDS: themeWords(s, full); break;
    default: themeMatrix(s, full); break;
  }
}

// ---------- Pomodoro: anel de progresso ----------
static const TimeLayout TL_POMO = {{44, 79, 132, 167}, 70, 6, 117};

static uint16_t phaseColor(uint8_t p) { return p == PH_WORK ? WORK : p == PH_SHORT ? SHORT : LONG; }

static void ringDot(int deg, uint16_t c) {
  float a = (deg - 90) * K_PI / 180;
  G->fillCircle(AX + cosf(a) * 110, AY + sinf(a) * 110, 6, c);
}

void uiRenderPomodoro(const UiState &s) {
  bool full = enter(10);
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

  if (changed(cA, PHASE_LABEL[s.phase])) {
    G->fillRect(50, 44, 140, 18, BG);  // dentro do anel (raio interno 104)
    textC(120, 46, 2, col, PHASE_LABEL[s.phase]);
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

  // tomate + livro (foco) ou café (pausa); quando uma fase termina, a animação de fim
  int8_t key;
  if (s.celebrate) key = 64 + uiSpriteFrame(&SPR_TOMATE_FIM, s.ms);
  else key = (!s.running ? FACE_SLEEP : s.phase == PH_WORK ? FACE_FOCUS : FACE_RELAX) * 2 + (s.phase == PH_WORK);
  if (key != lastFace) {
    if ((key >= 64) != (lastFace >= 64)) G->fillRect(84, 136, 72, 44, BG);  // troca de layout
    if (key >= 64) uiSprite(&SPR_TOMATE_FIM, 104, 138, 2, key - 64, BG);    // bg repinta o quadro anterior
    else {
      mascot(86, 144, 2, key / 2, BG);
      uiSprite(key & 1 ? &SPR_LIVRO : &SPR_CAFE, 122, 144, 2, 0, BG);
    }
    lastFace = key;
  }

  char st[24];
  if (!s.running) snprintf(st, sizeof st, "PAUSADO");
  else if (s.timeOk) {
    struct tm t = s.now, e;
    time_t end = mktime(&t) + secs;
    localtime_r(&end, &e);
    snprintf(st, sizeof st, "termina %02d:%02d", e.tm_hour, e.tm_min);
  } else st[0] = 0;
  if (changed(cB, st)) {
    G->fillRect(50, 184, 140, 10, BG);
    textC(120, 186, 1, s.running ? MUTED : TEXT, st);
  }
}

// ---------- telas avulsas ----------
void uiMessage(const char *title, const char *l1, const char *l2, const char *l3) {
  screen = 20;
  G->fillScreen(BG);
  mascot(96, 26, 3, FACE_HAPPY);
  textC(120, 90, 2, TEXT, title);
  textC(120, 118, 2, WORK, l1);
  textC(120, 150, 1, MUTED, l2);
  textC(120, 164, 1, MUTED, l3);
}

void uiColorTest() {
  screen = 21;
  G->fillScreen(BG);
  textC(120, 8, 1, TEXT, "TESTE DE CORES");
  const uint16_t c[4] = {0xF800, 0x07E0, 0x001F, 0xFFFF};
  const char *n[4] = {"VERMELHO", "VERDE", "AZUL", "BRANCO"};
  for (int i = 0; i < 4; i++) {
    G->fillRect(i * 60 + 4, 26, 52, 160, c[i]);
    textC(i * 60 + 30, 194, 1, TEXT, n[i]);
  }
  textC(120, 218, 1, MUTED, "o 1o deve ser vermelho e o");
  textC(120, 228, 1, MUTED, "fundo preto");
}

void uiError(const char *msg) {
  screen = 22;
  G->fillScreen(BG);
  textC(120, 112, 2, MUTED, msg);
}
