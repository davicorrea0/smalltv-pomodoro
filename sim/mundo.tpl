// Mostrador Mundo: relógio de pulso com mapa-múndi em pixels, no visor do LCD retrô (DSEG7). A faixa de dia e
// noite anda com a hora UTC e o ponto do local (a cidade do clima) pisca. Registrado em faces_list.h.
#include "ui_kit.h"

// mesmas cores do LCD retrô (face_lcd.cpp)
static const uint16_t LCD_BG = uiRgb(166, 180, 140), LCD_ON = uiRgb(18, 24, 16), LCD_GHOST = uiRgb(161, 175, 136);
static const uint16_t LCD_EDGE = uiRgb(140, 154, 116);

// ---------- mapa ----------
// 80 x 40 células de 2 x 2 px (160 x 80 px), 1 bit cada: equiretangular, longitude -180..180, latitude 78..-58
// (cada célula cobre 4,5 x 3,4 graus). Gerado por sim/gerar_mapa.js, 10 bytes por linha, bit mais alto à esquerda.
static const int MAP_W = 80, MAP_H = 40, MAP_X = 64, MAP_Y = 34;
static const uint8_t MAP[MAP_H][MAP_W / 8] PROGMEM = {
@MAPA@
};

// Seno em inteiros: ângulo em 1/256 de volta, quarto de onda com 65 pontos (255 = 1)
static const uint8_t SIN_Q[65] PROGMEM = {@SEN@};
static int isin(int a) {
  a &= 255;
  int i = a & 63, v = pgm_read_byte(&SIN_Q[(a & 64) ? 64 - i : i]);
  return a & 128 ? -v : v;
}
static int icos(int a) { return isin(a + 64); }

// Dia e noite pela hora UTC, sem float: o sol está a pino na longitude -(hora UTC - 12 h) · 15 °/h e na
// declinação de -23,4° · cos(2π (dia do ano + 10) / 365). Uma célula é noite quando o seno da altura do sol
// no centro dela (senφ senδ + cosφ cosδ cosH) é negativo; sem a equação do tempo (erro de poucos graus).
struct Sun { int sinD, cosD, utcMin; bool ok; };
static Sun sunNow(const UiState &s) {
  Sun u = {0, 255, 0, s.timeOk};
  if (!u.ok) return u;
  int d = -17 * icos((s.now.tm_yday + 10) * 256 / 365) / 255;
  u.sinD = isin(d);
  u.cosD = icos(d);
  u.utcMin = ((s.now.tm_hour * 60 + s.now.tm_min - s.utcOff / 60) % 1440 + 1440) % 1440;
  return u;
}
static bool isNight(const Sun &u, int c, int r) {
  if (!u.ok) return false;
  int lat = (780 - (2 * r + 1) * 17) * 64 / 900;  // centro da linha (3,4° cada), décimos de grau -> 1/256 de volta
  int h = (c * 18 + 9 - 720 + u.utcMin - 720) * 8 / 45;  // longitude + (hora UTC - 12 h), em 1/256 de volta
  return isin(lat) * u.sinD * 255 + icos(lat) * u.cosD * icos(h) < 0;
}

// Uma célula: terra = 4 px acesos; mar de dia = vazio; mar de noite = xadrez (2 px), o sombreado dos mapas de relógio
static void cell(const Sun &u, int c, int r) {
  const int x = MAP_X + c * 2, y = MAP_Y + r * 2;
  if (uiClip.on && (y + 2 <= uiClip.y0 || y >= uiClip.y1)) return;
  bool land = pgm_read_byte(&MAP[r][c >> 3]) >> (7 - (c & 7)) & 1;
  G->fillRect(x, y, 2, 2, land ? LCD_ON : LCD_BG);
  if (!land && isNight(u, c, r)) {
    G->drawPixel(x, y, LCD_ON);
    G->drawPixel(x + 1, y + 1, LCD_ON);
  }
}

// Ponto do local: célula do mapa da cidade do clima
static bool dotCell(const UiState &s, int *c, int *r) {
  if (!s.hasLoc) return false;
  int cc = (s.lon10 + 1800) * MAP_W / 3600, rr = (780 - s.lat10) * MAP_H / 1360;  // sem constrain: não existe no simulador
  *c = cc < 0 ? 0 : cc > MAP_W - 1 ? MAP_W - 1 : cc;
  *r = rr < 0 ? 0 : rr > MAP_H - 1 ? MAP_H - 1 : rr;
  return true;
}
// Marcador de 8 x 8 px centrado na célula, sem sair do mapa: margem clara, aro de 6 x 6 e miolo de 2 x 2
// acesos. A margem clara separa o aro da terra, que também é acesa.
static bool dotRect(const UiState &s, int *x, int *y) {
  int c, r;
  if (!dotCell(s, &c, &r)) return false;
  int dx = MAP_X + c * 2 - 3, dy = MAP_Y + r * 2 - 3;
  *x = dx < MAP_X ? MAP_X : dx > MAP_X + MAP_W * 2 - 8 ? MAP_X + MAP_W * 2 - 8 : dx;
  *y = dy < MAP_Y ? MAP_Y : dy > MAP_Y + MAP_H * 2 - 8 ? MAP_Y + MAP_H * 2 - 8 : dy;
  return true;
}
static void dot(const UiState &s) {
  int x, y;
  if (!dotRect(s, &x, &y)) return;
  G->fillRect(x, y, 8, 8, LCD_BG);
  G->drawRect(x + 1, y + 1, 6, 6, LCD_ON);
  G->fillRect(x + 3, y + 3, 2, 2, LCD_ON);
}

struct MapCtx { const UiState *s; int c0, r0, c1, r1; bool tick; };
static void paintMap(void *p) {
  MapCtx *m = (MapCtx *)p;
  Sun u = sunNow(*m->s);
  for (int r = m->r0; r <= m->r1; r++)
    for (int c = m->c0; c <= m->c1; c++) cell(u, c, r);
  if (m->tick) dot(*m->s);
}

// ---------- indicadores à esquerda (como MUTE / ALM / SIG): só acesos quando valem ----------
static void indicator(int y, bool on, const char *label) {
  const uint16_t c = on ? LCD_ON : LCD_GHOST;
  G->fillRect(16, y, 8, 8, LCD_BG);
  G->drawRect(16, y, 8, 8, c);
  if (on) G->fillRect(18, y + 2, 4, 4, c);
  G->fillRect(28, y, 34, 8, LCD_BG);
  text(28, y, 1, c, label);
}

void themeWorld(const UiState &s, bool full) {
  if (full) {
    textC(120, 9, 1, uiRgb(170, 170, 182), lcdLabel(2));
    G->fillRoundRect(8, 24, 224, 186, 14, LCD_BG);
    G->drawFastHLine(22, 25, 196, LCD_EDGE);  // sombra interna
    G->drawFastHLine(22, 26, 196, uiRgb(152, 165, 128));
    G->drawRect(MAP_X - 2, MAP_Y - 2, MAP_W * 2 + 4, MAP_H * 2 + 4, LCD_EDGE);
  }
  // mapa: a faixa de dia e noite anda a cada minuto; o ponto pisca uma vez por segundo
  const bool tick = !s.timeOk || s.now.tm_sec % 2 == 0;
  char key[40];
  snprintf(key, sizeof key, "%d %d %d %d", s.timeOk ? s.now.tm_yday * 1440 + s.now.tm_hour * 60 + s.now.tm_min : -1, (int)(s.utcOff / 60), s.hasLoc ? s.lat10 : -9999, s.lon10);
  if (changed(cA, key) || full) {
    MapCtx m = {&s, 0, 0, MAP_W - 1, MAP_H - 1, tick};
    uiPaint(full, MAP_X, MAP_Y, MAP_W * 2, MAP_H * 2, paintMap, &m);
    lastRing = tick;
  } else if (tick != lastRing) {
    int x, y;
    if (dotRect(s, &x, &y)) {  // só as células sob o marcador (8 x 8 px, alinhado ou não às células de 2 px)
      MapCtx m = {&s, (x - MAP_X) / 2, (y - MAP_Y) / 2, (x + 7 - MAP_X) / 2, (y + 7 - MAP_Y) / 2, tick};
      uiPaint(false, MAP_X + m.c0 * 2, MAP_Y + m.r0 * 2, (m.c1 - m.c0 + 1) * 2, (m.r1 - m.r0 + 1) * 2, paintMap, &m);
    }
    lastRing = tick;
  }
  // indicadores: FOCO com o Pomodoro rodando, CHUVA com o aviso de chuva ativo e WI-FI conectado
  const bool foco = s.running, chuva = s.wx.rainIn > 0, wifi = s.ip && s.ip[0] && strcmp(s.ip, "0.0.0.0");
  char ik[8];
  snprintf(ik, sizeof ik, "%d%d%d", foco, chuva, wifi);
  if (changed(cC, ik)) {
    indicator(44, foco, tr("FOCO", "FOCUS", "FOCO"));
    indicator(66, chuva, tr("CHUVA", "RAIN", "LLUVIA"));
    indicator(88, wifi, "WI-FI");
  }
  // linha de cima: AM/PM, dia da semana e dia-mês em segmentos (mês-dia em inglês)
  char dk[32];
  snprintf(dk, sizeof dk, "%d %d %d %d %d", s.timeOk ? s.now.tm_wday : -1, s.timeOk ? s.now.tm_mday : 0, s.timeOk ? s.now.tm_mon : 0, s.hour12 && s.timeOk ? (s.now.tm_hour < 12 ? 1 : 2) : 0, uiLang);
  if (changed(cStatL, dk)) {
    G->fillRect(16, 120, 208, 22, LCD_BG);
    if (s.hour12 && s.timeOk) text(18, 128, 1, LCD_ON, s.now.tm_hour < 12 ? "AM" : "PM");
    text(46, 124, 2, LCD_ON, s.timeOk ? WD[s.now.tm_wday] : "---");
    int a = s.timeOk ? s.now.tm_mday : -1, b = s.timeOk ? s.now.tm_mon + 1 : -1;
    if (uiLang == 1) { int t = a; a = b; b = t; }
    const int H = 20, W = 12;  // dígitos de 20 px (12,4 de largura): "dd-mm" = 4 dígitos e um traço fino de 8 px
    int x = 222 - (4 * (W + 4) + 12);
    for (int k = 0; k < 2; k++) {
      int v = k ? b : a;
      dsegDigit(x, 121, H, v >= 10 ? v / 10 : -1, LCD_ON, LCD_GHOST);
      dsegDigit(x + W + 4, 121, H, v < 0 ? 10 : v % 10, LCD_ON, LCD_GHOST);
      x += 2 * (W + 4);
      if (!k) {
        G->fillRect(x, 130, 8, 3, LCD_ON);  // traço, como o "6-30" do relógio de pulso
        x += 12;
      }
    }
  }
  // horas e minutos grandes, com os segmentos apagados à mostra
  int v[4] = {-1, -1, -1, -1};
  if (s.timeOk) {
    int h = displayHour(s);
    v[0] = h / 10; v[1] = h % 10; v[2] = s.now.tm_min / 10; v[3] = s.now.tm_min % 10;
    if (v[0] == 0) v[0] = -1;
  }
  const int X[4] = {14, 53, 104, 143}, Y = 146, H = 56;
  for (int i = 0; i < 4; i++)
    if (full || lastDig[i] != v[i]) {
      dsegDigit(X[i], Y, H, v[i], LCD_ON, LCD_GHOST);
      lastDig[i] = v[i];
    }
  if (full || (int)tick != lastColon) {
    G->fillCircle(96, Y + 18, 3, tick ? LCD_ON : LCD_GHOST);
    G->fillCircle(96, Y + 38, 3, tick ? LCD_ON : LCD_GHOST);
    lastColon = tick;
  }
  int sec = s.timeOk ? s.now.tm_sec : -1;
  if (full || sec != lastSecW) {
    dsegDigit(186, Y + H - 26, 26, sec < 0 ? -1 : sec / 10, LCD_ON, LCD_GHOST);
    dsegDigit(204, Y + H - 26, 26, sec < 0 ? -1 : sec % 10, LCD_ON, LCD_GHOST);
    lastSecW = sec;
  }
  if (changed(cStatR, s.ip ? s.ip : "")) {
    G->fillRect(0, 216, 240, 12, BG);
    textC(120, 220, 1, MUTED, cStatR.s);
  }
}
