// Mostrador Clima: temperatura grande e mini cartões.
// Registrado em faces_list.h; usa o kit de desenho de ui_kit.h.
#include "ui_kit.h"
#include "assets.h"

// ---------- tema 3: CLIMA ----------
static void miniCard(int x, const char *label, const char *value, bool deg) {
  const int y = 160, w = 70, h = 72, cx = x + w / 2;
  G->fillRoundRect(x, y, w, h, 12, CARD);
  textC(cx, y + 12, 1, MUTED, label);
  int vw = textW(value, 3) + (deg ? 10 : 0);
  text(cx - vw / 2, y + 34, 3, TEXT, value);
  if (deg) degree(cx - vw / 2 + textW(value, 3) + 3, y + 34, 2, TEXT);
}

// Bloco do clima (abaixo da barra de cima), desenhado fora da tela e mandado pronto: sem piscar.
static void paintWeather(void *c) {
  const UiState &s = *(const UiState *)c;
  G->fillRect(0, 24, 240, 216, BG);
  if (!(s.weatherOn && s.wx.ok)) {
    textC(120, 96, 2, MUTED, s.weatherOn ? tr("Buscando clima", "Fetching weather", "Buscando clima") : tr("Clima desligado", "Weather off", "Clima apagado"));
    textC(120, 124, 1, MUTED, tr("defina a cidade no painel", "set the city in the panel", "elige la ciudad en el panel"));
    return;
  }
  weatherIcon(64, 82, 1.6f, s.wx.code, s.wx.day, BG);
  int v = (int)lroundf(s.wx.t);
  if (v < -9) v = -9;
  if (v > 199) v = 199;
  const int P = 7, Y = 52;
  if (v >= 100) {  // °F em dia quente: três dígitos menores (5 px) no mesmo espaço, centrados na altura
    for (int i = 0; i < 3; i++) dmDigit(118 + i * 30, Y + 7, i == 0 ? v / 100 : i == 1 ? v / 10 % 10 : v % 10, 5, TEXT, BG, true);
  } else if (v < 0) {
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
  snprintf(desc, sizeof desc, "%s", s.wx.rainIn ? rainSoonText(s.wx.rainIn) : weatherText(s.wx.code));
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
  miniCard(162, tr("UMIDADE", "HUMIDITY", "HUMEDAD"), h, false);
}

void themeWeather(const UiState &s, bool full) {
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
  uiPaint(full, 0, 24, 240, 216, paintWeather, (void *)&s);
}

