// Mostrador Previsão: hoje e os próximos dias, com ícone, chance de chuva e uma barra de
// mínima a máxima na mesma escala para todos os dias (dá para comparar de relance).
// Registrado em faces_list.h; usa o kit de desenho de ui_kit.h.
#include "ui_kit.h"

static const int ROW_Y = 46, ROW_H = 38;     // primeira linha e altura de cada dia
static const int BAR_X = 156, BAR_W = 44;    // barra de temperatura

// cor da temperatura: azul no frio, verde ameno, amarelo quente, vermelho no calor
static uint16_t tempColor(int t) {
  if (t <= 8) return RAIN;
  if (t <= 18) return mix(RAIN, SHORT, (t - 8) / 10.0f);
  if (t <= 26) return mix(SHORT, SUN, (t - 18) / 8.0f);
  if (t <= 34) return mix(SUN, WORK, (t - 26) / 8.0f);
  return WORK;
}

static void paintDays(void *c) {
  const UiState &s = *(const UiState *)c;
  G->fillRect(0, ROW_Y - 4, 240, 240 - ROW_Y + 4, BG);
  int lo = 99, hi = -99;
  for (int i = 0; i < s.wx.nd; i++) {
    if (s.wx.d[i].tmin < lo) lo = s.wx.d[i].tmin;
    if (s.wx.d[i].tmax > hi) hi = s.wx.d[i].tmax;
  }
  if (hi <= lo) hi = lo + 1;
  auto bx = [&](int t) { return BAR_X + (t - lo) * (BAR_W - 1) / (hi - lo); };
  for (int i = 0; i < s.wx.nd; i++) {
    const UiDay &d = s.wx.d[i];
    const int y = ROW_Y + i * ROW_H, cy = y + ROW_H / 2 - 1;
    if (i) G->drawFastHLine(10, y - 1, 220, CARD);
    const char *name = i ? WD[(s.now.tm_wday + i) % 7] : tr("HOJE", "TODAY", "HOY");
    text(10, cy - 7, 2, i ? TEXT : s.accent, name);
    weatherIcon(84, cy - 2, 0.42f, d.code, true, BG);
    if (d.rain != 255 && d.rain >= 10) {
      char r[6];
      snprintf(r, sizeof r, "%d%%", d.rain);
      text(100, cy - 3, 1, RAIN, r);
    }
    char a[6], b[6];
    snprintf(a, sizeof a, "%d", d.tmin);
    snprintf(b, sizeof b, "%d", d.tmax);
    textR(BAR_X - 6, cy - 7, 2, MUTED, a);
    text(BAR_X + BAR_W + 6, cy - 7, 2, TEXT, b);
    // trilho e o trecho do dia, colorido pela temperatura de cada ponto
    G->fillRoundRect(BAR_X, cy - 2, BAR_W, 5, 2, CARD2);
    int x0 = bx(d.tmin), x1 = bx(d.tmax);
    for (int x = x0; x <= x1; x++) G->drawFastVLine(x, cy - 2, 5, tempColor(lo + (x - BAR_X) * (hi - lo) / (BAR_W - 1)));
    if (x1 > x0) {
      G->drawPixel(x0, cy - 2, CARD2);
      G->drawPixel(x0, cy + 2, CARD2);
      G->drawPixel(x1, cy - 2, CARD2);
      G->drawPixel(x1, cy + 2, CARD2);
    }
    // hoje: um ponto na temperatura de agora
    if (!i) {
      int t = (int)lroundf(s.wx.t);
      t = t < lo ? lo : t > hi ? hi : t;
      G->fillCircle(bx(t), cy, 3, TEXT);
      G->drawCircle(bx(t), cy, 4, BG);
    }
  }
}

void themeForecast(const UiState &s, bool full) {
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
  // título: cidade (ou "Previsão") e as colunas
  char t[28];
  snprintf(t, sizeof t, "%.20s", s.city && s.city[0] ? s.city : tr("Previsao", "Forecast", "Pronostico"));
  if (changed(cB, t)) {
    G->fillRect(0, 26, 240, 12, BG);
    text(10, 28, 1, TEXT, t);
    textR(BAR_X - 6, 28, 1, MUTED, tr("MIN", "LOW", "MIN"));
    text(BAR_X + BAR_W + 6, 28, 1, MUTED, tr("MAX", "HIGH", "MAX"));
  }
  // dias: só quando o clima muda, o dia vira ou o clima liga/desliga
  char key[24];
  snprintf(key, sizeof key, "%u %d %d %d", (unsigned)s.wx.version, s.weatherOn, s.wx.ok, s.timeOk ? s.now.tm_wday : -1);
  if (!changed(cA, key) && !full) return;
  if (!(s.weatherOn && s.wx.ok && s.wx.nd)) {
    G->fillRect(0, ROW_Y - 4, 240, 240 - ROW_Y + 4, BG);
    textC(120, 112, 2, MUTED, s.weatherOn ? tr("Buscando clima", "Fetching weather", "Buscando clima") : tr("Clima desligado", "Weather off", "Clima apagado"));
    textC(120, 140, 1, MUTED, tr("defina a cidade no painel", "set the city in the panel", "elige la ciudad en el panel"));
    return;
  }
  uiPaint(full, 0, ROW_Y - 4, 240, 240 - ROW_Y + 4, paintDays, (void *)&s);
}
