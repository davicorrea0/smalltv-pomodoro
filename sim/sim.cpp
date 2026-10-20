// Simulador das telas do SmallTV: roda o ui.cpp do firmware num framebuffer 240x240
// e grava cada tela em out/<nome>.raw (RGB565). png.js converte para PNG.
//
// Também testa o redesenho incremental: desenha uma sequência de estados (segundos
// passando, virada de minuto, clima novo, foco concluído) e compara o resultado final
// com um desenho feito do zero. Qualquer pixel diferente = sobra de desenho na tela.

#include <stdio.h>
#include <string.h>
#include "../smalltv-pomodoro/ui.cpp"

class Frame : public Adafruit_GFX {
 public:
  uint16_t px[240 * 240];
  Frame() : Adafruit_GFX(240, 240) { memset(px, 0, sizeof px); }
  void drawPixel(int16_t x, int16_t y, uint16_t c) override {
    if (x < 0 || y < 0 || x >= 240 || y >= 240) return;
    px[y * 240 + x] = c;
  }
};

static void save(const Frame &f, const char *name) {
  char path[128];
  snprintf(path, sizeof path, "out/%s.raw", name);
  FILE *fp = fopen(path, "wb");
  fwrite(f.px, 2, 240 * 240, fp);
  fclose(fp);
}

static UiState base(int h, int m, int s) {
  UiState st;
  memset(&st, 0, sizeof st);
  st.timeOk = true;
  st.now.tm_year = 126; st.now.tm_mon = 9; st.now.tm_mday = 20; st.now.tm_wday = 2;
  st.now.tm_hour = h; st.now.tm_min = m; st.now.tm_sec = s;
  st.now.tm_isdst = -1;
  st.longEvery = 4;
  st.dailyGoal = 8;
  st.today = 3;
  st.weatherOn = true;
  st.wx = {true, 23.4f, 14.2f, 27.1f, 44, 2, true, 1};
  st.city = "Florianopolis";
  st.accent = uiRgb(242, 242, 242);
  st.ip = "192.168.1.101";
  return st;
}

static Frame *F;

static void shot(const char *name, const UiState &s, bool pomo) {
  Frame *f = new Frame();
  uiBegin(f);
  if (pomo) uiRenderPomodoro(s);
  else uiRenderClock(s);
  save(*f, name);
  delete f;
}

// Sequência incremental vs. desenho do zero
static int checkIncremental(const char *name, UiState s, bool pomo) {
  Frame *inc = new Frame();
  uiBegin(inc);
  for (int step = 0; step < 140; step++) {
    if (pomo) {
      s.remainingMs = s.remainingMs > 1000 ? s.remainingMs - 1000 : 0;
      if (step == 70) s.running = !s.running;      // pausa no meio
      if (step == 90) s.running = !s.running;
      if (step == 100) { s.cycleDone++; }          // ciclo avança
    } else {
      if (++s.now.tm_sec >= 60) { s.now.tm_sec = 0; if (++s.now.tm_min >= 60) { s.now.tm_min = 0; s.now.tm_hour = (s.now.tm_hour + 1) % 24; } }
      if (step == 50) { s.wx.t = 9.6f; s.wx.code = 63; s.wx.tmin = 7; s.wx.tmax = 12; s.wx.version++; }
      if (step == 80) s.today++;
      if (step == 110) { s.weatherOn = false; }
    }
    if (pomo) uiRenderPomodoro(s);
    else uiRenderClock(s);
  }
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
  printf("%-22s %s (%d pixels diferentes)", name, diff ? "FALHOU" : "ok", diff);
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

int main() {
  // ---- relógio: temas ----
  UiState s = base(14, 37, 25);
  s.theme = THEME_MATRIX;              shot("01_matriz", s, false);
  { UiState t = s; t.weatherOn = false; t.today = 8; t.accent = uiRgb(255, 176, 60); shot("02_matriz_sem_clima_meta", t, false); }
  s.theme = THEME_TOMATO;              shot("03_tomate_dia", s, false);
  { UiState t = base(23, 41, 0); t.theme = THEME_TOMATO; t.wx.code = 0; t.wx.day = false; t.wx.t = 17; shot("04_tomate_noite", t, false); }
  { UiState t = base(17, 52, 0); t.theme = THEME_TOMATO; t.wx.code = 63; t.wx.t = 19; shot("05_tomate_chuva_entardecer", t, false); }
  { UiState t = base(10, 9, 0); t.theme = THEME_ANALOG; shot("06_analogico", t, false); }
  { UiState t = base(8, 5, 0); t.theme = THEME_WEATHER; t.wx.code = 61; t.wx.t = 18.2f; shot("07_clima_chuva", t, false); }
  { UiState t = base(13, 0, 0); t.theme = THEME_WEATHER; t.wx.code = 0; t.wx.t = 31; t.wx.hum = 38; shot("08_clima_sol", t, false); }

  { UiState t = base(14, 37, 24); t.theme = THEME_NEON; shot("14_neon", t, false); }
  { UiState t = base(9, 5, 0); t.theme = THEME_FLIP; shot("15_flip", t, false); }
  { UiState t = base(18, 26, 43); t.theme = THEME_LCD; shot("16_lcd", t, false); }
  { UiState t = base(14, 31, 0); t.theme = THEME_WORDS; shot("17_palavras_meia", t, false); }
  { UiState t = base(2, 46, 0); t.theme = THEME_WORDS; shot("18_palavras_para", t, false); }
  { UiState t = base(11, 58, 0); t.theme = THEME_WORDS; shot("19_palavras_meiodia", t, false); }
  { UiState t = base(8, 23, 0); t.theme = THEME_WORDS; shot("20_palavras_manha", t, false); }

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

  // ---- telas avulsas ----
  { Frame *f = new Frame(); uiBegin(f); uiMessage("Configure o Wi-Fi", "SmallTV-Pomodoro", "Conecte nessa rede e abra", "http://192.168.4.1"); save(*f, "12_portal"); delete f; }
  { Frame *f = new Frame(); uiBegin(f); uiColorTest(); save(*f, "13_teste_cores"); delete f; }

  // ---- redesenho incremental ----
  int fails = 0;
  for (int t = 0; t < THEME_COUNT; t++) {
    UiState st = base(14, 58, 50);
    st.theme = t;
    const char *names[] = {"matriz", "tomate", "analogico", "clima", "neon", "flip", "lcd", "palavras"};
    fails += checkIncremental(names[t], st, false) != 0;
  }
  fails += checkIncremental("pomodoro", p, true) != 0;
  return fails;
}
