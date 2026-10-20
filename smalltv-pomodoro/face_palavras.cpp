// Mostrador Palavras: a hora por extenso.
// Registrado em faces_list.h; usa o kit de desenho de ui_kit.h.
#include "ui_kit.h"
#include "assets.h"

// ---------- tema 7: PALAVRAS ----------
// A hora por extenso ("SAO DUAS E MEIA DA TARDE"), arredondada a 5 minutos.
// Acentos: '\x01' = Ê, '\x02' = Ã, '\x03' = É (a fonte embutida só tem ASCII; o sinal é desenhado).
struct Tok { const char *s; uint16_t c; };
static const char *HOURW[12] = {"", "UMA", "DUAS", "TR\x01S", "QUATRO", "CINCO", "SEIS", "SETE", "OITO", "NOVE", "DEZ", "ONZE"};
static const char *const MIN_E[7][4] = {{}, {"E", "CINCO"}, {"E", "DEZ"}, {"E", "QUINZE"}, {"E", "VINTE"}, {"E", "VINTE", "E", "CINCO"}, {"E", "MEIA"}};
static const char *const MIN_PARA[5][3] = {{"VINTE", "E", "CINCO"}, {"VINTE"}, {"QUINZE"}, {"DEZ"}, {"CINCO"}};

static void wordText(int x, int y, uint8_t sz, uint16_t c, const char *str) {
  for (const char *p = str; *p; p++, x += 6 * sz) {
    char ch = *p, base = ch == '\x01' || ch == '\x03' ? 'E' : ch == '\x02' ? 'A' : ch;
    G->drawChar(x, y, base, c, c, sz);
    float u = sz;
    if (ch == '\x01') {  // circunflexo
      thickLine(x + 0.8f * u, y - 2, x + 2.5f * u, y - 2 - 1.6f * u, 2, c);
      thickLine(x + 2.5f * u, y - 2 - 1.6f * u, x + 4.2f * u, y - 2, 2, c);
    } else if (ch == '\x02') {  // til
      thickLine(x + 0.5f * u, y - 3, x + 1.6f * u, y - 2 - 1.5f * u, 2, c);
      thickLine(x + 1.6f * u, y - 2 - 1.5f * u, x + 3.2f * u, y - 3, 2, c);
      thickLine(x + 3.2f * u, y - 3, x + 4.4f * u, y - 2 - 1.5f * u, 2, c);
    } else if (ch == '\x03') {  // agudo
      thickLine(x + 2.0f * u, y - 2, x + 3.4f * u, y - 2 - 1.8f * u, 2, c);
    }
  }
}

static int buildPhrase(const UiState &s, Tok *t, uint16_t hourC) {
  int n = 0;
  int h = s.now.tm_hour, r = (s.now.tm_min + 2) / 5 * 5;
  if (r == 60) { r = 0; h = (h + 1) % 24; }
  bool para = r >= 35;
  int hh = para ? (h + 1) % 24 : h;
  const char *hw = hh == 0 ? "MEIA-NOITE" : hh == 12 ? "MEIO-DIA" : HOURW[hh % 12];
  bool singular = hh == 0 || hh == 12 || hh % 12 == 1;
  if (!para) {
    t[n++] = {singular ? "\x03" : "S\x02O", MUTED};
    t[n++] = {hw, hourC};
    if (r == 0) {
      if (hh != 0 && hh != 12) t[n++] = {"EM PONTO", MUTED};
    } else {
      for (int i = 0; i < 4 && MIN_E[r / 5][i]; i++) t[n++] = {MIN_E[r / 5][i], i % 2 ? TEXT : MUTED};
    }
  } else {
    for (int i = 0; i < 3 && MIN_PARA[(r - 35) / 5][i]; i++)
      t[n++] = {MIN_PARA[(r - 35) / 5][i], strcmp(MIN_PARA[(r - 35) / 5][i], "E") ? TEXT : MUTED};
    t[n++] = {"PARA", MUTED};
    t[n++] = {hh == 12 ? "O" : hh == 0 || hh % 12 == 1 ? "A" : "AS", MUTED};
    t[n++] = {hw, hourC};
  }
  if (hh != 0 && hh != 12) {
    t[n++] = {"DA", MUTED};
    t[n++] = {hh < 5 ? "MADRUGADA" : hh < 12 ? "MANH\x02" : hh < 18 ? "TARDE" : "NOITE", MUTED};
  }
  return n;
}

void themeWords(const UiState &s, bool full) {
  statusBar(s, s.ip ? s.ip : "", BG);
  if (!s.timeOk) return;
  Tok t[16];
  int n = buildPhrase(s, t, WORK);
  char key[40] = "";
  for (int i = 0; i < n; i++) {
    strncat(key, t[i].s, sizeof key - strlen(key) - 2);
    strncat(key, " ", sizeof key - strlen(key) - 1);
  }
  if (changed(cA, key)) {
    G->fillRect(0, 24, 240, 184, BG);
    // quebra em linhas de até 12 caracteres (tamanho 3 = 18 px por caractere)
    int lineStart[8], lineLen[8], lines = 0, len = 0;
    for (int i = 0; i < n && lines <= 8; i++) {
      int wl = strlen(t[i].s);
      if (lines == 0 || len + 1 + wl > 12) {
        if (lines == 8) break;
        lineStart[lines] = i;
        lineLen[lines++] = 0;
        len = wl;
      } else len += 1 + wl;
      lineLen[lines - 1]++;
    }
    const int LH = 36, total = lines * LH - 12;
    int y = 30 + (174 - total) / 2;
    for (int l = 0; l < lines; l++, y += LH) {
      int chars = 0;
      for (int k = 0; k < lineLen[l]; k++) chars += strlen(t[lineStart[l] + k].s) + (k ? 1 : 0);
      int x = 120 - (chars * 18 - 3) / 2;
      for (int k = 0; k < lineLen[l]; k++) {
        const Tok &tk = t[lineStart[l] + k];
        wordText(x, y, 3, tk.c, tk.s);
        x += (strlen(tk.s) + 1) * 18;
      }
    }
  }
  char hm[8];
  snprintf(hm, sizeof hm, "%02d:%02d", displayHour(s), s.now.tm_min);
  if (changed(cC, hm)) {
    G->fillRect(0, 212, 240, 18, BG);
    textC(120, 214, 2, uiRgb(90, 90, 100), hm);
  }
}

