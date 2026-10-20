// Mostrador Analógico: ponteiros bastão e marcas grossas.
// Registrado em faces_list.h; usa o kit de desenho de ui_kit.h.
#include "ui_kit.h"
#include "assets.h"

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

void themeAnalog(const UiState &s, bool full) {
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

