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
    static char buf[16384];
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
  auto pack = [&](const std::string &spr) {
    return "{\"format\":1,\"sprites\":{" + spr + "},\"layers\":[{\"type\":\"rect\",\"w\":10,\"h\":10}]}";
  };
  auto sp = [&](const char *name, const std::string &fr, const char *extra = "") {
    return std::string("\"") + name + "\":{\"pal\":{\"a\":\"#fff\",\"b\":\"#f00\"},\"frames\":" + fr + extra + "}";
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
  };
  for (auto &t : T) {
    char err[96] = "";
    bool ok = facePackLoad((char *)t.json.c_str(), t.json.size(), err, sizeof err);
    if (ok) facePackUnload();
    if (ok != t.ok) { printf("sprite %-20s FALHOU (%s)\n", t.nome, ok ? "aceito" : err); fails++; }
    else printf("sprite %-20s ok%s%s\n",t.nome, ok ? "" : ": ", ok ? "" : err);
  }
  return fails;
}

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
    t.utcOff = 9 * 3600; t.lat10 = 357; t.lon10 = 1397; shot("21c_mundo_toquio", t, false); }
  { UiState t = base(18, 37, 24); t.theme = THEME_WORLD; t.utcOff = 3600; t.lat10 = 515; t.lon10 = -1; t.hour12 = true;
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
  fails += testPackages(p);
  fails += testSpriteLimits();
  return fails;
}
