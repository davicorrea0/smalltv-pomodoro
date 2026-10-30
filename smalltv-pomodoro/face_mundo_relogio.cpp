// Mostrador Mundo relógio: relógio de pulso "world time" no visor do LCD retrô (DSEG7). Indicadores em caixas à
// esquerda, mapa-múndi em matriz de pontos numa janela à direita, com a faixa da noite andando com a hora UTC e o
// ponto do local (a cidade do clima) piscando; abaixo, a data com o código da cidade e o fuso, e a hora grande com o
// dia da semana e AM/PM sobre os segundos. Gerado por sim/gerar_mundo_relogio.js a partir de sim/mundo_relogio.tpl;
// registrado em faces_list.h. Tudo aqui é static (ou com nome próprio) para não colidir com face_mundo.cpp.
#include "ui_kit.h"

// mesmas cores do LCD retrô (face_lcd.cpp)
static const uint16_t LCD_BG = uiRgb(166, 180, 140), LCD_ON = uiRgb(18, 24, 16), LCD_GHOST = uiRgb(161, 175, 136);
static const uint16_t LCD_EDGE = uiRgb(140, 154, 116);
// noite no mapa: o fundo da célula escurece (terra e mar) e o ponto apagado do mar acompanha
static const uint16_t LCD_NIGHT = uiRgb(124, 137, 102), LCD_NDOT = uiRgb(116, 129, 95);
// terra: xadrez de pontos acesos sobre um meio-tom, de dia e de noite
static const uint16_t LCD_LAND = uiRgb(104, 116, 86), LCD_NLAND = uiRgb(80, 90, 66);

// ---------- mapa ----------
// 80 x 40 células de 2 x 2 px (160 x 80 px), 1 bit cada: equiretangular, longitude -180..180, latitude 78..-58
// (cada célula cobre 4,5 x 3,4 graus). Gerado por sim/gerar_mapa_relogio.js, 10 bytes por linha, bit mais alto à esquerda.
static const int MAP_W = 80, MAP_H = 40, MAP_X = 65, MAP_Y = 34;
static const uint8_t MAP[MAP_H][MAP_W / 8] PROGMEM = {
  0x00, 0x00, 0x00, 0x7f, 0xf0, 0x00, 0x00, 0x0f, 0x80, 0x00,
  0x00, 0x03, 0x8e, 0x1f, 0xf0, 0x00, 0x01, 0xff, 0xfe, 0x00,
  0x1f, 0xfd, 0x8f, 0x8f, 0xe0, 0x0f, 0x0f, 0xff, 0xff, 0xff,
  0x1f, 0xff, 0xff, 0xcf, 0x18, 0x3f, 0xff, 0xff, 0xff, 0xff,
  0x1f, 0xff, 0xf1, 0x86, 0x00, 0x77, 0xff, 0xff, 0xff, 0xff,
  0x0e, 0xff, 0xf1, 0xc0, 0x00, 0x77, 0xff, 0xff, 0xff, 0xfc,
  0x08, 0x3f, 0xf9, 0xe0, 0x03, 0x7f, 0xff, 0xff, 0xfe, 0x30,
  0x00, 0x1f, 0xff, 0xe0, 0x03, 0xff, 0xff, 0xff, 0xfe, 0x00,
  0x00, 0x0f, 0xff, 0xf0, 0x01, 0xff, 0xff, 0xff, 0xfe, 0x00,
  0x00, 0x0f, 0xff, 0xc0, 0x01, 0xff, 0xff, 0xff, 0xfe, 0x00,
  0x00, 0x0f, 0xff, 0x80, 0x03, 0xbe, 0x7f, 0xff, 0xfd, 0x00,
  0x00, 0x0f, 0xff, 0x00, 0x03, 0x0f, 0xef, 0xff, 0xf9, 0x00,
  0x00, 0x07, 0xfe, 0x00, 0x03, 0xe1, 0xff, 0xff, 0xee, 0x00,
  0x00, 0x03, 0xfe, 0x00, 0x03, 0xf7, 0xff, 0xff, 0xe0, 0x00,
  0x00, 0x03, 0xf4, 0x00, 0x07, 0xff, 0xff, 0xff, 0xe0, 0x00,
  0x00, 0x01, 0xe0, 0x00, 0x07, 0xff, 0xff, 0xff, 0xe0, 0x00,
  0x00, 0x00, 0xe2, 0x00, 0x0f, 0xff, 0xf9, 0xff, 0x80, 0x00,
  0x00, 0x00, 0x78, 0x00, 0x0f, 0xff, 0xf8, 0xe7, 0x00, 0x00,
  0x00, 0x00, 0x18, 0x00, 0x0f, 0xff, 0xe0, 0xc7, 0x00, 0x00,
  0x00, 0x00, 0x0c, 0x80, 0x0f, 0xff, 0xe0, 0x43, 0x00, 0x00,
  0x00, 0x00, 0x05, 0xe0, 0x07, 0xff, 0xe0, 0x40, 0x10, 0x00,
  0x00, 0x00, 0x03, 0xf8, 0x03, 0xff, 0xe0, 0x02, 0x60, 0x00,
  0x00, 0x00, 0x03, 0xf8, 0x00, 0x3f, 0xc0, 0x06, 0xc0, 0x00,
  0x00, 0x00, 0x03, 0xfc, 0x00, 0x3f, 0x80, 0x03, 0xe6, 0x00,
  0x00, 0x00, 0x03, 0xff, 0x00, 0x3f, 0x80, 0x01, 0x03, 0x80,
  0x00, 0x00, 0x03, 0xff, 0x00, 0x1f, 0x80, 0x00, 0x81, 0x80,
  0x00, 0x00, 0x01, 0xff, 0x00, 0x1f, 0x80, 0x00, 0x05, 0x00,
  0x00, 0x00, 0x01, 0xff, 0x00, 0x1f, 0xa0, 0x00, 0x1f, 0x80,
  0x00, 0x00, 0x00, 0xfe, 0x00, 0x1f, 0x60, 0x00, 0x3f, 0x80,
  0x00, 0x00, 0x00, 0xfe, 0x00, 0x1f, 0x60, 0x00, 0x7f, 0xc0,
  0x00, 0x00, 0x00, 0xfc, 0x00, 0x1f, 0x00, 0x00, 0x7f, 0xc0,
  0x00, 0x00, 0x00, 0xf8, 0x00, 0x0e, 0x00, 0x00, 0x7f, 0xc0,
  0x00, 0x00, 0x00, 0xf8, 0x00, 0x0e, 0x00, 0x00, 0x7f, 0xc0,
  0x00, 0x00, 0x00, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x03, 0xc0,
  0x00, 0x00, 0x01, 0xe0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
  0x00, 0x00, 0x01, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06,
  0x00, 0x00, 0x01, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04,
  0x00, 0x00, 0x01, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

// Seno em inteiros: ângulo em 1/256 de volta, quarto de onda com 65 pontos (255 = 1)
static const uint8_t SIN_Q[65] PROGMEM = {0, 6, 13, 19, 25, 31, 37, 44, 50, 56, 62, 68, 74, 80, 86, 92, 98, 103, 109, 115, 120, 126, 131, 136, 142, 147, 152, 157, 162, 167, 171, 176, 180, 185, 189, 193, 197, 201, 205, 208, 212, 215, 219, 222, 225, 228, 231, 233, 236, 238, 240, 242, 244, 246, 247, 249, 250, 251, 252, 253, 254, 254, 255, 255, 255};
static int isin(int a) {
  a &= 255;
  int i = a & 63, v = pgm_read_byte(&SIN_Q[(a & 64) ? 64 - i : i]);
  return a & 128 ? -v : v;
}
static int icos(int a) { return isin(a + 64); }

// Dia e noite pela hora UTC, sem float: o sol está a pino na longitude -(hora UTC - 12 h) · 15 °/h e na
// declinação de -23,4° · cos(2π (dia do ano + 10) / 365). Uma célula é noite quando o seno da altura do sol
// no centro dela (senφ senδ + cosφ cosδ cosH) é negativo; sem a equação do tempo (erro de poucos graus).
struct WatchSun { int sinD, cosD, utcMin; bool ok; };
static WatchSun sunNow(const UiState &s) {
  WatchSun u = {0, 255, 0, s.timeOk};
  if (!u.ok) return u;
  int d = -17 * icos((s.now.tm_yday + 10) * 256 / 365) / 255;
  u.sinD = isin(d);
  u.cosD = icos(d);
  u.utcMin = ((s.now.tm_hour * 60 + s.now.tm_min - s.utcOff / 60) % 1440 + 1440) % 1440;
  return u;
}
static bool isNight(const WatchSun &u, int c, int r) {
  if (!u.ok) return false;
  int lat = (780 - (2 * r + 1) * 17) * 64 / 900;  // centro da linha (3,4° cada), décimos de grau -> 1/256 de volta
  int h = (c * 18 + 9 - 720 + u.utcMin - 720) * 8 / 45;  // longitude + (hora UTC - 12 h), em 1/256 de volta
  return isin(lat) * u.sinD * 255 + icos(lat) * u.cosD * icos(h) < 0;
}

// Uma célula é um ponto da matriz, como num LCD de pontos: na terra, xadrez aceso sobre meio-tom; no mar, o pixel de
// cima à esquerda apagado (quase invisível). Na noite o fundo escurece, então a faixa se lê sobre a terra e o mar.
static void cell(const WatchSun &u, int c, int r) {
  const int x = MAP_X + c * 2, y = MAP_Y + r * 2;
  if (uiClip.on && (y + 2 <= uiClip.y0 || y >= uiClip.y1)) return;
  bool land = pgm_read_byte(&MAP[r][c >> 3]) >> (7 - (c & 7)) & 1, night = isNight(u, c, r);
  G->fillRect(x, y, 2, 2, land ? (night ? LCD_NLAND : LCD_LAND) : night ? LCD_NIGHT : LCD_BG);
  if (land) {
    G->drawPixel(x, y, LCD_ON);
    G->drawPixel(x + 1, y + 1, LCD_ON);
  } else G->drawPixel(x, y, night ? LCD_NDOT : LCD_GHOST);
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

struct WatchMap { const UiState *s; int c0, r0, c1, r1; bool tick; };
static void paintMap(void *p) {
  WatchMap *m = (WatchMap *)p;
  WatchSun u = sunNow(*m->s);
  for (int r = m->r0; r <= m->r1; r++)
    for (int c = m->c0; c <= m->c1; c++) cell(u, c, r);
  if (m->tick) dot(*m->s);
}

// ---------- indicadores à esquerda (como MUTE / ALM / SIG) ----------
// A caixa e o rótulo ficam sempre impressos; só o quadradinho é o segmento que acende.
static const int IND_X = 12, IND_W = 47, IND_H = 26;
static void indicator(int y, bool on, const char *label) {
  G->fillRect(IND_X + 1, y + 1, IND_W - 2, IND_H - 2, LCD_BG);
  G->drawRect(IND_X, y, IND_W, IND_H, LCD_ON);
  G->fillRect(IND_X + 3, y + 10, 5, 5, on ? LCD_ON : LCD_GHOST);
  text(IND_X + 10, y + 9, 1, LCD_ON, label);
}

// Código da cidade, como nos relógios "world time": as 3 primeiras letras do nome, sem acento e em maiúsculas
// ("Florianópolis" -> FLO, "Tokyo" -> TOK). FOLD tira o acento das letras U+00C0..U+00FF (0xC3 0x80..0xBF em UTF-8).
static const char FOLD[33] PROGMEM = "AAAAAAACEEEEIIIIDNOOOOO.OUUUUY..";
static void cityCode(const UiState &s, char *out) {
  int k = 0;
  for (const uint8_t *p = (const uint8_t *)(s.hasLoc && s.city ? s.city : ""); *p && k < 3; p++) {
    uint8_t c = *p;
    if (c == 0xC3 && p[1]) c = pgm_read_byte(&FOLD[*++p & 31]);
    if (c >= 'a' && c <= 'z') c -= 32;
    if (c >= 'A' && c <= 'Z') out[k++] = c;
  }
  while (k < 3) out[k++] = '-';
  out[3] = 0;
}

void themeWorldWatch(const UiState &s, bool full) {
  if (full) {
    textC(120, 9, 1, uiRgb(170, 170, 182), lcdLabel(3));
    G->fillRoundRect(8, 24, 224, 186, 14, LCD_BG);
    G->drawFastHLine(22, 25, 196, LCD_EDGE);  // sombra interna
    G->drawFastHLine(22, 26, 196, uiRgb(152, 165, 128));
    G->drawRect(MAP_X - 3, MAP_Y - 3, MAP_W * 2 + 6, MAP_H * 2 + 6, LCD_ON);  // janela do mapa
  }
  // mapa: a faixa de dia e noite anda a cada minuto; o ponto pisca uma vez por segundo
  const bool tick = !s.timeOk || s.now.tm_sec % 2 == 0;
  char key[40];
  snprintf(key, sizeof key, "%d %d %d %d", s.timeOk ? s.now.tm_yday * 1440 + s.now.tm_hour * 60 + s.now.tm_min : -1, (int)(s.utcOff / 60), s.hasLoc ? s.lat10 : -9999, s.lon10);
  if (changed(cA, key) || full) {
    WatchMap m = {&s, 0, 0, MAP_W - 1, MAP_H - 1, tick};
    uiPaint(full, MAP_X, MAP_Y, MAP_W * 2, MAP_H * 2, paintMap, &m);
    lastRing = tick;
  } else if (tick != lastRing) {
    int x, y;
    if (dotRect(s, &x, &y)) {  // só as células sob o marcador (8 x 8 px, alinhado ou não às células de 2 px)
      WatchMap m = {&s, (x - MAP_X) / 2, (y - MAP_Y) / 2, (x + 7 - MAP_X) / 2, (y + 7 - MAP_Y) / 2, tick};
      uiPaint(false, MAP_X + m.c0 * 2, MAP_Y + m.r0 * 2, (m.c1 - m.c0 + 1) * 2, (m.r1 - m.r0 + 1) * 2, paintMap, &m);
    }
    lastRing = tick;
  }
  // indicadores: FOCO com o Pomodoro rodando, CHUVA com o aviso de chuva ativo e WI-FI conectado
  const bool foco = s.running, chuva = s.wx.rainIn > 0, wifi = s.ip && s.ip[0] && strcmp(s.ip, "0.0.0.0");
  char ik[8];
  snprintf(ik, sizeof ik, "%d%d%d%d", foco, chuva, wifi, uiLang);
  if (changed(cC, ik)) {
    indicator(31, foco, tr("FOCO", "FOCUS", "FOCO"));
    indicator(61, chuva, tr("CHUVA", "RAIN", "LLUVIA"));
    indicator(91, wifi, "WI-FI");
  }
  // linha do meio: dia-mês em segmentos (mês-dia em inglês) e, depois do traço, o código da cidade e o fuso,
  // todos na mesma altura de letra (o "+5:30" dos fusos de meia hora entra sem o "UTC")
  char city[4];
  cityCode(s, city);
  const int off = s.utcOff / 60, aoff = off < 0 ? -off : off;
  char dk[40];
  snprintf(dk, sizeof dk, "%d %d %d %s %d", s.timeOk ? s.now.tm_mday : 0, s.timeOk ? s.now.tm_mon : 0, uiLang, city, off);
  if (changed(cStatL, dk)) {
    G->fillRect(11, 120, 218, 23, LCD_BG);
    int a = s.timeOk ? s.now.tm_mday : -1, b = s.timeOk ? s.now.tm_mon + 1 : -1;
    if (uiLang == 1) { int t = a; a = b; b = t; }
    const int H = 20, W = 12;  // dígitos de 20 px (12,4 de largura): "dd-mm" = 4 dígitos e um traço fino de 8 px
    int x = 14;
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
    G->drawFastVLine(96, 121, 21, LCD_ON);
    text(104, 124, 2, LCD_ON, city);
    char z[12];
    int n = aoff % 60 ? snprintf(z, sizeof z, "UTC%c%d:%02d", off < 0 ? '-' : '+', aoff / 60, aoff % 60) : snprintf(z, sizeof z, "UTC%c%d", off < 0 ? '-' : '+', aoff / 60);
    text(146, 124, 2, LCD_ON, n > 6 ? z + 3 : z);
  }
  // sobre os segundos: dia da semana e AM/PM, como nos relógios de pulso
  char wk[8];
  snprintf(wk, sizeof wk, "%d %d %d", s.timeOk ? s.now.tm_wday : -1, s.hour12 && s.timeOk ? (s.now.tm_hour < 12 ? 1 : 2) : 0, uiLang);
  if (changed(cB, wk)) {
    G->fillRect(184, 147, 42, 26, LCD_BG);
    textR(222, 148, 2, LCD_ON, s.timeOk ? WD[s.now.tm_wday] : "---");
    if (s.hour12 && s.timeOk) textR(222, 165, 1, LCD_ON, s.now.tm_hour < 12 ? "AM" : "PM");
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
