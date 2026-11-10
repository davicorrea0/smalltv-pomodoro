// Simulador das telas do SmallTV: roda o ui.cpp do firmware num framebuffer 240x240
// e grava cada tela em out/<nome>.raw (RGB565). png.js converte para PNG.
//
// Também testa o redesenho incremental: desenha uma sequência de estados (segundos
// passando, virada de minuto, clima novo, foco concluído) e compara o resultado final
// com um desenho feito do zero. Qualquer pixel diferente = sobra de desenho na tela.

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define UI_ASSET_LIST  // assets.h inclui a lista com o nome de cada asset (para a folha de prévia)
#include "../smalltv-pomodoro/ui_kit.h"
#include "../smalltv-pomodoro/assets.h"
#include "../smalltv-pomodoro/facepack.h"
#include "../smalltv-pomodoro/setup_qr.h"
#include "../smalltv-pomodoro/warm.h"
#include <chrono>
#include <dirent.h>
#include <vector>
#include <string>
#include <set>

class Frame : public Adafruit_GFX {
 public:
  uint16_t px[240 * 240];
  // piscada: durante um redesenho, um pixel que mostrou uma cor que não é nem a de antes nem a de
  // depois (ex.: o fundo entre apagar e redesenhar o ponteiro). log guarda (posição << 16 | cor).
  std::vector<uint32_t> *log = nullptr;
  Frame() : Adafruit_GFX(240, 240) { memset(px, 0, sizeof px); }
  void drawPixel(int16_t x, int16_t y, uint16_t c) override {
    if (x < 0 || y < 0 || x >= 240 || y >= 240) return;
    px[y * 240 + x] = c;
    if (log) log->push_back((uint32_t)(y * 240 + x) << 16 | c);
  }
};

static void save(const Frame &f, const char *name) {
  char path[128];
  snprintf(path, sizeof path, "out/%s.raw", name);
  FILE *fp = fopen(path, "wb");
  if (!fp) {  // sem a pasta out/ (o build.sh cria)
    perror(path);
    exit(1);
  }
  fwrite(f.px, 2, 240 * 240, fp);
  fclose(fp);
}

static UiState base(int h, int m, int s) {
  UiState st;
  memset(&st, 0, sizeof st);
  st.timeOk = true;
  st.now.tm_year = 126; st.now.tm_mon = 9; st.now.tm_mday = 20; st.now.tm_wday = 2;
  st.now.tm_hour = h; st.now.tm_min = m; st.now.tm_sec = s;
  st.now.tm_yday = 292;  // 20 de outubro
  st.now.tm_isdst = -1;
  st.utcOff = -3 * 3600;  // Florianópolis: UTC-3, -27,6° -48,5°
  st.lat10 = -276; st.lon10 = -485; st.hasLoc = true;
  st.longEvery = 4;
  st.dailyGoal = 8;
  st.today = 3;
  st.streak = 5;  // planta com tomate verde e chama na prévia
  st.best = 9;
  st.weatherOn = true;
  st.wx = {true, 23.4f, 14.2f, 27.1f, 44, 2, true, 1, 5, {{2, 14, 27, 10}, {61, 15, 21, 80}, {95, 13, 19, 100}, {3, -2, 22, 20}, {0, 12, 29, 0}}};
  st.city = "Florianopolis";
  st.accent = uiRgb(242, 242, 242);
  st.ip = "192.168.1.101";
  return st;
}

// Tela de tamanho livre (folha de prévia dos assets)
class Canvas : public Adafruit_GFX {
 public:
  int w, h;
  uint16_t *px;
  Canvas(int w_, int h_, uint16_t bg) : Adafruit_GFX(w_, h_), w(w_), h(h_), px(new uint16_t[w_ * h_]) {
    for (int i = 0; i < w * h; i++) px[i] = bg;
  }
  ~Canvas() { delete[] px; }
  void drawPixel(int16_t x, int16_t y, uint16_t c) override {
    if (x < 0 || y < 0 || x >= w || y >= h) return;
    px[y * w + x] = c;
  }
};

// Folha com todos os assets (cada quadro das animações lado a lado) -> out/assets.bin
// (cabeçalho: largura e altura em uint16; depois os pixels RGB565). png.js gera out/assets.png.
static void assetSheet() {
  const int SC = 4, CW = 124, CH = 112, COLS = 6;
  const int n = sizeof UI_ASSETS / sizeof UI_ASSETS[0];
  // primeiro os sprites parados; cada animação começa numa linha nova
  for (int draw = 0, cells = 0; draw < 2; draw++) {
    Canvas *c = nullptr;
    if (draw) {
      c = new Canvas(COLS * CW + 8, (cells + COLS - 1) / COLS * CH + 8, BG);
      uiBegin(c);
    }
    int k = 0;
    for (int anim = 0; anim < 2; anim++)
      for (int i = 0; i < n; i++) {
        const UiSprite *sp = UI_ASSETS[i].sprite;
        if ((sp->frames > 1) != (anim == 1)) continue;
        if (anim && k % COLS) k += COLS - k % COLS;
        for (int f = 0; f < sp->frames; f++, k++) {
          if (!c) continue;
          int x = 8 + k % COLS * CW, y = 8 + k / COLS * CH;
          c->fillRoundRect(x, y, CW - 8, CH - 8, 8, UI_ASSETS[i].bg >= 0 ? UI_ASSETS[i].bg : CARD);
          uiSprite(sp, x + (CW - 8 - sp->w * SC) / 2, y + 6 + (80 - sp->h * SC) / 2, SC, f);
          char label[32];
          if (f == 0) snprintf(label, sizeof label, "%s", UI_ASSETS[i].name);
          else snprintf(label, sizeof label, "quadro %d", f + 1);
          textC(x + (CW - 8) / 2, y + CH - 22, 1, f ? MUTED : TEXT, label);
        }
      }
    cells = k;
    if (!c) continue;
    FILE *fp = fopen("out/assets.bin", "wb");
    uint16_t wh[2] = {(uint16_t)c->w, (uint16_t)c->h};
    fwrite(wh, 2, 2, fp);
    fwrite(c->px, 2, c->w * c->h, fp);
    fclose(fp);
    delete c;
  }
}

static Frame *F;
static Frame *animFrame = nullptr;
static int animN = 0;
static void saveAnimFrame() {
  char n[32];
  snprintf(n, sizeof n, "flip_anim_%02d", animN++);
  save(*animFrame, n);
}

static void shot(const char *name, const UiState &s, bool pomo) {
  Frame *f = new Frame();
  uiBegin(f);
  if (pomo) uiRenderPomodoro(s);
  else uiRenderClock(s);
  save(*f, name);
  delete f;
}

// ---- painel ST7789 modelado: RAM de 320 linhas + rolagem por hardware ----
// Modela o que main.cpp (tftScroll/tftClearMargin) assume do chip, nas duas orientações: endereço de linha =
// y (+80 girado), o MY espelha a RAM (319 - endereço) e a linha lida n mostra a linha de RAM n + VSCSAD.
// O VSCSAD vem de uiScrollSad (o mesmo do firmware). A cada comando de rolagem confere a vista inteira.
struct Panel : public Adafruit_GFX {
  uint16_t ram[320 * 240];  // por endereço de linha
  bool flip = false;
  int sad = 0;
  long blitBytes = 0, cmds = 0, bad = 0, badAt = -1;
  const Frame *A = nullptr, *B = nullptr;
  bool mid = false, finished = false;
  Panel() : Adafruit_GFX(240, 240) {}
  void put(int x, int y, uint16_t c) {  // y como a biblioteca recebe (0..239); o offset de linha é dela
    int a = y + (flip ? 80 : 0);
    if (x >= 0 && x < 240 && y >= 0 && y < 240) ram[a * 240 + x] = c;
  }
  void drawPixel(int16_t x, int16_t y, uint16_t c) override { put(x, y, c); }
  uint16_t view(int x, int v) const {
    int n = flip ? 239 - v : v, ring = (n + sad) % 320, a = flip ? 319 - ring : ring;
    return ram[a * 240 + x];
  }
  // margem = endereços fora das 240 linhas da biblioteca; preta uma vez, como tftClearMargin
  void clearMargin() { for (int a = flip ? 0 : 240; a < (flip ? 80 : 320); a++) for (int x = 0; x < 240; x++) ram[a * 240 + x] = 0; }
  // vista esperada com o conteúdo L linhas acima: antigo, vão preto, novo
  void check(int L, bool wantB) {
    long d = 0;
    for (int v = 0; v < 240; v++)
      for (int x = 0; x < 240; x++) {
        int y = v + L;
        uint16_t e = wantB ? B->px[v * 240 + x] : y < 240 ? A->px[y * 240 + x] : y < 320 ? 0 : B->px[(y - 320) * 240 + x];
        d += view(x, v) != e;
      }
    if (d) { bad += d; if (badAt < 0) badAt = L; }
  }
};
static Panel *PN;
static void simBlit(int16_t x, int16_t y, uint16_t *px, int16_t w, int16_t h) {  // = tftBitmap
  for (int r = 0; r < h; r++)
    for (int i = 0; i < w; i++) PN->put(x + i, y + r, px[r * w + i]);
  PN->blitBytes += (long)w * h * 2;
}
static void simScroll(int16_t rows, int16_t off) {
  int r = rows ? rows : 320;
  PN->sad = uiScrollSad(PN->flip, r, off);
  PN->cmds++;
  if (!rows) { PN->check(0, PN->finished = true); return; }
  if (off) PN->mid = true;
  PN->check(off, PN->mid && !off);  // offset 0 depois de ter rolado = já voltou ao início, com o novo
}

// Troca animada por rolagem de hardware: cada mostrador -> o seguinte, nas duas orientações. Em todo
// comando de rolagem, a vista tem que ser antigo / vão preto / novo; no fim, offset 0 e exatamente a
// tela que um desenho completo do mostrador novo daria. Mede o custo em relação a um desenho completo.
static int checkSlide() {
  int fails = 0;
  long cmds = 0, bytes = 0;
  double tSlide = 0, tFull = 0;
  Panel *p = new Panel();
  Frame *a = new Frame(), *b = new Frame();
  PN = p;
  // entre dois mostradores LCD a troca é a da tinta (checkLcdCurtain), não a rolagem
  auto lcd = [](int t) { return t == THEME_LCD || t == THEME_FORECAST_LCD || t == THEME_WORLD || t == THEME_WORLD_WATCH; };
  int n = 0;
  for (int flip = 0; flip < 2; flip++)
    for (int t = 0; t < THEME_COUNT; t++) {
      int u = (t + 1) % THEME_COUNT;
      if (lcd(t) && lcd(u)) continue;
      n++;
      UiState sa = base(14, 58, 50), sb = base(14, 58, 50);
      sa.theme = t;
      sb.theme = u;
      uiScrollHw = nullptr;
      uiBlit = nullptr;
      uiBegin(a); uiRenderClock(sa);
      auto t0 = std::chrono::steady_clock::now();
      uiBegin(b); uiRenderClock(sb);
      tFull += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
      for (auto &px : p->ram) px = 0xA5A5;  // lixo, como a RAM do painel ao ligar
      p->flip = flip; p->sad = 0; p->blitBytes = p->cmds = p->bad = 0; p->badAt = -1; p->mid = p->finished = false;
      p->A = a; p->B = b;
      p->clearMargin();
      uiScrollHw = simScroll;
      uiBlit = simBlit;
      uiBegin(p);
      uiRenderClock(sa);
      bool okA = true;
      for (int v = 0; v < 240; v++) for (int x = 0; x < 240; x++) okA &= p->view(x, v) == a->px[v * 240 + x];
      uiInvalidateSlide();
      t0 = std::chrono::steady_clock::now();
      uiRenderClock(sb);
      tSlide += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
      p->check(0, true);
      uiRenderClock(sb);  // quadro incremental depois da troca: nada muda
      p->check(0, true);
      cmds += p->cmds; bytes += p->blitBytes;
      if (!okA || p->bad || !p->finished || p->sad || uiScrollActive() || p->cmds < 20) {
        printf("rolagem %s%s -> %s: %ld pixels errados (1o em offset %ld), %ld comandos, antes ok=%d, fim=%d, sad=%d, ativa=%d\n", flip ? "girado " : "", uiFaceName(t), uiFaceName(u), p->bad, p->badAt, p->cmds, okA, p->finished, p->sad, uiScrollActive());
        fails++;
      }
    }
  uiScrollHw = nullptr;
  uiBlit = nullptr;
  if (!fails) printf("troca por rolagem de hardware: ok (%d trocas, as duas orientações); em media %ld comandos de rolagem e %ld KB no SPI por troca; CPU de desenho %ld,%ld vezes um desenho completo\n", n, cmds / n, bytes / n / 1024, (long)(tSlide / tFull * 10) / 10, (long)(tSlide / tFull * 10) % 10);
  delete p; delete a; delete b;
  return fails;
}

// Troca LCD retrô <-> Previsão LCD (ui.cpp, lcdFade): a moldura tem que ficar parada. Em todo quadro da
// transição, os pixels fora do miolo (x 10..229, y 28..207) e dos rótulos de fora (y < 24 e y >= 210) são os
// de antes (que são os mesmos do mostrador novo: a moldura é idêntica nos dois); no fim, a tela é igual a
// um desenho do zero do mostrador novo (nada sobra do anterior). Grava os quadros em out/inc_segmentos_*.raw.
static std::vector<std::vector<uint16_t>> segmentos;
static Frame *segmentosF, *segmentosA;
static long segmentosBad;  // pixels da moldura diferentes do início, em qualquer faixa desenhada
static bool lcdMoldura(int x, int y);
static void segmentosFaixa() {
  for (int i = 0; i < 240 * 240; i++) segmentosBad += lcdMoldura(i % 240, i / 240) && segmentosF->px[i] != segmentosA->px[i];
}
static void segmentosHook() {
  if (segmentos.empty() || memcmp(segmentos.back().data(), segmentosF->px, sizeof segmentosF->px))
    segmentos.emplace_back(segmentosF->px, segmentosF->px + 240 * 240);  // quadros repetidos (espera) não contam
}
static bool lcdMoldura(int x, int y) {
  return !(y < 24 || y >= 210 || (x >= 10 && x < 230 && y >= 28 && y < 208));
}
static int checkLcdCurtain() {
  int fails = 0;
  Frame *f = new Frame(), *a = new Frame(), *b = new Frame();
  segmentosF = f;
  segmentosA = a;
  // os dois sentidos entre LCD retrô e Previsão LCD (as prévias da troca), o Mundo com cada um deles e o Mundo relógio
  static const int8_t PAR[5][2] = {{THEME_LCD, THEME_FORECAST_LCD}, {THEME_FORECAST_LCD, THEME_LCD}, {THEME_LCD, THEME_WORLD}, {THEME_WORLD, THEME_FORECAST_LCD}, {THEME_WORLD, THEME_WORLD_WATCH}};
  static const char *const NOME[5] = {"lcd_previsao-lcd", "previsao-lcd_lcd", "lcd_mundo", "mundo_previsao-lcd", "mundo_mundo-relogio"};
  for (int dir = 0; dir < 5; dir++) {
    int from = PAR[dir][0], to = PAR[dir][1];
    UiState sa = base(14, 58, 50), sb = sa;
    sa.theme = from;
    sb.theme = to;
    uiBegin(a); uiRenderClock(sa);
    uiBegin(b); uiRenderClock(sb);
    uiBegin(f); uiRenderClock(sa);
    segmentos.clear();
    segmentosHook();
    segmentosBad = 0;
    uiFrameHook = segmentosFaixa;  // a cada faixa desenhada
    uiStepHook = segmentosHook;    // a cada quadro da troca
    uiInvalidateSlide();
    uiRenderClock(sb);
    uiFrameHook = nullptr;
    uiStepHook = nullptr;
    long moldura = segmentosBad, sobra = 0;
    for (int i = 0; i < 240 * 240; i++) {
      sobra += f->px[i] != b->px[i];
      moldura += lcdMoldura(i % 240, i / 240) && a->px[i] != b->px[i];  // moldura dos dois mostradores idêntica
    }
    bool ok = !moldura && !sobra && segmentos.size() >= 12;
    printf("segmentos %s -> %s: %s (%zu quadros, %ld pixels da moldura mudaram, %ld pixels sobrando)\n", uiFaceName(from), uiFaceName(to), ok ? "ok" : "FALHOU", segmentos.size(), moldura, sobra);
    fails += !ok;
    for (size_t k = 0; k < segmentos.size(); k++) {
      Frame q;
      memcpy(q.px, segmentos[k].data(), sizeof q.px);
      char n[64];
      snprintf(n, sizeof n, "inc_segmentos_%s_%02zu", NOME[dir], k);
      save(q, n);
    }
    uiRenderClock(sb);  // quadro incremental depois da troca: nada muda
    long depois = 0;
    for (int i = 0; i < 240 * 240; i++) depois += f->px[i] != b->px[i];
    if (depois) printf("segmentos %s -> %s: FALHOU no quadro seguinte à troca (%ld pixels diferentes)\n", uiFaceName(from), uiFaceName(to), depois);
    fails += depois != 0;
  }
  delete f; delete a; delete b;
  return fails;
}

// Troca de tela: cada mostrador -> Pomodoro e Pomodoro -> cada mostrador tem que dar o mesmo que desenhar
// a tela nova numa tela limpa. Até a 1.19 o Pomodoro e o 11º mostrador tinham o mesmo número de tela e a
// troca entre eles não limpava: um aparecia por cima do outro.
static int checkTransitions(const UiState &pomo) {
  int fails = 0;
  Frame *a = new Frame(), *b = new Frame();
  for (int t = 0; t < THEME_COUNT; t++) {
    UiState c = base(14, 58, 50);
    c.theme = t;
    for (int dir = 0; dir < 2; dir++) {  // 0: mostrador -> Pomodoro; 1: Pomodoro -> mostrador
      uiBegin(a);
      if (dir == 0) { uiRenderClock(c); uiRenderPomodoro(pomo); } else { uiRenderPomodoro(pomo); uiRenderClock(c); }
      uiBegin(b);
      if (dir == 0) uiRenderPomodoro(pomo); else uiRenderClock(c);
      int diff = 0;
      for (int i = 0; i < 240 * 240; i++) diff += a->px[i] != b->px[i];
      if (diff) {
        printf("troca %-22s %s: %d pixels sobrando da tela anterior\n", uiFaceName(t), dir ? "<- Pomodoro" : "-> Pomodoro", diff);
        fails++;
      }
    }
  }
  if (!fails) printf("troca mostrador <-> Pomodoro: ok (%d mostradores, nos dois sentidos)\n", THEME_COUNT);
  delete a;
  delete b;
  return fails;
}

// Sequência incremental vs. desenho do zero
static int checkIncremental(const char *name, UiState s, bool pomo) {
  Frame *inc = new Frame();
  uiBegin(inc);
  std::vector<uint32_t> log;
  std::vector<uint16_t> before(240 * 240);
  long flicker = 0;
  for (int step = 0; step < 140; step++) {
    memcpy(before.data(), inc->px, sizeof inc->px);
    log.clear();
    inc->log = step ? &log : nullptr;  // o primeiro desenho é a tela inteira: não conta
    if (pomo) {
      s.remainingMs = s.remainingMs > 1000 ? s.remainingMs - 1000 : 0;
      s.ms += 100;
      if (step == 20) s.celebrate = true;          // fase terminou: animação de fim
      if (step == 47) s.celebrate = false;
      if (step == 55) s.activity = ACT_CODE;       // troca de atividade no meio do foco
      if (step == 70) s.running = !s.running;      // pausa no meio
      if (step == 90) s.running = !s.running;
      if (step == 100) { s.cycleDone++; }          // ciclo avança
    } else {
      if (s.theme == THEME_PACK) s.ms += 100;  // sprites animados dos pacotes andam com o relógio em ms
      if (++s.now.tm_sec >= 60) { s.now.tm_sec = 0; if (++s.now.tm_min >= 60) { s.now.tm_min = 0; s.now.tm_hour = (s.now.tm_hour + 1) % 24; } }
      if (step == 50) { s.wx.t = 9.6f; s.wx.code = 63; s.wx.tmin = 7; s.wx.tmax = 12; s.wx.version++; }
      if (step == 80) s.today++;
      if (step == 110) { s.weatherOn = false; }
      if (step == 125) s.today = s.dailyGoal;      // meta batida: tomate comemorando
    }
    if (pomo) uiRenderPomodoro(s);
    else uiRenderClock(s);
    std::set<uint32_t> flash;
    for (uint32_t e : log) {
      uint32_t i = e >> 16;
      uint16_t c = e & 0xFFFF;
      if (c != before[i] && c != inc->px[i]) flash.insert(i);
    }
    flicker += flash.size();
  }
  inc->log = nullptr;
  Frame *fresh = new Frame();
  uiBegin(fresh);
  if (pomo) uiRenderPomodoro(s);
  else uiRenderClock(s);
  int diff = 0, x0 = 240, y0 = 240, x1 = -1, y1 = -1;
  for (int i = 0; i < 240 * 240; i++)
    if (inc->px[i] != fresh->px[i]) {
      diff++;
      int x = i % 240, y = i / 240;
      if (x < x0) x0 = x; if (y < y0) y0 = y; if (x > x1) x1 = x; if (y > y1) y1 = y;
    }
  printf("%-22s %s (%d pixels diferentes, %ld pixels piscando)", name, diff ? "FALHOU" : "ok", diff, flicker);
  if (diff) printf("  area x %d-%d, y %d-%d", x0, x1, y0, y1);
  printf("\n");
  char n[64];
  snprintf(n, sizeof n, "inc_%s", name);
  save(*inc, n);
  snprintf(n, sizeof n, "fresh_%s", name);
  save(*fresh, n);
  delete inc;
  delete fresh;
  return diff;
}

// Pacotes de mostrador (faces/*.json na raiz do repositório): prévia em out/pack-<id>.raw e
// o mesmo teste de redesenho dos mostradores embutidos.
static int testPackages(const UiState &pomo) {
  int fails = 0, packs = 0;
  DIR *dir = opendir("../faces");
  struct dirent *e;
  while (dir && (e = readdir(dir))) {
    size_t n = strlen(e->d_name);
    if (n < 6 || strcmp(e->d_name + n - 5, ".json") || !strcmp(e->d_name, "catalog.json")) continue;
    char path[160];
    snprintf(path, sizeof path, "../faces/%s", e->d_name);
    FILE *fp = fopen(path, "rb");
    if (!fp) continue;
    static char buf[24577];
    size_t len = fread(buf, 1, sizeof buf - 1, fp);
    fclose(fp);
    buf[len] = 0;
    char err[96];
    if (!facePackLoad(buf, len, err, sizeof err)) {
      printf("%-22s ERRO: %s\n", e->d_name, err);
      fails++;
      continue;
    }
    char name[48];
    snprintf(name, sizeof name, "pack-%s", facePackId());
    UiState st = base(14, 37, 24);
    st.theme = THEME_PACK;
    shot(name, st, false);
    if (strstr(buf, "\"frames\"") || strstr(buf, "\"glow\"")) {  // sprite animado: alguns quadros da animação para conferir
      for (int k = 1; k <= 3; k++) {
        char fn[64];
        UiState a = st;
        a.ms = k * (strstr(buf, "\"glow\"") ? 600 : 170);
        snprintf(fn, sizeof fn, "%s-q%d", name, k);
        shot(fn, a, false);
      }
    }
    UiState t = base(14, 58, 50);
    t.theme = THEME_PACK;
    fails += checkIncremental(name, t, false) != 0;
    facePackUnload();
    packs++;
  }
  if (dir) closedir(dir);
  if (!packs) {  // pasta ilegível não pode passar como "nenhum pacote, nenhum erro"
    printf("pacotes: ERRO, nenhum lido de ../faces\n");
    fails++;
  }
  (void)pomo;
  return fails;
}


// Limites dos sprites animados: cada JSON inválido precisa ser recusado com mensagem; os válidos, aceitos
static int testSpriteLimits() {
  int fails = 0;
  auto frames = [](int n, int w, int h) {
    std::string r = "[";
    for (int k = 0; k < n; k++) {
      r += k ? ",[" : "[";
      for (int y = 0; y < h; y++) r += (y ? ",\"" : "\"") + std::string(w, k & 1 ? 'a' : 'b') + "\"";
      r += "]";
    }
    return r + "]";
  };
  auto pack = [&](const std::string &spr, int fmt = 1) {
    return "{\"format\":" + std::to_string(fmt) + ",\"sprites\":{" + spr + "},\"layers\":[{\"type\":\"rect\",\"w\":10,\"h\":10}]}";
  };
  auto sp = [&](const char *name, const std::string &fr, const char *extra = "") {
    return std::string("\"") + name + "\":{\"pal\":{\"a\":\"#fff\",\"b\":\"#f00\"},\"frames\":" + fr + extra + "}";
  };
  auto textos = [](int n, int tam) {
    std::string r = "{\"format\":2,\"layers\":[";
    for (int i = 0; i < n; i++) {
      auto s = [&](int k) {
        char p[8];
        snprintf(p, sizeof p, "%03d_", i * 3 + k);
        return (std::string(p) + std::string(tam, 'a' + (i + k) % 26)).substr(0, tam);
      };
      r += std::string(i ? "," : "") + "{\"type\":\"text\",\"text\":{\"pt\":\"" + s(0) + "\",\"en\":\"" + s(1) + "\",\"es\":\"" + s(2) + "\"}}";
    }
    return r + "]}";
  };
  struct { const char *nome; std::string json; bool ok; } T[] = {
    {"8 quadros 32x32", pack(sp("s", frames(8, 32, 32))), true},
    {"9 quadros", pack(sp("s", frames(9, 8, 8))), false},
    {"fps 11", pack(sp("s", frames(2, 8, 8), ",\"fps\":11")), false},
    {"fps 0", pack(sp("s", frames(2, 8, 8), ",\"fps\":0")), false},
    {"fps 10", pack(sp("s", frames(2, 8, 8), ",\"fps\":10")), true},
    {"largura diferente", pack(sp("s", "[[\"aa\",\"bb\"],[\"aaa\",\"bbb\"]]")), false},
    {"altura diferente", pack(sp("s", "[[\"aa\",\"bb\"],[\"aa\"]]")), false},
    {"33x33", pack(sp("s", frames(2, 33, 33))), false},
    {"total > 6144 bytes", pack(sp("a", frames(8, 32, 32)) + "," + sp("b", frames(8, 32, 32))), false},
    {"frames vazio", pack(sp("s", "[]")), false},
    // formato 2 (1.26.0): até 16 quadros e 8.192 B crus; fps continua até 10 (o relógio desenha a 10 Hz)
    {"f2 16 quadros", pack(sp("s", frames(16, 8, 8)), 2), true},
    {"f2 17 quadros", pack(sp("s", frames(17, 8, 8)), 2), false},
    {"f2 fps 11", pack(sp("s", frames(2, 8, 8), ",\"fps\":11"), 2), false},
    {"f2 16 quadros 31x32", pack(sp("s", frames(16, 31, 32)), 2), true},
    {"f2 total > 8192 bytes", pack(sp("a", frames(16, 32, 32)) + "," + sp("b", frames(1, 8, 8)), 2), false},
    {"f2 sem novidade", pack(sp("s", frames(2, 8, 8)), 2), true},
    {"f1 16 quadros", pack(sp("s", frames(16, 8, 8))), false},
    {"f1 7 KB crus", pack(sp("a", frames(8, 32, 32)) + "," + sp("b", frames(8, 24, 24))), false},
    {"formato 3", pack(sp("s", frames(2, 8, 8)), 3), false},
    // arquivo acima de 16 KB (a 1.25.0 recusa): só no formato 2
    {"f1 arquivo 17 KB", "{\"format\":1,\"desc\":\"" + std::string(17000, 'x') + "\",\"layers\":[{\"type\":\"rect\",\"w\":10,\"h\":10}]}", false},
    {"f2 arquivo 17 KB", "{\"format\":2,\"desc\":\"" + std::string(17000, 'x') + "\",\"layers\":[{\"type\":\"rect\",\"w\":10,\"h\":10}]}", true},
    // textos: até 47 B por texto (cabe em char txt[48]) e 1.536 B no pool do pacote
    {"texto 47 B", "{\"format\":2,\"layers\":[{\"type\":\"text\",\"text\":\"" + std::string(47, 'x') + "\"}]}", true},
    {"texto 48 B", "{\"format\":2,\"layers\":[{\"type\":\"text\",\"text\":\"" + std::string(48, 'x') + "\"}]}", false},
    {"48 textos <= 1536 B", textos(48, 9), true},
    {"48 textos > 1536 B", textos(48, 11), false},
    {"48 textos 125 B", textos(48, 125), false},
  };
  for (auto &t : T) {
    char err[96] = "";
    bool ok = facePackLoad((char *)t.json.c_str(), t.json.size(), err, sizeof err);
    if (ok) facePackUnload();
    if (ok != t.ok) { printf("sprite %-20s FALHOU (%s)\n", t.nome, ok ? "aceito" : err); fails++; }
    else printf("sprite %-20s ok%s%s\n",t.nome, ok ? "" : ": ", ok ? "" : err);
  }
  // Fronteira do teto público (tamanho cru: 4 + 15 cores x 2 + quadros x (w x h + 1) / 2 por sprite): 6.144 B
  // passam, 6.145 B não. Três sprites com o total exato, achados por busca (com dois, 6.145 não tem combinação).
  static int quadros[4097][3];  // bytes dos quadros (nf x (w x h + 1) / 2) -> um nf, w, h que dá esse valor
  for (int nf = 1; nf <= 8; nf++)
    for (int h = 1; h <= 32; h++)
      for (int w = 1; w <= 32; w++) {
        int *q = quadros[nf * ((w * h + 1) / 2)];
        if (!q[0]) q[0] = nf, q[1] = w, q[2] = h;
      }
  auto um = [&](const char *nome, int b) { return sp(nome, frames(quadros[b][0], quadros[b][1], quadros[b][2])); };
  for (int alvo : {6144, 6145, 8192, 8193}) {
    const int fmt = alvo > 6145 ? 2 : 1, teto = fmt == 1 ? 6144 : 8192;
    std::string achado;
    for (int a = 4096; a >= 1 && achado.empty(); a--)
      for (int b = 1; b <= 4096 && quadros[a][0] && achado.empty(); b++) {
        int c = alvo - 3 * 34 - a - b;
        if (quadros[b][0] && c >= 1 && c <= 4096 && quadros[c][0]) achado = pack(um("a", a) + "," + um("b", b) + "," + um("c", c), fmt);
      }
    char err[96] = "";
    bool ok = !achado.empty() && facePackLoad((char *)achado.c_str(), achado.size(), err, sizeof err);
    if (ok) facePackUnload();
    if (achado.empty() || ok != (alvo == teto)) { printf("sprite teto %d B          FALHOU (%s)\n", alvo, achado.empty() ? "sem combinacao" : ok ? "aceito" : err); fails++; }
    else printf("sprite teto %d B          ok%s%s\n", alvo, ok ? "" : ": ", ok ? "" : err);
  }
  return fails;
}

// Cena 3D ("world3d", formato 2): as cores e a geometria mudam a imagem, a câmera anda, o desenho incremental (só o que
// mudou, em faixas) fica igual ao completo quadro a quadro, o texto "overlay" preserva a cena e mistura a borda, e cada
// entrada insegura é recusada com mensagem (um pacote que derrubasse a carga reiniciaria o relógio a cada boot).
static int testWorld3D() {
  int fails = 0;
  const std::string cores = R"("colors":["#1188aa","#443399","#aa3366","#00bbaa"],"accent":"#ccffff")";
  const std::string mapa = R"("map":["########","#......#","#...#..#","#...#..#","#......#","#......#","#......#","########"])";
  auto tunel = [&](const std::string &extra, int fmt = 2) {
    return "{\"format\":" + std::to_string(fmt) + ",\"layers\":[{\"type\":\"world3d\",\"geometry\":\"tunnel\",\"fps\":8,\"speed\":16," + cores + extra + "}]}";
  };
  auto grade = [&](const std::string &cam, const std::string &extra = "", const std::string &m = "") {
    return "{\"format\":2,\"layers\":[{\"type\":\"world3d\",\"geometry\":\"grid\",\"fps\":8," + cores + "," + (m.empty() ? mapa : m) +
           ",\"period\":4000,\"camera\":[" + cam + "]" + extra + "}]}";
  };
  const std::string cam = R"({"t":0,"x":150,"y":150,"yaw":0},{"t":2000,"x":550,"y":150,"yaw":0},{"t":4000,"x":150,"y":150,"yaw":0})";
  Frame *a = new Frame(), *b = new Frame();
  UiState st = base(14, 37, 0);
  st.theme = THEME_PACK;
  st.ms = 1000;
  char err[96];
  auto render = [&](const std::string &js, Frame *f) {
    bool ok = facePackLoad((char *)js.c_str(), js.size(), err, sizeof err);
    if (ok) { uiBegin(f); facePackRender(st, true); }
    facePackUnload();
    return ok;
  };
  {
    std::string outra = tunel("");
    outra.replace(outra.find("#1188aa"), 7, "#aa2200");
    if (!render(tunel(""), a) || !render(outra, b) || !memcmp(a->px, b->px, sizeof a->px)) { printf("world3d cores FALHOU\n"); fails++; }
  }
  if (!render(tunel(""), a) || !render(tunel(",\"shape\":\"square\""), b) || !memcmp(a->px, b->px, sizeof a->px)) { printf("world3d quadrado FALHOU: %s\n", err); fails++; }
  // câmera colada na parede (x = 699, a 0,01 célula da borda) olhando para ela: a distância mínima (0,05) segura a altura
  st.ms = 2000;
  if (!render(grade(R"({"t":0,"x":150,"y":150,"yaw":0},{"t":2000,"x":699,"y":150,"yaw":0},{"t":4000,"x":150,"y":150,"yaw":0})"), a)) {
    printf("world3d camera colada FALHOU: %s\n", err);
    fails++;
  }
  st.ms = 1000;
  if (!render(grade(cam), a)) { printf("world3d corredor FALHOU: %s\n", err); fails++; }
  else {
    st.ms = 2000;
    if (!render(grade(cam), b) || !memcmp(a->px, b->px, sizeof a->px)) { printf("world3d camera FALHOU\n"); fails++; }
  }
  // incremental (faixas, só o que mudou) contra completo, 40 quadros, com o relógio por cima (overlay) e a virada do minuto
  for (const std::string &js : {tunel(""), tunel(",\"shape\":\"square\",\"sway\":4,\"spin\":3"), grade(cam)}) {
    std::string j = js;  // mais uma camada: o relógio por cima da cena
    j.insert(j.size() - 2, R"(,{"type":"text","x":120,"y":72,"font":"bebas","size":96,"align":"center","color":"#ecffff","overlay":true,"text":"{HH}:{MM}"})");
    if (!facePackLoad((char *)j.c_str(), j.size(), err, sizeof err)) { printf("world3d com overlay FALHOU: %s\n", err); fails++; continue; }
    Frame *inc = new Frame(), *fresh = new Frame();
    UiState s = base(14, 37, 0);
    s.theme = THEME_PACK;
    uiBegin(inc);
    facePackRender(s, true);
    int diff = 0;
    for (int k = 1; k <= 40; k++) {
      s.ms = k * 125;
      if (k == 20) s.now.tm_min = 38;
      uiBegin(inc);
      facePackRender(s, false);
      uiBegin(fresh);
      facePackRender(s, true);
      if (memcmp(inc->px, fresh->px, sizeof inc->px)) diff++;
    }
    facePackUnload();
    delete inc;
    delete fresh;
    if (diff) { printf("world3d incremental FALHOU: %d de 40 quadros diferentes do completo\n", diff); fails++; }
  }
  // overlay: fora dos dígitos a cena fica igual; nos dígitos há tinta e borda misturada
  {
    std::string fundo = tunel(""), texto = fundo;
    texto.insert(texto.size() - 2, R"(,{"type":"text","x":120,"y":72,"font":"bebas","size":96,"align":"center","color":"#ecffff","overlay":true,"text":"{HH}:{MM}"})");
    st.ms = 125;
    int same = 0, changed = 0, blended = 0;
    uint16_t fg = uiRgb(236, 255, 255);
    if (render(fundo, a) && render(texto, b))
      for (int y = 72; y < 168; y++)
        for (int x = 15; x < 225; x++) {
          int i = y * 240 + x;
          if (a->px[i] == b->px[i]) same++;
          else { changed++; if (b->px[i] != fg) blended++; }
        }
    if (same < 100 || changed < 100 || blended < 10) { printf("world3d overlay FALHOU: %d preservados, %d tinta, %d bordas (%s)\n", same, changed, blended, err); fails++; }
  }
  // entradas que precisam ser recusadas (com mensagem)
  const std::string fecha = R"({"t":4000,"x":150,"y":150,"yaw":0})";
  struct { const char *nome; std::string js; } R[] = {
    {"mapa aberto", grade(cam, "", R"("map":[".#######","#......#","#......#","#......#","#......#","#......#","#......#","########"])")},
    {"mapa 7 linhas", grade(cam, "", R"("map":["########","#......#","#......#","#......#","#......#","#......#","########"])")},
    {"letra no mapa", grade(cam, "", R"("map":["########","#..x...#","#......#","#......#","#......#","#......#","#......#","########"])")},
    {"camera atravessa parede", grade(R"({"t":0,"x":150,"y":250,"yaw":0},{"t":2000,"x":550,"y":250,"yaw":0},)" + std::string(R"({"t":4000,"x":150,"y":250,"yaw":0})"))},
    {"camera pelo canto", grade(R"({"t":0,"x":350,"y":150,"yaw":0},{"t":2000,"x":450,"y":250,"yaw":0},{"t":4000,"x":350,"y":150,"yaw":0})")},
    {"camera dentro da parede", grade(R"({"t":0,"x":450,"y":250,"yaw":0},)" + fecha)},
    {"camera fora do mapa", grade(R"({"t":0,"x":150,"y":150,"yaw":0},{"t":2000,"x":900,"y":150,"yaw":0},)" + fecha)},
    {"tempos fora de ordem", grade(R"({"t":0,"x":150,"y":150,"yaw":0},{"t":3000,"x":550,"y":150,"yaw":0},{"t":2000,"x":550,"y":550,"yaw":0},)" + fecha)},
    {"tempo repetido", grade(R"({"t":0,"x":150,"y":150,"yaw":0},{"t":2000,"x":550,"y":150,"yaw":0},{"t":2000,"x":550,"y":550,"yaw":0},)" + fecha)},
    {"primeiro t diferente de 0", grade(R"({"t":10,"x":150,"y":150,"yaw":0},)" + fecha)},
    {"sem ponto final", grade(R"({"t":0,"x":150,"y":150,"yaw":0},{"t":2000,"x":550,"y":150,"yaw":0})")},
    {"nao fecha o ciclo", grade(R"({"t":0,"x":150,"y":150,"yaw":0},{"t":4000,"x":550,"y":150,"yaw":0})")},
    {"yaw nao fecha", grade(R"({"t":0,"x":150,"y":150,"yaw":0},{"t":4000,"x":150,"y":150,"yaw":90})")},
    {"yaw 721", grade(R"({"t":0,"x":150,"y":150,"yaw":721},{"t":4000,"x":150,"y":150,"yaw":721})")},
    {"7 pontos", grade(R"({"t":0,"x":150,"y":150,"yaw":0},{"t":500,"x":160,"y":150,"yaw":0},{"t":1000,"x":170,"y":150,"yaw":0},{"t":1500,"x":180,"y":150,"yaw":0},{"t":2000,"x":190,"y":150,"yaw":0},{"t":3000,"x":200,"y":150,"yaw":0},)" + fecha)},
    {"1 ponto", grade(R"({"t":0,"x":150,"y":150,"yaw":0})")},
    {"ponto sem yaw", grade(R"({"t":0,"x":150,"y":150},)" + fecha)},
    {"ponto em texto", grade(R"("t",)" + fecha)},
    {"geometria desconhecida", "{\"format\":2,\"layers\":[{\"type\":\"world3d\",\"geometry\":\"sphere\"," + cores + "}]}"},
    {"forma hexagono", tunel(",\"shape\":\"hexagon\"")},
    {"fps 60", tunel(",\"fps\":60")},
    {"fps 0", tunel(",\"fps\":0")},
    {"speed 33", tunel(",\"speed\":33")},
    {"sway 9", tunel(",\"sway\":9")},
    {"3 cores", "{\"format\":2,\"layers\":[{\"type\":\"world3d\",\"geometry\":\"tunnel\",\"colors\":[\"#111111\",\"#222222\",\"#333333\"],\"accent\":\"#ffffff\"}]}"},
    {"cor #xxxxxx", "{\"format\":2,\"layers\":[{\"type\":\"world3d\",\"geometry\":\"tunnel\",\"colors\":[\"#xxxxxx\",\"#222222\",\"#333333\",\"#444444\"],\"accent\":\"#ffffff\"}]}"},
    {"cor com nome", "{\"format\":2,\"layers\":[{\"type\":\"world3d\",\"geometry\":\"tunnel\",\"colors\":[\"accent\",\"#222222\",\"#333333\",\"#444444\"],\"accent\":\"#ffffff\"}]}"},
    {"sem accent", "{\"format\":2,\"layers\":[{\"type\":\"world3d\",\"geometry\":\"tunnel\",\"colors\":[\"#111111\",\"#222222\",\"#333333\",\"#444444\"]}]}"},
    {"period 999", grade(cam, "").replace(grade(cam).find("\"period\":4000"), 13, "\"period\":999")},
    {"period 60001", grade(cam, "").replace(grade(cam).find("\"period\":4000"), 13, "\"period\":60001")},
    {"world3d na segunda camada", "{\"format\":2,\"layers\":[{\"type\":\"rect\",\"w\":10,\"h\":10},{\"type\":\"world3d\",\"geometry\":\"tunnel\"," + cores + "}]}"},
    {"dois world3d", "{\"format\":2,\"layers\":[{\"type\":\"world3d\",\"geometry\":\"tunnel\"," + cores + "},{\"type\":\"world3d\",\"geometry\":\"tunnel\"," + cores + "}]}"},
    {"world3d no formato 1", tunel("", 1)},
    {"overlay no formato 1", R"({"format":1,"layers":[{"type":"text","font":"bebas","size":40,"overlay":true,"text":"{HH}"}]})"},
    {"overlay em fonte pixel", R"({"format":2,"layers":[{"type":"text","font":"pixel","overlay":true,"text":"{HH}"}]})"},
    {"overlay em retangulo", R"({"format":2,"layers":[{"type":"rect","w":10,"h":10,"overlay":true}]})"},
    {"tipo raycast", R"({"format":2,"layers":[{"type":"raycast"}]})"},
    {"tipo speed_tunnel", R"({"format":2,"layers":[{"type":"speed_tunnel"}]})"},
    {"tipo shots", R"({"format":2,"layers":[{"type":"shots"}]})"},
  };
  int recusas = 0;
  for (auto &r : R) {
    err[0] = 0;
    bool ok = facePackLoad((char *)r.js.c_str(), r.js.size(), err, sizeof err);
    facePackUnload();
    if (ok || !err[0]) { printf("world3d recusa \"%s\" FALHOU (%s)\n", r.nome, ok ? "aceito" : "sem mensagem"); fails++; }
    else recusas++;
  }
  // a cena e o cache do corredor somem com o pacote: carregar e descarregar 200 vezes não pode deixar memória presa
  for (int k = 0; k < 200; k++) {
    std::string js = k & 1 ? grade(cam) : tunel("");
    if (facePackLoad((char *)js.c_str(), js.size(), err, sizeof err)) facePackUnload();
  }
  delete a;
  delete b;
  if (!fails) printf("world3d: ok (cores, quadrado, camera, incremental igual ao completo em 3 cenas x 40 quadros, overlay, %d entradas recusadas)\n", recusas);
  return fails;
}

// Movimento ("motion", formato 2, só em sprite): a posição segue os pontos, o desenho incremental fica igual ao
// completo, e entradas inseguras são recusadas
static int testMotion() {
  int fails = 0;
  auto pack = [](const std::string &mot, int fmt = 2, const char *tipo = "sprite", int n = 1) {
    std::string js = "{\"format\":" + std::to_string(fmt) + R"(,"sprites":{"s":{"pal":{"a":"#ffcc00"},"frames":[["aaaa","a..a","aaaa"],["....","aaaa","...."]],"fps":5}},"layers":[)";
    for (int i = 0; i < n; i++)
      js += std::string(i ? "," : "") + "{\"type\":\"" + tipo + "\",\"sprite\":\"s\",\"w\":10,\"h\":10,\"size\":4,\"motion\":" + mot + "}";
    return js + "]}";
  };
  const std::string ok = R"({"period":4000,"keys":[{"t":0,"x":10,"y":20},{"t":2000,"x":200,"y":150},{"t":4000,"x":10,"y":20}]})";
  char err[96];
  std::string js = pack(ok);
  if (!facePackLoad((char *)js.c_str(), js.size(), err, sizeof err)) { printf("motion valido FALHOU: %s\n", err); return 1; }
  Frame *inc = new Frame(), *fresh = new Frame();
  UiState s = base(14, 37, 0);
  s.theme = THEME_PACK;
  uiBegin(inc);
  facePackRender(s, true);
  int diff = 0;
  for (int k = 1; k <= 60; k++) {
    s.ms = k * 100;
    uiBegin(inc);
    facePackRender(s, false);
    uiBegin(fresh);
    facePackRender(s, true);
    if (memcmp(inc->px, fresh->px, sizeof inc->px)) diff++;
  }
  // em t = 1000 o sprite está no meio do caminho (105, 85), no quadro 1 (só a linha do meio): o pixel (105, 89) é dele
  s.ms = 1000;
  uiBegin(fresh);
  facePackRender(s, true);
  if (fresh->px[89 * 240 + 105] != uiRgb(255, 204, 0)) { printf("motion posicao FALHOU\n"); fails++; }
  facePackUnload();
  delete inc;
  delete fresh;
  if (diff) { printf("motion incremental FALHOU: %d de 60 quadros diferentes do completo\n", diff); fails++; }
  struct { const char *nome; std::string js; } R[] = {
    {"period 0", pack(R"({"period":0,"keys":[{"t":0,"x":0,"y":0},{"t":0,"x":0,"y":0}]})")},
    {"period 60001", pack(R"({"period":60001,"keys":[{"t":0,"x":0,"y":0},{"t":60001,"x":0,"y":0}]})")},
    {"7 pontos", pack(R"({"period":700,"keys":[{"t":0,"x":0,"y":0},{"t":100,"x":0,"y":0},{"t":200,"x":0,"y":0},{"t":300,"x":0,"y":0},{"t":400,"x":0,"y":0},{"t":500,"x":0,"y":0},{"t":700,"x":0,"y":0}]})")},
    {"1 ponto", pack(R"({"period":1000,"keys":[{"t":0,"x":0,"y":0}]})")},
    {"tempos fora de ordem", pack(R"({"period":1000,"keys":[{"t":0,"x":0,"y":0},{"t":600,"x":0,"y":0},{"t":500,"x":0,"y":0},{"t":1000,"x":0,"y":0}]})")},
    {"tempo repetido", pack(R"({"period":1000,"keys":[{"t":0,"x":0,"y":0},{"t":500,"x":0,"y":0},{"t":500,"x":9,"y":0},{"t":1000,"x":0,"y":0}]})")},
    {"primeiro t 5", pack(R"({"period":1000,"keys":[{"t":5,"x":0,"y":0},{"t":1000,"x":0,"y":0}]})")},
    {"sem ponto final", pack(R"({"period":1000,"keys":[{"t":0,"x":0,"y":0},{"t":900,"x":0,"y":0}]})")},
    {"x 481", pack(R"({"period":1000,"keys":[{"t":0,"x":481,"y":0},{"t":1000,"x":0,"y":0}]})")},
    {"y -481", pack(R"({"period":1000,"keys":[{"t":0,"x":0,"y":-481},{"t":1000,"x":0,"y":0}]})")},
    {"ponto sem y", pack(R"({"period":1000,"keys":[{"t":0,"x":0},{"t":1000,"x":0,"y":0}]})")},
    {"keys em texto", pack(R"({"period":1000,"keys":"abc"})")},
    {"motion em numero", pack("5")},
    {"motion em texto", pack(R"({"type":"text","text":"x"})", 2, "text")},
    {"motion no formato 1", pack(ok, 1)},
    {"13 camadas com motion", pack(ok, 2, "sprite", 13)},
  };
  int recusas = 0;
  for (auto &r : R) {
    err[0] = 0;
    bool aceito = facePackLoad((char *)r.js.c_str(), r.js.size(), err, sizeof err);
    facePackUnload();
    if (aceito || !err[0]) { printf("motion recusa \"%s\" FALHOU (%s)\n", r.nome, aceito ? "aceito" : "sem mensagem"); fails++; }
    else recusas++;
  }
  js = pack(ok, 2, "sprite", 12);
  if (!facePackLoad((char *)js.c_str(), js.size(), err, sizeof err)) { printf("motion 12 camadas FALHOU: %s\n", err); fails++; }
  facePackUnload();
  if (!fails) printf("motion: ok (posicao, incremental igual ao completo em 60 quadros, 12 camadas aceitas, %d entradas recusadas)\n", recusas);
  return fails;
}

// Oráculo dos sprites: pacotes aleatórios (semente fixa) com 1 a 3 sprites, quadros variados (ruído, vazio, repetido,
// só um canto, cheio), paletas de 1 a 15 letras e linhas mais curtas ou mais longas que a largura. Cada quadro de cada
// sprite é desenhado pelo motor (facePackRender) e comparado pixel a pixel com o JSON plotado direto, letra por letra.
// Pacote acima do teto cru precisa ser recusado; abaixo, aceito. Prova que a forma de guardar os sprites na RAM não muda
// o que aparece na tela.
class FastFrame : public Frame {  // o mesmo Frame, com retângulos preenchidos direto na memória (o oráculo desenha muito)
 public:
  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) override {
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y, x1 = x + w > 240 ? 240 : x + w, y1 = y + h > 240 ? 240 : y + h;
    for (int yy = y0; yy < y1; yy++)
      for (int xx = x0; xx < x1; xx++) px[yy * 240 + xx] = c;
  }
  void fillScreen(uint16_t c) override { fillRect(0, 0, 240, 240, c); }
};

static int testSpriteOracle(int nPacks, int maxFrames, int maxFps, int fmt = 1) {
  const long teto = fmt == 1 ? 6144 : 8192;  // teto cru do formato
  uint32_t seed = fmt == 1 ? 0x5EED2026u : 0x5EED2027u;  // o formato 1 com a mesma semente do #78
  auto rnd = [&](int n) { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return (int)(seed % (uint32_t)n); };
  static const char LETRAS[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  static const char VAZIO[] = ". #-";  // fora da paleta = transparente
  struct Spr { int w, h, nf, fps, n; char keys[16]; uint16_t pal[15]; std::vector<std::vector<std::string>> fr; };
  struct Lay { int s, x, y, size; };
  FastFrame *f = new FastFrame();
  std::vector<uint16_t> ref(240 * 240);
  int fails = 0, aceitos = 0, recusados = 0, quadros = 0;
  long somaRaw = 0, somaRam = 0;
  for (int pk = 0; pk < nPacks; pk++) {
    std::vector<Spr> S(1 + rnd(3));
    long raw = 0, nq = 0;
    for (Spr &s : S) {
      bool grande = rnd(3) == 0;
      s.w = grande ? 20 + rnd(13) : 1 + rnd(32);
      s.h = grande ? 20 + rnd(13) : 1 + rnd(32);
      s.nf = 1 + rnd(maxFrames);
      s.fps = 1 + rnd(maxFps);
      s.n = rnd(4) == 0 ? 1 + rnd(3) : 1 + rnd(15);  // paletas pequenas (1 e 2 bits por pixel) aparecem bastante
      std::string used;
      for (int i = 0; i < s.n; i++) {
        char c;
        do c = LETRAS[rnd(sizeof LETRAS - 1)]; while (used.find(c) != std::string::npos);
        used += c;
        s.keys[i] = c;
        s.pal[i] = uiRgb(rnd(256), rnd(256), rnd(256));
      }
      for (int k = 0; k < s.nf; k++) {
        std::vector<std::string> q;
        int modo = rnd(6);
        if (modo == 0 && k) { s.fr.push_back(s.fr[rnd(k)]); continue; }  // quadro repetido
        int cx0 = rnd(s.w), cy0 = rnd(s.h), cx1 = cx0 + rnd(s.w - cx0 + 1), cy1 = cy0 + rnd(s.h - cy0 + 1);  // só um canto
        int dens = rnd(101);
        for (int r = 0; r < s.h; r++) {
          std::string row;
          for (int c = 0; c < s.w; c++) {
            bool cheio = modo == 1 ? false : modo == 2 ? (c >= cx0 && c < cx1 && r >= cy0 && r < cy1 && rnd(100) < 80)
                       : modo == 3 ? true : rnd(100) < dens;
            row += cheio ? s.keys[rnd(s.n)] : VAZIO[rnd(sizeof VAZIO - 1)];
          }
          if (r && rnd(12) == 0) row = row.substr(0, rnd(s.w + 1));  // linha curta: o resto é transparente
          else if (r && rnd(12) == 0) row += std::string(1 + rnd(4), s.keys[0]);  // linha longa: o excesso é ignorado
          q.push_back(row);
        }
        s.fr.push_back(q);
      }
      raw += 4 + 15 * 2 + (long)s.nf * ((s.w * s.h + 1) / 2);
      nq += s.nf;
    }
    std::vector<Lay> L(S.size() + rnd(2));
    for (size_t i = 0; i < L.size(); i++) L[i] = {i < S.size() ? (int)i : rnd(S.size()), -20 + rnd(250), -20 + rnd(250), 1 + rnd(3)};
    uint16_t bg = uiRgb(rnd(256), rnd(256), rnd(256));
    // JSON
    char hex[8];
    auto cor = [&](uint16_t c) { snprintf(hex, sizeof hex, "#%02x%02x%02x", (c >> 11) << 3, ((c >> 5) & 63) << 2, (c & 31) << 3); return std::string(hex); };
    std::string js = "{\"format\":" + std::to_string(fmt) + ",\"bg\":\"" + cor(bg) + "\",\"sprites\":{";
    for (size_t i = 0; i < S.size(); i++) {
      Spr &s = S[i];
      js += (i ? ",\"s" : "\"s") + std::to_string(i) + "\":{\"pal\":{";
      for (int c = 0; c < s.n; c++) js += (c ? ",\"" : "\"") + std::string(1, s.keys[c]) + "\":\"" + cor(s.pal[c]) + "\"";
      js += "},\"fps\":" + std::to_string(s.fps);
      bool soPx = s.nf == 1 && rnd(2);
      js += soPx ? ",\"px\":" : ",\"frames\":[";
      for (int k = 0; k < s.nf; k++) {
        js += k ? ",[" : "[";
        for (int r = 0; r < s.h; r++) js += (r ? ",\"" : "\"") + s.fr[k][r] + "\"";
        js += "]";
      }
      js += soPx ? "}" : "]}";
    }
    js += "},\"layers\":[";
    for (size_t i = 0; i < L.size(); i++)
      js += std::string(i ? "," : "") + "{\"type\":\"sprite\",\"sprite\":\"s" + std::to_string(L[i].s) + "\",\"x\":" + std::to_string(L[i].x) +
            ",\"y\":" + std::to_string(L[i].y) + ",\"size\":" + std::to_string(L[i].size) + "}";
    js += "]}";
    char err[96] = "";
    bool ok = facePackLoad((char *)js.c_str(), js.size(), err, sizeof err);
    if (ok != (raw <= teto)) {
      if (fails++ < 5) printf("oraculo: pacote %d %s com %ld B crus (%s)\n", pk, ok ? "aceito" : "recusado", raw, err);
      if (ok) facePackUnload();
      continue;
    }
    if (!ok) { recusados++; continue; }
    aceitos++;
    // pior caso do formato na RAM: a conta crua + 6 B por quadro (ver sprEncode)
    long ram = facePackSpriteBytes();
    somaRaw += raw;
    somaRam += ram;
    if (ram > raw + 6 * nq) {
      if (fails++ < 5) printf("oraculo: pacote %d com %ld B na RAM, acima de %ld crus + 6 x %ld quadros\n", pk, ram, raw, nq);
      facePackUnload();
      continue;
    }
    // cada quadro de cada sprite, no instante em que o sprite s mostra o quadro k
    bool erro = false;
    for (size_t si = 0; si < S.size() && !erro; si++)
      for (int k = 0; k < S[si].nf && !erro; k++) {
        uint32_t ms = (uint32_t)((k * 1000 + S[si].fps - 1) / S[si].fps) + 1000 * rnd(3);
        UiState st = base(14, 37, 24);
        st.theme = THEME_PACK;
        st.ms = ms;
        uiBegin(f);
        facePackRender(st, true);
        for (uint16_t &p : ref) p = bg;
        for (const Lay &l : L) {
          const Spr &s = S[l.s];
          int q = s.nf > 1 ? (int)((uint64_t)ms * s.fps / 1000 % s.nf) : 0;
          for (int r = 0; r < s.h; r++) {
            const std::string &row = s.fr[q][r];
            for (int c = 0; c < s.w && c < (int)row.size(); c++) {
              int v = 0;
              for (int i = 0; i < s.n; i++) if (s.keys[i] == row[c]) v = i + 1;
              if (!v) continue;
              for (int yy = l.y + r * l.size; yy < l.y + (r + 1) * l.size; yy++)
                for (int xx = l.x + c * l.size; xx < l.x + (c + 1) * l.size; xx++)
                  if (xx >= 0 && yy >= 0 && xx < 240 && yy < 240) ref[yy * 240 + xx] = s.pal[v - 1];
            }
          }
        }
        quadros++;
        for (int i = 0; i < 240 * 240 && !erro; i++)
          if (f->px[i] != ref[i]) {
            erro = true;
            if (fails < 5) printf("oraculo: pacote %d, sprite %d, quadro %d: pixel (%d,%d) %04x, esperado %04x\n", pk, (int)si, k, i % 240, i / 240, f->px[i], ref[i]);
          }
      }
    fails += erro;
    facePackUnload();
  }
  delete f;
  printf("oraculo dos sprites (formato %d, ate %d quadros): %d pacotes (%d aceitos, %d recusados pelo teto), %d quadros comparados pixel a pixel, %d falhas; sprites na RAM %ld B contra %ld B crus\n",
         fmt, maxFrames, nPacks, aceitos, recusados, quadros, fails, somaRam, somaRaw);
  return fails;
}

// Mundo: custo da virada do minuto (o mapa inteiro é refeito) contra um segundo comum (só o ponto e os segundos)
// e contra um desenho completo do LCD retrô, que já roda no aparelho sem travar o relógio. Tempo de CPU no PC.
static void checkWorldCost() {
  using clk = std::chrono::steady_clock;
  auto us = [](clk::time_point t0) { return std::chrono::duration<double, std::micro>(clk::now() - t0).count(); };
  Frame *f = new Frame();
  const int N = 200;
  double tMin = 0, tSec = 0, tLcd = 0;
  for (int i = 0; i < N; i++) {
    UiState s = base(14, 37, 58);
    s.theme = THEME_WORLD;
    uiBegin(f);
    uiRenderClock(s);
    s.now.tm_sec = 59;
    auto t0 = clk::now();
    uiRenderClock(s);
    tSec += us(t0);
    s.now.tm_sec = 0;
    s.now.tm_min = 38;
    t0 = clk::now();
    uiRenderClock(s);
    tMin += us(t0);
    UiState l = base(14, 37, 58);
    l.theme = THEME_LCD;
    t0 = clk::now();
    uiBegin(f);
    uiRenderClock(l);
    tLcd += us(t0);
  }
  printf("mundo: virada do minuto (mapa inteiro) %ld us, segundo comum %ld us, desenho completo do LCD retro %ld us (CPU no PC, media de %d)\n", (long)(tMin / N), (long)(tSec / N), (long)(tLcd / N), N);
  delete f;
}

static int checkSetupQr() {
  int fails = 0;
  char payload[SETUP_WIFI_TEXT];
  uint8_t qr[SETUP_QR_BYTES];
  const char *ssid = "Small;TV:1", *pass = "ab\\cd,\"12";
  bool valid = setupWifiPayload(payload, sizeof payload, ssid, pass);
  fails += !valid || strcmp(payload, "WIFI:T:WPA;S:Small\\;TV\\:1;P:ab\\\\cd\\,\\\"12;;") != 0;
  fails += !setupQrEncode(payload, qr);
  char tiny[12];
  fails += setupWifiPayload(tiny, sizeof tiny, ssid, pass) || tiny[0];
  fails += setupWifiPayload(payload, sizeof payload, "", pass);
  fails += setupWifiPayload(payload, sizeof payload, "SmallTV", "short");
  char longSsid[34]; memset(longSsid, 's', sizeof longSsid); longSsid[33] = 0;
  fails += setupWifiPayload(payload, sizeof payload, longSsid, pass);
  memset(payload, 'X', sizeof payload - 1); payload[sizeof payload - 1] = 0;
  fails += setupQrEncode(payload, qr);
  const uint8_t lang = uiLang;
  Frame *f = new Frame();
  for (int l = 0; l < 3; l++) {
    uiSetLang(l);
    for (int r = 0; r < 2; r++) for (int c = 0; c < 2; c++) {
      uiBegin(f);
      uiWifiSetup(r ? "SmallTV-Recuperacao" : "SmallTV-Pomodoro", "AbCd2345", "192.168.4.1", c, r);
      char name[48]; snprintf(name, sizeof name, "setup_%s_%s_%d", r ? "recovery" : "wifi", c ? "portal" : "connect", l);
      save(*f, name);
    }
  }
  uiBegin(f);
  uiWifiSetup(ssid, pass, "192.168.4.1", false, false);
  save(*f, "setup_escaped");
  delete f;
  uiSetLang(lang);
  printf("QR de configuracao e recuperacao: %s (escape, limites e 3 idiomas)\n", fails ? "FALHOU" : "ok");
  return fails;
}
#include "out/tint_test.inc"

int main() {
  if (const char *l = getenv("SIM_LANG")) uiSetLang(atoi(l));  // 1 = inglês, 2 = espanhol (as prévias oficiais ficam em português)
  // ---- relógio: temas ----
  UiState s = base(14, 37, 25);
  s.theme = THEME_MATRIX;              shot("01_matriz", s, false);
  { UiState t = s; t.weatherOn = false; t.today = 8; t.accent = uiRgb(255, 176, 60); shot("02_matriz_sem_clima_meta", t, false); }
  s.theme = THEME_TOMATO;              shot("03_tomate_dia", s, false);
  { UiState t = base(23, 41, 0); t.theme = THEME_TOMATO; t.wx.code = 0; t.wx.day = false; t.wx.t = 17; shot("04_tomate_noite", t, false); }
  { UiState t = base(17, 52, 0); t.theme = THEME_TOMATO; t.wx.code = 63; t.wx.t = 19; shot("05_tomate_chuva_entardecer", t, false); }
  { UiState t = base(10, 9, 0); t.theme = THEME_TOMATO; t.wx.code = 71; t.wx.t = -2; shot("05b_tomate_neve", t, false); }
  { UiState t = base(13, 15, 0); t.theme = THEME_TOMATO; t.wx.code = 0; t.wx.t = 33; shot("05c_tomate_calor", t, false); }
  { UiState t = base(15, 40, 0); t.theme = THEME_TOMATO; t.wx.code = 1; t.today = 8; shot("05d_tomate_meta", t, false); }
  { UiState t = base(10, 9, 0); t.theme = THEME_ANALOG; shot("06_analogico", t, false); }
  { UiState t = base(8, 5, 0); t.theme = THEME_WEATHER; t.wx.code = 61; t.wx.t = 18.2f; shot("07_clima_chuva", t, false); }
  { UiState t = base(13, 0, 0); t.theme = THEME_WEATHER; t.wx.code = 0; t.wx.t = 31; t.wx.hum = 38; shot("08_clima_sol", t, false); }
  // °F (o firmware converte em makeState: aqui os valores já vêm em °F) e chuva chegando
  auto fahr = [](UiState t) {
    t.fahr = true;
    t.wx.t = t.wx.t * 9 / 5 + 32; t.wx.tmin = t.wx.tmin * 9 / 5 + 32; t.wx.tmax = t.wx.tmax * 9 / 5 + 32;
    for (UiDay &d : t.wx.d) { d.tmin = d.tmin * 9 / 5 + 32; d.tmax = d.tmax * 9 / 5 + 32; }
    return t;
  };
  { UiState t = base(13, 0, 0); t.theme = THEME_WEATHER; t.wx.code = 0; t.wx.t = 40; t.wx.tmax = 41; shot("08b_clima_fahrenheit_calor", fahr(t), false); }
  { UiState t = base(16, 20, 0); t.theme = THEME_WEATHER; t.wx.code = 3; t.wx.rainIn = 15; shot("08c_clima_chuva_chegando", t, false); }
  { UiState t = base(16, 20, 0); t.theme = THEME_TOMATO; t.wx.code = 3; t.wx.rainIn = 30; shot("05e_tomate_chuva_chegando", t, false); }
  { UiState t = base(18, 26, 43); t.theme = THEME_LCD; t.wx.t = 39; shot("16b_lcd_fahrenheit", fahr(t), false); }
  { UiState t = base(9, 0, 0); t.theme = THEME_FORECAST_LCD; t.wx.d[0].tmax = 41; shot("16c_previsao_lcd_fahrenheit", fahr(t), false); }
  { UiState t = base(9, 0, 0); t.theme = THEME_FORECAST_CARDS; t.wx.d[0].tmax = 41; shot("16d_previsao_cartoes_fahrenheit", fahr(t), false); }

  { UiState t = base(14, 37, 24); t.theme = THEME_NEON; shot("14_neon", t, false); }
  { UiState t = base(9, 5, 0); t.theme = THEME_FLIP; shot("15_flip", t, false); }
  { UiState t = base(18, 26, 43); t.theme = THEME_LCD; shot("16_lcd", t, false); }
  { UiState t = base(14, 31, 0); t.theme = THEME_WORDS; shot("17_palavras_meia", t, false); }
  { UiState t = base(2, 46, 0); t.theme = THEME_WORDS; shot("18_palavras_para", t, false); }
  { UiState t = base(11, 58, 0); t.theme = THEME_WORDS; shot("19_palavras_meiodia", t, false); }
  { UiState t = base(8, 23, 0); t.theme = THEME_WORDS; shot("20_palavras_manha", t, false); }
  // Mundo: dia e noite em Florianópolis; o mesmo instante (17:37 UTC) em Tóquio e em Londres (12 h, com foco e chuva)
  { UiState t = base(14, 37, 24); t.theme = THEME_WORLD; shot("21_mundo_dia", t, false); }
  { UiState t = base(23, 10, 41); t.theme = THEME_WORLD; t.wx.rainIn = 0; shot("21b_mundo_noite", t, false); }
  { UiState t = base(2, 37, 24); t.now.tm_yday = 293; t.now.tm_mday = 21; t.now.tm_wday = 3; t.theme = THEME_WORLD;
    t.utcOff = 9 * 3600; t.lat10 = 357; t.lon10 = 1397; t.city = "Tokyo"; shot("21c_mundo_toquio", t, false); }
  { UiState t = base(18, 37, 24); t.theme = THEME_WORLD; t.utcOff = 3600; t.lat10 = 515; t.lon10 = -1; t.city = "London"; t.hour12 = true;
    t.running = true; t.wx.rainIn = 15; shot("21d_mundo_londres_12h", t, false); }
  // Mundo relógio: os mesmos quatro cenários, com o nome da cidade (código FLO, TOK e LON)
  { UiState t = base(14, 37, 24); t.theme = THEME_WORLD_WATCH; shot("22_mundo_relogio_dia", t, false); }
  { UiState t = base(23, 10, 41); t.theme = THEME_WORLD_WATCH; t.wx.rainIn = 0; shot("22b_mundo_relogio_noite", t, false); }
  { UiState t = base(2, 37, 24); t.now.tm_yday = 293; t.now.tm_mday = 21; t.now.tm_wday = 3; t.theme = THEME_WORLD_WATCH;
    t.utcOff = 9 * 3600; t.lat10 = 357; t.lon10 = 1397; t.city = "Tokyo"; shot("22c_mundo_relogio_toquio", t, false); }
  { UiState t = base(18, 37, 24); t.theme = THEME_WORLD_WATCH; t.utcOff = 3600; t.lat10 = 515; t.lon10 = -1; t.city = "London"; t.hour12 = true;
    t.running = true; t.wx.rainIn = 15; shot("22d_mundo_relogio_londres_12h", t, false); }

  // ---- animação da virada do Flip: 14:37 -> 14:38 ----
  {
    Frame *f = new Frame();
    uiBegin(f);
    UiState t = base(14, 37, 58);
    t.theme = THEME_FLIP;
    uiRenderClock(t);
    animFrame = f;
    uiFrameHook = saveAnimFrame;
    t.now.tm_min = 38;
    uiRenderClock(t);
    uiFrameHook = nullptr;
    delete f;
  }

  // ---- prévias da galeria do painel (docs/faces/face-N.png) ----
  for (int t = 0; t < THEME_COUNT; t++) {
    UiState g = t == THEME_ANALOG ? base(10, 9, 30) : base(14, 37, 24);
    g.theme = t;
    char n[16];
    snprintf(n, sizeof n, "face-%d", t);
    shot(n, g, false);
  }

  // ---- Pomodoro ----
  UiState p = base(14, 37, 25);
  p.phase = PH_WORK; p.running = true; p.totalMs = 25 * 60000; p.remainingMs = 10 * 60000 + 12000; p.cycleDone = 2;
  shot("09_pomo_foco", p, true);
  { UiState t = p; t.phase = PH_SHORT; t.totalMs = 5 * 60000; t.remainingMs = 3 * 60000 + 40000; t.running = false; t.now.tm_sec = 0; shot("10_pomo_pausa_pausado", t, true); }
  { UiState t = p; t.phase = PH_LONG; t.totalMs = 15 * 60000; t.remainingMs = 14 * 60000; t.cycleDone = 4; shot("11_pomo_pausa_longa", t, true); }
  { UiState t = p; t.phase = PH_SHORT; t.totalMs = t.remainingMs = 5 * 60000; t.cycleDone = 3; t.celebrate = true; t.ms = 3 * 160; shot("11b_pomo_fim", t, true); }
  for (int a = 0; a < ACT_COUNT; a++) {  // uma tela por atividade do foco
    UiState t = p;
    t.activity = a;
    char n[32];
    snprintf(n, sizeof n, "11c_pomo_atividade_%d", a);
    shot(n, t, true);
  }

  // ---- telas avulsas ----
  { Frame *f = new Frame(); uiBegin(f); uiMessage("Configure o Wi-Fi", "SmallTV-Pomodoro", "Conecte nessa rede e abra", "http://192.168.4.1"); save(*f, "12_portal"); delete f; }
  { Frame *f = new Frame(); uiBegin(f);  // tela de início: etapa do Wi-Fi, da hora e pronto
    uiMessage("SmallTV Pomodoro", "Conectando Wi-Fi", "v1.0", ""); uiBootProgress(300, 0); save(*f, "12b_inicio_wifi");
    uiMessage("Acertando a hora", "smalltv-1a2b.local", "192.168.1.108", ""); uiBootProgress(900, 1); save(*f, "12c_inicio_hora");
    uiMessage("Conectado", "smalltv-1a2b.local", "192.168.1.108", ""); uiBootProgress(900, 2); save(*f, "12d_inicio_pronto"); delete f; }
  { Frame *f = new Frame(); uiBegin(f);  // sem hora: o mostrador com o indicador "sincronizando"
    UiState t = base(14, 58, 50); t.timeOk = false; t.theme = 0; uiRenderClock(t); uiSyncHint(400); save(*f, "12e_sem_hora"); delete f; }
  { Frame *f = new Frame(); uiBegin(f); uiColorTest(); save(*f, "13_teste_cores"); delete f; }
  { Frame *f = new Frame(); uiBegin(f); uiVcomTest(0x20, true); uiVcomTest(0x1C, false); save(*f, "13b_anti_fantasma"); delete f; }

  assetSheet();

  // ---- redesenho incremental ----
  int fails = 0;
  fails += checkSetupQr();
  for (int t = 0; t < THEME_COUNT; t++) {
    UiState st = base(14, 58, 50);
    st.theme = t;
    // nome do arquivo a partir do registro (faces_list.h), sem acentos nem espaços
    char slug[32];
    int k = 0;
    for (const unsigned char *p = (const unsigned char *)uiFaceName(t); *p && k < 30; p++) {
      if (*p == 0xC3 && p[1]) {  // á â ã à é ê í ó ô õ ú ç em UTF-8
        unsigned char c = *++p;
        slug[k++] = c >= 0xA0 && c <= 0xA5 ? 'a' : c == 0xA7 ? 'c' : c >= 0xA8 && c <= 0xAB ? 'e' : c >= 0xAC && c <= 0xAF ? 'i' : c >= 0xB2 && c <= 0xB6 ? 'o' : c >= 0xB9 && c <= 0xBC ? 'u' : '_';
      } else slug[k++] = *p == ' ' ? '_' : (*p >= 'A' && *p <= 'Z' ? *p + 32 : *p);
    }
    slug[k] = 0;
    fails += checkIncremental(slug, st, false) != 0;
  }
  fails += checkIncremental("pomodoro", p, true) != 0;
  fails += checkTransitions(p);
  fails += checkSlide();
  fails += checkLcdCurtain();
  checkWorldCost();
  fails += testPackages(p);
  fails += testSpriteLimits();
  fails += testSpriteOracle(2000, 8, 10);
  fails += testSpriteOracle(2000, 16, 10, 2);
  fails += testWorld3D();
  fails += testMotion();
  fails += TintTest::run();
  return fails;
}
