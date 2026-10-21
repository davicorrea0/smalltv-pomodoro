// Mostrador Neon: tubos de neon com a fonte Geo.
// Registrado em faces_list.h; usa o kit de desenho de ui_kit.h.
#include "ui_kit.h"
#include "assets.h"

// ---------- tema 4: NEON ----------
// Tubos de neon: halo largo e escuro + tubo na cor + núcleo claro. Horas rosa, minutos ciano.
static const uint16_t NEON_A = uiRgb(255, 64, 190), NEON_B = uiRgb(40, 220, 255);
static const int NEON_X[4] = {6, 60, 134, 188}, NEON_Y = 46;
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

// Cada par de dígitos (com o halo) é redesenhado fora da tela e vai pronto: o neon não pisca.
static void neonPair(int x0, int w, int v, int i, uint16_t c) {
  G->fillRect(x0, NEON_Y - 8, w, 80 + 16, BG);  // halo de 13 px passa 7 px do dígito
  if (v >= 0) {
    neonDigit(NEON_X[i], v / 10, c);
    neonDigit(NEON_X[i + 1], v % 10, c);
  }
}
static void paintHours(void *v) { neonPair(0, 116, *(int *)v, 0, NEON_A); }
static void paintMinutes(void *v) { neonPair(126, 114, *(int *)v, 2, NEON_B); }
static void paintDate(void *d) {
  G->fillRect(0, 12, 240, 18, BG);
  textC(120, 14, 2, uiRgb(150, 150, 170), (const char *)d);
}

void themeNeon(const UiState &s, bool full) {
  char d[24];
  dateLabel(s, d, sizeof d);
  if (changed(cStatL, d)) uiPaint(full, 0, 12, 240, 18, paintDate, d);
  int h = s.timeOk ? displayHour(s) : -1, m = s.timeOk ? s.now.tm_min : -1;
  if (h != lastDig[0]) {  // o par inteiro: o halo de um dígito encosta no vizinho
    uiPaint(full, 0, NEON_Y - 8, 116, 96, paintHours, &h);
    lastDig[0] = h;
  }
  if (m != lastDig[1]) {
    uiPaint(full, 126, NEON_Y - 8, 114, 96, paintMinutes, &m);
    lastDig[1] = m;
  }
  bool colon = !s.timeOk || s.now.tm_sec % 2 == 0;
  if (colon != lastColon) {
    neonDot(NEON_Y + 26, colon);
    neonDot(NEON_Y + 56, colon);
    lastColon = colon;
  }
  if (full || lastWx != s.wx.version || lastWxOn != (int)s.weatherOn) {
    G->fillRect(0, 142, 240, 32, BG);
    G->drawFastHLine(40, 146, 160, mix(BG, NEON_B, 0.35f));
    if (s.weatherOn && s.wx.ok) tempDesc(158, 2, NEON_B, uiRgb(170, 230, 245), s.wx.t, weatherText(s.wx.code));
    lastWx = s.wx.version;
    lastWxOn = s.weatherOn;
  }
  if (full || lastToday != s.today) {
    G->fillRect(0, 186, 240, 24, BG);
    char f[24];
    snprintf(f, sizeof f, tr("%u %s hoje", "%u %s today", "%u %s hoy"), s.today, s.today == 1 ? tr("foco", "session", "foco") : tr("focos", "sessions", "focos"));
    int w = textW(f, 2) + 22;
    mascot(120 - w / 2, 188, 1, s.dailyGoal && s.today >= s.dailyGoal ? FACE_CHEER : FACE_HAPPY);
    text(120 - w / 2 + 22, 191, 2, NEON_A, f);
    lastToday = s.today;
  }
  if (changed(cStatR, s.ip ? s.ip : "")) {
    G->fillRect(0, 222, 240, 10, BG);
    textC(120, 224, 1, MUTED, cStatR.s);
  }
}

