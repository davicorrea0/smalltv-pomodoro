// Mostrador Previsão em cartões: cidade e hora em cima e quatro cartões (hoje e os próximos 3 dias)
// com ícone, máxima grande, mínima e chance de chuva. Números na fonte vetorial Outfit (suavizada).
// Registrado em faces_list.h; usa o kit de desenho de ui_kit.h.
#include "ui_kit.h"

static const int CARD_Y = 34, CARD_W = 54, CARD_H = 200, CARD_GAP = 4;
static const uint16_t CARD_BG = uiRgb(24, 24, 28), CARD_LINE = uiRgb(48, 48, 54);

// Temperatura centralizada em cx: sinal de menos e grau desenhados (a fonte só tem dígitos)
static void cardTemp(int cx, int y, int h, int v, uint16_t fg) {
  char d[4];
  snprintf(d, sizeof d, "%d", v < 0 ? -v : v);
  const int minus = v < 0 ? h * 3 / 8 : 0, r = h > 20 ? 3 : 2;
  const int w = minus + vecWidth(VF_OUTFIT, d, h) + 2 * r + 2;
  int x = cx - w / 2;
  if (minus) G->fillRect(x, y + h / 2 - 1, minus - 2, h > 20 ? 3 : 2, fg);
  vecText(VF_OUTFIT, x + minus, y, h, d, fg, CARD_BG);
  int dx = x + w - r - 1, dy = y + h / 8 + r;
  G->drawCircle(dx, dy, r, fg);
  if (h > 20) G->drawCircle(dx, dy, r - 1, fg);
}

// gota de chuva: bico em cima, fundo redondo
static void drop(int x, int y, uint16_t c) {
  G->fillCircle(x, y + 4, 3, c);
  G->fillTriangle(x - 3, y + 3, x + 3, y + 3, x, y - 2, c);
}

static void paintCards(void *c) {
  const UiState &s = *(const UiState *)c;
  G->fillRect(0, CARD_Y, 240, CARD_H, BG);
  for (int i = 0; i < 4 && i < s.wx.nd; i++) {
    const UiDay &d = s.wx.d[i];
    const int x = 6 + i * (CARD_W + CARD_GAP), cx = x + CARD_W / 2, y = CARD_Y;
    G->fillRoundRect(x, y, CARD_W, CARD_H, 10, CARD_BG);
    G->drawRoundRect(x, y, CARD_W, CARD_H, 10, CARD_LINE);
    textC(cx, y + 12, 1, i ? MUTED : RAIN, i ? WD[(s.now.tm_wday + i) % 7] : tr("HOJE", "TODAY", "HOY"));
    weatherIcon(cx, y + 56, 0.82f, d.code, true, CARD_BG);
    cardTemp(cx, y + 96, 24, d.tmax, TEXT);
    cardTemp(cx, y + 132, 15, d.tmin, MUTED);
    if (d.rain != 255 && d.rain >= 10) {
      char r[6];
      snprintf(r, sizeof r, "%d%%", d.rain);
      int w = 9 + textW(r, 1), rx = cx - w / 2;
      drop(rx + 3, y + 172, RAIN);
      text(rx + 9, y + 174, 1, RAIN, r);
    }
  }
}

void themeForecastCards(const UiState &s, bool full) {
  char t[24];
  snprintf(t, sizeof t, "%.23s", s.city && s.city[0] ? s.city : tr("Previsao", "Forecast", "Pronostico"));
  if (changed(cB, t)) {  // cidade grande; nome longo cai para a letra pequena em vez de cortar
    G->fillRect(0, 4, 156, 24, BG);
    if (strlen(t) <= 11) text(10, 9, 2, TEXT, t);
    else text(10, 13, 1, TEXT, t);
  }
  char hm[8] = "--:--";
  if (s.timeOk) snprintf(hm, sizeof hm, "%d:%02d", displayHour(s), s.now.tm_min);
  if (changed(cC, hm)) {
    G->fillRect(156, 4, 84, 24, BG);
    textR(230, 9, 2, MUTED, hm);
  }
  // cartões: só quando o clima muda, o dia vira ou o clima liga/desliga
  char key[24];
  snprintf(key, sizeof key, "%u %d %d %d", (unsigned)s.wx.version, s.weatherOn, s.wx.ok, s.timeOk ? s.now.tm_wday : -1);
  if (!changed(cA, key) && !full) return;
  if (!(s.weatherOn && s.wx.ok && s.wx.nd)) {
    G->fillRect(0, CARD_Y, 240, CARD_H, BG);
    textC(120, 112, 2, MUTED, s.weatherOn ? tr("Buscando clima", "Fetching weather", "Buscando clima") : tr("Clima desligado", "Weather off", "Clima apagado"));
    textC(120, 140, 1, MUTED, tr("defina a cidade no painel", "set the city in the panel", "elige la ciudad en el panel"));
    return;
  }
  uiPaint(full, 0, CARD_Y, 240, CARD_H, paintCards, (void *)&s);
}
