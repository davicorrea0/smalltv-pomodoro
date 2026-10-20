// Mostrador Matriz: dígitos em pixel art e cartões de clima e focos.
// Registrado em faces_list.h; usa o kit de desenho de ui_kit.h.
#include "ui_kit.h"
#include "assets.h"

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

void themeMatrix(const UiState &s, bool full) {
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

