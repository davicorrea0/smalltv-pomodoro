// Mostrador Flip: plaquinhas com a fonte Bebas Neue e virada animada.
// Registrado em faces_list.h; usa o kit de desenho de ui_kit.h.
#include "ui_kit.h"
#include "assets.h"

// ---------- tema 5: FLIP ----------
// Relógio de plaquinhas com a fonte Bebas Neue e animação de virada de verdade:
// a aba de cima (dígito antigo) cai em direção à dobradiça encolhendo em perspectiva e
// escurecendo, revelando o dígito novo; depois a aba de baixo (dígito novo) desce cobrindo
// o antigo. O cartão é desenhado linha a linha com escala vertical, então cada quadro da
// animação é só "qual linha da fonte cai em qual linha da tela".
//
// O ESP8266 não tem unidade de ponto flutuante: o preenchimento é todo em inteiros
// (escala 1/8 px por unidade da fonte, cobertura de borda em 1/16 px, 4 sub-linhas por linha).

// Bebas Neue (Dharma Type, SIL Open Font License 1.1): contornos dos dígitos 0-9 com as curvas
// convertidas em segmentos. Unidades da fonte: célula de 400 x 720, y a partir do topo.
static const int16_t FLIP_PT[580][2] PROGMEM = {
  {200, 720}, {162, 717}, {129, 709}, {100, 694}, {76, 674}, {57, 649}, {44, 619}, {36, 584},
  {33, 544}, {33, 544}, {33, 176}, {36, 136}, {44, 102}, {57, 71}, {76, 46}, {100, 26},
  {129, 12}, {162, 3}, {200, 0}, {200, 0}, {238, 3}, {272, 12}, {300, 26}, {324, 46},
  {343, 71}, {356, 102}, {364, 136}, {367, 176}, {367, 176}, {367, 544}, {364, 584}, {356, 619},
  {343, 649}, {324, 674}, {300, 694}, {272, 709}, {238, 717}, {200, 720}, {200, 720}, {200, 620},
  {232, 612}, {251, 589}, {257, 551}, {257, 551}, {257, 169}, {251, 131}, {232, 108}, {200, 100},
  {200, 100}, {168, 108}, {149, 131}, {143, 169}, {143, 169}, {143, 551}, {149, 589}, {168, 612},
  {200, 620}, {200, 620}, {195, 710}, {195, 176}, {80, 176}, {80, 98}, {122, 96}, {156, 90},
  {181, 79}, {201, 64}, {217, 41}, {231, 10}, {231, 10}, {305, 10}, {305, 710}, {195, 710},
  {39, 710}, {39, 634}, {42, 586}, {50, 544}, {63, 506}, {77, 478}, {97, 449}, {120, 419},
  {149, 387}, {149, 387}, {177, 356}, {201, 327}, {219, 300}, {234, 276}, {247, 245}, {254, 213},
  {257, 180}, {257, 180}, {253, 142}, {243, 118}, {225, 104}, {200, 100}, {200, 100}, {168, 108},
  {149, 131}, {143, 169}, {143, 169}, {143, 244}, {39, 244}, {39, 176}, {42, 136}, {50, 101},
  {63, 71}, {81, 46}, {104, 26}, {133, 11}, {165, 3}, {203, 0}, {203, 0}, {241, 3},
  {274, 11}, {302, 26}, {325, 46}, {343, 71}, {357, 101}, {364, 136}, {367, 176}, {367, 176},
  {364, 218}, {353, 260}, {337, 301}, {318, 333}, {295, 367}, {266, 403}, {231, 441}, {231, 441},
  {202, 473}, {180, 502}, {165, 526}, {152, 559}, {148, 595}, {148, 595}, {148, 610}, {357, 610},
  {357, 710}, {39, 710}, {197, 720}, {159, 717}, {126, 709}, {98, 695}, {75, 675}, {56, 650},
  {43, 620}, {36, 586}, {33, 546}, {33, 546}, {33, 489}, {137, 489}, {137, 551}, {143, 589},
  {162, 612}, {194, 620}, {194, 620}, {219, 616}, {237, 603}, {247, 578}, {251, 541}, {251, 541},
  {251, 484}, {247, 443}, {234, 416}, {212, 400}, {179, 395}, {179, 395}, {142, 395}, {142, 295},
  {182, 295}, {211, 291}, {232, 278}, {244, 255}, {248, 221}, {248, 221}, {248, 170}, {244, 139},
  {233, 117}, {216, 104}, {193, 100}, {193, 100}, {164, 107}, {146, 129}, {140, 165}, {140, 165},
  {140, 211}, {36, 211}, {36, 174}, {39, 134}, {46, 100}, {59, 70}, {78, 45}, {109, 20},
  {149, 5}, {197, 0}, {197, 0}, {245, 5}, {285, 20}, {317, 45}, {335, 69}, {348, 99},
  {355, 133}, {358, 173}, {358, 173}, {358, 198}, {353, 249}, {339, 290}, {315, 319}, {280, 339},
  {280, 339}, {280, 341}, {317, 362}, {342, 393}, {356, 434}, {361, 485}, {361, 485}, {361, 547},
  {358, 587}, {351, 621}, {338, 651}, {320, 676}, {296, 695}, {268, 709}, {235, 717}, {197, 720},
  {197, 720}, {225, 710}, {225, 583}, {15, 583}, {15, 483}, {215, 10}, {335, 10}, {335, 483},
  {387, 483}, {387, 583}, {335, 583}, {335, 710}, {225, 710}, {115, 483}, {225, 483}, {225, 210},
  {223, 210}, {115, 483}, {202, 720}, {164, 717}, {132, 709}, {103, 694}, {80, 675}, {62, 649},
  {49, 619}, {41, 584}, {38, 544}, {38, 544}, {38, 489}, {142, 489}, {142, 552}, {148, 590},
  {167, 612}, {199, 620}, {199, 620}, {231, 612}, {250, 590}, {256, 552}, {256, 552}, {256, 395},
  {250, 357}, {231, 334}, {199, 326}, {199, 326}, {167, 334}, {148, 357}, {142, 395}, {142, 395},
  {142, 401}, {38, 401}, {58, 10}, {348, 10}, {348, 110}, {156, 110}, {147, 277}, {149, 277},
  {173, 249}, {205, 232}, {244, 226}, {244, 226}, {281, 231}, {311, 244}, {335, 267}, {352, 298},
  {363, 338}, {366, 386}, {366, 386}, {366, 544}, {363, 584}, {356, 619}, {342, 649}, {324, 675},
  {301, 694}, {273, 709}, {240, 717}, {202, 720}, {202, 720}, {203, 720}, {165, 717}, {132, 709},
  {103, 694}, {79, 674}, {60, 649}, {47, 619}, {39, 584}, {36, 544}, {36, 544}, {36, 182},
  {39, 139}, {47, 102}, {60, 71}, {78, 46}, {102, 26}, {131, 11}, {165, 3}, {204, 0},
  {204, 0}, {242, 3}, {275, 11}, {303, 26}, {326, 46}, {344, 71}, {358, 101}, {365, 136},
  {368, 176}, {368, 176}, {368, 189}, {264, 189}, {264, 169}, {258, 131}, {239, 108}, {207, 100},
  {207, 100}, {180, 105}, {161, 119}, {150, 145}, {146, 186}, {146, 186}, {146, 320}, {148, 320},
  {172, 286}, {205, 266}, {248, 259}, {248, 259}, {285, 264}, {315, 277}, {339, 300}, {356, 331},
  {367, 371}, {370, 419}, {370, 419}, {370, 544}, {367, 584}, {359, 619}, {346, 649}, {327, 674},
  {303, 694}, {275, 709}, {241, 717}, {203, 720}, {203, 720}, {203, 620}, {235, 612}, {254, 590},
  {260, 552}, {260, 552}, {260, 428}, {254, 390}, {235, 367}, {203, 359}, {203, 359}, {171, 367},
  {152, 390}, {146, 428}, {146, 428}, {146, 552}, {152, 590}, {171, 612}, {203, 620}, {203, 620},
  {80, 710}, {256, 110}, {34, 110}, {34, 10}, {364, 10}, {364, 106}, {192, 710}, {80, 710},
  {200, 720}, {161, 717}, {127, 709}, {97, 695}, {73, 676}, {53, 651}, {39, 621}, {31, 587},
  {28, 547}, {28, 547}, {28, 485}, {32, 437}, {45, 398}, {67, 366}, {100, 342}, {100, 342},
  {100, 340}, {70, 317}, {49, 288}, {36, 249}, {32, 202}, {32, 202}, {32, 173}, {35, 133},
  {43, 99}, {56, 69}, {76, 45}, {100, 25}, {128, 11}, {162, 3}, {200, 0}, {200, 0},
  {238, 3}, {272, 11}, {300, 25}, {325, 45}, {344, 69}, {357, 99}, {365, 133}, {368, 173},
  {368, 173}, {368, 202}, {364, 248}, {351, 287}, {330, 317}, {300, 340}, {300, 340}, {300, 342},
  {333, 366}, {355, 398}, {368, 437}, {372, 485}, {372, 485}, {372, 547}, {369, 587}, {361, 621},
  {347, 651}, {328, 676}, {303, 695}, {273, 709}, {239, 717}, {200, 720}, {200, 720}, {200, 295},
  {232, 287}, {252, 264}, {258, 225}, {258, 225}, {258, 170}, {252, 131}, {232, 108}, {200, 100},
  {200, 100}, {168, 108}, {148, 131}, {142, 170}, {142, 170}, {142, 225}, {148, 264}, {168, 287},
  {200, 295}, {200, 295}, {200, 620}, {227, 615}, {247, 600}, {258, 576}, {262, 541}, {262, 541},
  {262, 474}, {258, 439}, {247, 415}, {227, 400}, {200, 395}, {200, 395}, {173, 400}, {154, 415},
  {142, 439}, {138, 474}, {138, 474}, {138, 541}, {142, 576}, {154, 600}, {173, 615}, {200, 620},
  {200, 620}, {196, 720}, {158, 717}, {126, 709}, {97, 694}, {74, 675}, {56, 649}, {43, 619},
  {35, 584}, {32, 544}, {32, 544}, {32, 531}, {136, 531}, {136, 551}, {142, 589}, {161, 612},
  {193, 620}, {193, 620}, {220, 615}, {239, 601}, {250, 575}, {254, 534}, {254, 534}, {254, 400},
  {252, 400}, {228, 434}, {195, 454}, {152, 461}, {152, 461}, {115, 456}, {85, 443}, {61, 420},
  {44, 389}, {33, 349}, {30, 301}, {30, 301}, {30, 176}, {33, 136}, {41, 102}, {54, 71},
  {73, 46}, {97, 26}, {126, 12}, {159, 3}, {197, 0}, {197, 0}, {235, 3}, {269, 12},
  {297, 26}, {321, 46}, {340, 71}, {353, 102}, {361, 136}, {364, 176}, {364, 176}, {364, 538},
  {361, 581}, {354, 618}, {340, 649}, {322, 675}, {298, 694}, {270, 709}, {235, 717}, {196, 720},
  {196, 720}, {197, 361}, {229, 353}, {248, 330}, {254, 292}, {254, 292}, {254, 168}, {248, 130},
  {229, 108}, {197, 100}, {197, 100}, {165, 108}, {146, 130}, {140, 168}, {140, 168}, {140, 292},
  {146, 330}, {165, 353}, {197, 361}, {197, 361},
};
static const uint16_t FLIP_CEND[16] PROGMEM = {39, 58, 72, 138, 225, 237, 242, 301, 365, 384, 392, 455, 474, 497, 561, 580};  // fim (exclusivo) de cada contorno
static const uint8_t FLIP_DIG[11] = {0, 2, 3, 4, 5, 7, 8, 10, 11, 14, 16};  // primeiro contorno de cada dígito
static const int FLIP_CW = 400, FLIP_CH = 720;



static const int FL_W = 108, FL_H = 124, FL_HALF = 62, FL_Y = 40;
static const int FL_EXT = 4;             // folga lateral: a borda da aba "cresce" em perspectiva
static const int FL_BW = FL_W + 2 * FL_EXT;
static const int FL_DTOP = 17;           // topo do dígito dentro do cartão (dígito = 720/8 = 90 px)
static const int FL_X[2] = {8, 124};     // cartão das horas e dos minutos
static const float FL_D = 14.0f * FL_HALF;  // distância do observador (perspectiva sutil)
static const uint16_t FL_TOP = uiRgb(46, 46, 52), FL_BOT = uiRgb(37, 37, 42), FL_FG = uiRgb(244, 242, 234);
static const uint8_t FL_CORNER[10] = {7, 5, 3, 2, 2, 1, 1, 0, 0, 0};  // recuo dos cantos arredondados (raio 10)
static uint8_t flCov[FL_BW];
static uint16_t flRowBuf[FL_BW];

// Mistura inteira de cores RGB565: n/16 de b sobre a.
static uint16_t mix16(uint16_t a, uint16_t b, int n) {
  if (n <= 0) return a;
  if (n >= 16) return b;
  int ar = (a >> 11) & 31, ag = (a >> 5) & 63, ab = a & 31;
  int br = (b >> 11) & 31, bg = (b >> 5) & 63, bb = b & 31;
  return ((ar + (br - ar) * n / 16) << 11) | ((ag + (bg - ag) * n / 16) << 5) | (ab + (bb - ab) * n / 16);
}

// Acumula a cobertura de um trecho [a, b) em 1/16 de pixel.
static void flSpan(int a, int b) {
  if (a < 0) a = 0;
  if (b > FL_BW * 16) b = FL_BW * 16;
  if (b <= a) return;
  int ia = a >> 4, ib = b >> 4;
  if (ia == ib) {
    flCov[ia] += b - a;
    return;
  }
  flCov[ia] += 16 - (a & 15);
  for (int i = ia + 1; i < ib; i++) flCov[i] += 16;
  if (ib < FL_BW) flCov[ib] += b & 15;
}

// Uma sub-linha de um dígito: cruzamentos com as arestas (regra par-ímpar, que trata os furos).
// ox16 = origem do dígito (1/16 px, coordenadas do buffer); sx16 = escala horizontal x16 em torno do centro.
static void flSubline(int d, int ox16, int sy, int sx16) {
  if (sy < 0 || sy >= FLIP_CH) return;
  int xs[24], n = 0;
  for (int c = FLIP_DIG[d]; c < FLIP_DIG[d + 1]; c++) {
    // tabelas em PROGMEM (flash): no ESP8266, "const" comum ocuparia RAM
    int s = c ? pgm_read_word(&FLIP_CEND[c - 1]) : 0, e = pgm_read_word(&FLIP_CEND[c]);
    for (int i = s; i < e; i++) {
      int j = i + 1 < e ? i + 1 : s;
      int ay = (int16_t)pgm_read_word(&FLIP_PT[i][1]), by = (int16_t)pgm_read_word(&FLIP_PT[j][1]);
      if ((ay <= sy && sy < by) || (by <= sy && sy < ay)) {
        int ax = (int16_t)pgm_read_word(&FLIP_PT[i][0]), bx = (int16_t)pgm_read_word(&FLIP_PT[j][0]);
        if (n < 24) xs[n++] = ax * 16 + (sy - ay) * (bx - ax) * 16 / (by - ay);
      }
    }
  }
  for (int i = 1; i < n; i++)  // ordenação por inserção (poucos cruzamentos)
    for (int k = i; k > 0 && xs[k] < xs[k - 1]; k--) {
      int t = xs[k];
      xs[k] = xs[k - 1];
      xs[k - 1] = t;
    }
  const int c16 = FL_BW * 8;  // centro do buffer em 1/16 px
  for (int k = 0; k + 1 < n; k += 2) {
    int a = ox16 + xs[k] / 8, b = ox16 + xs[k + 1] / 8;  // fonte/8 = px
    flSpan(c16 + (a - c16) * sx16 / 16, c16 + (b - c16) * sx16 / 16);
  }
}

// Desenha a linha de tela FL_Y+yr do cartão em cx mostrando a linha de origem srcTop (px do
// cartão, fracionária, altura dy) do par "value", com escala horizontal sx e escurecida em shade/16.
static void flRow(int cx, int yr, int value, float srcTop, float dy, int shade, float sx = 1) {
  memset(flCov, 0, sizeof flCov);
  int sx16 = (int)(sx * 16 + 0.5f);
  if (value >= 0)
    for (int j = 0; j < 4; j++) {
      int sy = (int)((srcTop + (j + 0.5f) * dy / 4 - FL_DTOP) * 8);  // linha de origem em unidades da fonte
      flSubline(value / 10, (FL_EXT + 4) * 16, sy, sx16);
      flSubline(value % 10, (FL_EXT + 54) * 16, sy, sx16);
    }
  uint16_t bg = srcTop < FL_HALF ? FL_TOP : FL_BOT;
  uint16_t pal[17], b = mix16(bg, BG, shade), f = mix16(FL_FG, BG, shade);
  for (int i = 0; i <= 16; i++) pal[i] = mix16(b, f, i);
  // silhueta: cartão (com cantos arredondados da linha de origem) escalado em torno do centro
  int sr = (int)srcTop;
  int inset = sr < 10 ? FL_CORNER[sr < 0 ? 0 : sr] : sr >= FL_H - 10 ? FL_CORNER[FL_H - 1 - sr] : 0;
  float half = (FL_W / 2 - inset) * sx;
  int l = (int)(FL_BW / 2 - half + 0.5f), r = (int)(FL_BW / 2 + half + 0.5f);
  for (int x = 0; x < FL_BW; x++) {
    int c = flCov[x] > 64 ? 64 : flCov[x];
    flRowBuf[x] = (x < l || x >= r) ? BG : pal[(c + 2) / 4];
  }
  blit(cx - FL_EXT, FL_Y + yr, flRowBuf, FL_BW, 1);
}

// Metade de cima do cartão para o ângulo th da aba (0 = em pé, cobrindo; 90 = de lado).
// A aba mostra a metade de cima do número antigo; atrás dela aparece o número novo.
static void flipTopHalf(int cx, int oldV, int newV, float th) {
  float a = th * K_PI / 180, ca = cosf(a), sa = sinf(a);
  // altura aparente da aba projetada (r = FL_HALF)
  float dyMax = th < 90 ? FL_HALF * ca * FL_D / (FL_D - FL_HALF * sa) : 0;
  int shadow = (int)((90 - th) / 90 * 5);  // a aba ainda em pé faz sombra no número novo
  for (int yr = 0; yr < FL_HALF - 1; yr++) {
    float dy = FL_HALF - yr - 0.5f;  // distância até a dobradiça
    if (th < 90 && dy <= dyMax) {
      // projeção inversa: ponto da aba a r da dobradiça aparece a dy = r·cos·D/(D - r·sen)
      float r = dy * FL_D / (FL_D * ca + dy * sa);
      float sx = FL_D / (FL_D - r * sa);
      float q = FL_D * ca + dy * sa, step = FL_D * FL_D * ca / (q * q);  // linhas de origem por linha de tela
      flRow(cx, yr, oldV, FL_HALF - r - step / 2, step, (int)(th / 90 * 9), sx);
    } else {
      flRow(cx, yr, newV, yr, 1, th < 90 ? shadow : 0);
    }
  }
}

// Metade de baixo para o ângulo th (90 = de lado, 180 = deitada cobrindo).
// A aba mostra a metade de baixo do número novo; embaixo dela ainda aparece o antigo.
static void flipBottomHalf(int cx, int oldV, int newV, float th) {
  float phi = 180 - th;  // 0 = deitada
  float a = phi * K_PI / 180, ca = cosf(a), sa = sinf(a);
  float dyMax = FL_HALF * ca * FL_D / (FL_D - FL_HALF * sa);
  int shadow = (int)((90 - phi) / 90 * 4);  // a aba chegando faz sombra no número antigo
  for (int yr = FL_HALF + 1; yr < FL_H; yr++) {
    float dy = yr - FL_HALF + 0.5f;
    if (dy <= dyMax) {
      float r = dy * FL_D / (FL_D * ca + dy * sa);
      float sx = FL_D / (FL_D - r * sa);
      float q = FL_D * ca + dy * sa, step = FL_D * FL_D * ca / (q * q);
      flRow(cx, yr, newV, FL_HALF + r - step / 2, step, (int)(phi / 90 * 7), sx);
    } else {
      flRow(cx, yr, oldV, yr, 1, shadow);
    }
  }
}

static void flipHinge(int cx) {
  G->drawFastHLine(cx - FL_EXT, FL_Y + FL_HALF - 1, FL_BW, BG);
  G->drawFastHLine(cx - FL_EXT, FL_Y + FL_HALF, FL_BW, BG);
  G->fillCircle(cx, FL_Y + FL_HALF, 3, BG);
  G->fillCircle(cx + FL_W - 1, FL_Y + FL_HALF, 3, BG);
}

// Cartão parado mostrando "v".
static void flipStatic(int cx, int v) {
  for (int yr = 0; yr < FL_H; yr++)
    if (yr != FL_HALF - 1 && yr != FL_HALF) flRow(cx, yr, v, yr, 1, 0);
  flipHinge(cx);
}

// Vira até 2 cartões ao mesmo tempo. Física simples: a aba cai com aceleração constante
// (ângulo cresce com t²) e quica de leve ao bater. Guiado pelo relógio, não por número de
// quadros: dura ~0,37 s com quantos quadros o aparelho conseguir desenhar.
static void flipAnimate(int n, const int *cx, const int *oldV, const int *newV) {
  const float T_FALL = 280, T_BOUNCE = 90, BOUNCE = 10;  // ms, ms, graus
  uint32_t t0 = uiMillis ? uiMillis() : 0;
  float t = 0, prev = 0;
  for (;;) {
    float th;
    if (t < T_FALL) th = 180 * (t / T_FALL) * (t / T_FALL);
    else if (t < T_FALL + T_BOUNCE) th = 180 - BOUNCE * sinf(K_PI * (t - T_FALL) / T_BOUNCE);
    else break;
    for (int i = 0; i < n; i++) {
      if (th < 90) flipTopHalf(cx[i], oldV[i], newV[i], th);
      else {
        if (prev < 90) flipTopHalf(cx[i], oldV[i], newV[i], 90);  // a aba passou: número novo inteiro em cima
        flipBottomHalf(cx[i], oldV[i], newV[i], th);
      }
      flipHinge(cx[i]);
    }
    prev = th;
    if (uiFrameHook) uiFrameHook();
    t = uiMillis ? (float)(uiMillis() - t0) : t + 20;
  }
  for (int i = 0; i < n; i++) flipStatic(cx[i], newV[i]);
}

// ---------- Bebas Neue genérico (usado pelos pacotes de mostrador) ----------
// Só dígitos, ':' e espaço. Altura h px; largura de cada dígito = 400·h/720 (dígitos tabulares).
static int bbAdv(char ch, int h) { return ch == ':' ? h / 4 : ch == ' ' ? 200 * h / 720 : 400 * h / 720; }

int bebasWidth(const char *s, int h) {
  int w = 0;
  for (; *s; s++) w += bbAdv(*s, h);
  return w;
}

static void bbSpan(uint8_t *cov, int n, int a, int b) {
  if (a < 0) a = 0;
  if (b > n * 16) b = n * 16;
  if (b <= a) return;
  int ia = a >> 4, ib = b >> 4;
  if (ia == ib) {
    cov[ia] += b - a;
    return;
  }
  cov[ia] += 16 - (a & 15);
  for (int i = ia + 1; i < ib; i++) cov[i] += 16;
  if (ib < n) cov[ib] += b & 15;
}

// Texto suavizado sobre "bg", desenhado linha a linha pelo caminho rápido (blit).
void bebasText(int x, int y, int h, const char *s, uint16_t fg, uint16_t bg) {
  int W = bebasWidth(s, h);
  if (W > 240) W = 240;
  if (W <= 0 || h <= 0) return;
  // buffers só durante o desenho (RAM fixa é escassa: o GIF do álbum precisa de ~24 KB contíguos)
  uint8_t *cov = (uint8_t *)malloc(W * 3 + 2);  // cobertura (W) + linha RGB565 alinhada (2W)
  if (!cov) return;
  uint16_t *row = (uint16_t *)(cov + ((W + 1) & ~1));
  uint16_t pal[17];
  for (int i = 0; i <= 16; i++) pal[i] = mix16(bg, fg, i);
  const int dot = h / 9 > 1 ? h / 9 : 2;
  for (int r = 0; r < h; r++) {
    memset(cov, 0, W);
    int ox = 0;
    for (const char *p = s; *p; ox += bbAdv(*p, h), p++) {
      char ch = *p;
      if (ch == ':') {  // dois-pontos: dois quadrados
        int cx = ox + bbAdv(':', h) / 2 - dot / 2;
        if ((r >= h * 3 / 10 && r < h * 3 / 10 + dot) || (r >= h * 7 / 10 - dot && r < h * 7 / 10))
          for (int i = cx; i < cx + dot && i < W; i++) cov[i] = 64;
        continue;
      }
      if (ch < '0' || ch > '9') continue;
      int d = ch - '0';
      for (int j = 0; j < 4; j++) {
        int sy = (int)((r + (j + 0.5f) / 4) * FLIP_CH / h);
        int xs[24], n = 0;
        for (int c = FLIP_DIG[d]; c < FLIP_DIG[d + 1]; c++) {
          int s0 = c ? pgm_read_word(&FLIP_CEND[c - 1]) : 0, e = pgm_read_word(&FLIP_CEND[c]);
          for (int i = s0; i < e; i++) {
            int k = i + 1 < e ? i + 1 : s0;
            int ay = (int16_t)pgm_read_word(&FLIP_PT[i][1]), by = (int16_t)pgm_read_word(&FLIP_PT[k][1]);
            if ((ay <= sy && sy < by) || (by <= sy && sy < ay)) {
              int ax = (int16_t)pgm_read_word(&FLIP_PT[i][0]), bx = (int16_t)pgm_read_word(&FLIP_PT[k][0]);
              if (n < 24) xs[n++] = ax * 16 + (sy - ay) * (bx - ax) * 16 / (by - ay);
            }
          }
        }
        for (int i = 1; i < n; i++)
          for (int k = i; k > 0 && xs[k] < xs[k - 1]; k--) {
            int t = xs[k];
            xs[k] = xs[k - 1];
            xs[k - 1] = t;
          }
        for (int k = 0; k + 1 < n; k += 2) bbSpan(cov, W, ox * 16 + xs[k] * h / FLIP_CH, ox * 16 + xs[k + 1] * h / FLIP_CH);
      }
    }
    for (int i = 0; i < W; i++) row[i] = pal[(cov[i] > 64 ? 64 : cov[i]) / 4];
    blit(x, y + r, row, W, 1);
  }
  free(cov);
}

static void pill(int x, int w) { G->fillRoundRect(x, 186, w, 28, 14, CARD); }

void themeFlip(const UiState &s, bool full) {
  char d[24];
  dateLabel(s, d, sizeof d);
  if (changed(cStatL, d)) {
    G->fillRect(0, 8, 240, 18, BG);
    textC(120, 12, 2, MUTED, d);
  }
  int h = s.timeOk ? displayHour(s) : -1, m = s.timeOk ? s.now.tm_min : -1;
  // na primeira vez desenha parado; depois, as plaquinhas que mudaram viram juntas
  int cx[2], ov[2], nv[2], n = 0;
  const int v[2] = {h, m};
  for (int i = 0; i < 2; i++) {
    if (v[i] == lastDig[i]) continue;
    if (full || lastDig[i] < 0 || v[i] < 0) flipStatic(FL_X[i], v[i]);
    else {
      cx[n] = FL_X[i];
      ov[n] = lastDig[i];
      nv[n++] = v[i];
    }
    lastDig[i] = v[i];
  }
  if (n) flipAnimate(n, cx, ov, nv);
  if (full || lastWx != s.wx.version || lastWxOn != (int)s.weatherOn) {
    G->fillRect(8, 184, 110, 32, BG);
    pill(10, 105);
    if (s.weatherOn && s.wx.ok) {
      weatherIcon(34, 199, 0.42f, s.wx.code, s.wx.day, CARD);
      tempText(80, 193, 2, TEXT, s.wx.t);
    } else textC(62, 197, 1, MUTED, "sem clima");
    lastWx = s.wx.version;
    lastWxOn = s.weatherOn;
  }
  if (full || lastToday != s.today || lastGoal != s.dailyGoal) {
    G->fillRect(122, 184, 112, 32, BG);
    pill(125, 105);
    mascot(137, 192, 1, s.today >= s.dailyGoal ? FACE_CHEER : FACE_HAPPY);
    char f[16];
    snprintf(f, sizeof f, "%u/%u", s.today, s.dailyGoal);
    text(160, 193, 2, s.today >= s.dailyGoal ? SHORT : TEXT, f);
    lastToday = s.today;
    lastGoal = s.dailyGoal;
  }
  if (changed(cStatR, s.ip ? s.ip : "")) {
    G->fillRect(0, 224, 240, 10, BG);
    textC(120, 226, 1, MUTED, cStatR.s);
  }
}

