// Mostrador Tomate: cena em pixel art que muda com a hora e o clima.
// Registrado em faces_list.h; usa o kit de desenho de ui_kit.h.
#include "ui_kit.h"
#include "assets.h"

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

void themeTomato(const UiState &s, bool full) {
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

