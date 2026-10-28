// Mostrador LCD retrô: relógio digital dos anos 80 com a fonte DSEG7.
// Registrado em faces_list.h; usa o kit de desenho de ui_kit.h.
#include "ui_kit.h"
#include "assets.h"

// ---------- tema 6: LCD RETRÔ ----------
// Relógio de pulso dos anos 80: tela verde-acinzentada, 7 segmentos com os apagados visíveis.
static const uint16_t LCD_BG = uiRgb(166, 180, 140), LCD_ON = uiRgb(18, 24, 16), LCD_GHOST = uiRgb(161, 175, 136);  // fantasma = só um vestígio, como no LCD real

// DSEG7 Classic Bold (keshikan, SIL Open Font License 1.1) — os 7 segmentos do "8",
// em unidades da fonte: x 0..618, y 0..1000 (de cima para baixo). Ordem a b c d e f g.
static const int16_t DSEG_PT[][2] PROGMEM = {
  {138, 124}, {45, 31}, {76, 0}, {542, 0}, {573, 31}, {480, 124},  // a
  {494, 138}, {587, 45}, {618, 76}, {618, 490}, {560, 490}, {556, 486}, {494, 425},  // b
  {494, 576}, {556, 515}, {560, 511}, {618, 511}, {618, 924}, {587, 955}, {494, 862},  // c
  {480, 876}, {573, 969}, {542, 1000}, {76, 1000}, {45, 969}, {138, 876},  // d
  {124, 862}, {31, 955}, {0, 924}, {0, 510}, {58, 510}, {62, 515}, {124, 576},  // e
  {124, 424}, {62, 485}, {58, 490}, {0, 490}, {0, 76}, {31, 45}, {124, 138},  // f
  {138, 438}, {478, 438}, {541, 501}, {480, 562}, {138, 562}, {76, 500},  // g
};
static const uint8_t DSEG_START[8] PROGMEM = {0, 6, 13, 20, 26, 33, 40, 46};
static const uint8_t DSEG_MASK[11] PROGMEM = {0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x27, 0x7f, 0x6f, 0x40};  // 10 = "-" (só o g)

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
void dsegDigit(int x, int y, int h, int d, uint16_t on, uint16_t off) {
  const float k = h / 1000.0f;
  float px[8], py[8];
  for (int sgm = 0; sgm < 7; sgm++) {
    int st = pgm_read_byte(&DSEG_START[sgm]), n = pgm_read_byte(&DSEG_START[sgm + 1]) - st;
    for (int i = 0; i < n; i++) {
      px[i] = x + (int16_t)pgm_read_word(&DSEG_PT[st + i][0]) * k;
      py[i] = y + (int16_t)pgm_read_word(&DSEG_PT[st + i][1]) * k;
    }
    bool lit = d >= 0 && d <= 10 && (pgm_read_byte(&DSEG_MASK[d]) >> sgm & 1);
    if (lit || off != on) fillConvex(px, py, n, lit ? on : off);  // off == on: só os acesos
  }
}

void themeLcd(const UiState &s, bool full) {
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
      text(82, 156, 2, LCD_ON, s.fahr ? "F" : "C");
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

