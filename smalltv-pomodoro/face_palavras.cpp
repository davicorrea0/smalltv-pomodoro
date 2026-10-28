// Mostrador Palavras: a hora por extenso.
// Registrado em faces_list.h; usa o kit de desenho de ui_kit.h.
#include "ui_kit.h"
#include "assets.h"

// ---------- tema 7: PALAVRAS ----------
// A hora por extenso ("SAO DUAS E MEIA DA TARDE"), arredondada a 5 minutos.
// Em inglês ("IT'S HALF PAST TWO IN THE AFTERNOON") e espanhol ("SON LAS DOS Y MEDIA DE LA TARDE") conforme uiLang.
// Acentos: '\x01' = Ê, '\x02' = Ã, '\x03' = É, '\x04' = Ñ (a fonte embutida só tem ASCII; o sinal é desenhado).
// Os textos ficam na flash (não ocupam RAM): cada Tok aponta para um trecho dela, com o tamanho.
struct Tok { PGM_P s; uint8_t len; uint16_t c; };
// Tabelas: uma string só na flash, as entradas separadas por \0 (nth); cada entrada pode ter várias palavras
// separadas por espaço (putWords as divide em Toks).
static const char HOURW[] PROGMEM = "\0UMA\0DUAS\0TR\x01S\0QUATRO\0CINCO\0SEIS\0SETE\0OITO\0NOVE\0DEZ\0ONZE";
static const char MIN_E[] PROGMEM = "\0E CINCO\0E DEZ\0E QUINZE\0E VINTE\0E VINTE E CINCO\0E MEIA";
static const char MIN_PARA[] PROGMEM = "VINTE E CINCO\0VINTE\0QUINZE\0DEZ\0CINCO";
static const char HOUR_EN[] PROGMEM = "TWELVE\0ONE\0TWO\0THREE\0FOUR\0FIVE\0SIX\0SEVEN\0EIGHT\0NINE\0TEN\0ELEVEN";
static const char MIN_EN[] PROGMEM = "\0FIVE\0TEN\0QUARTER\0TWENTY\0TWENTY FIVE\0HALF";
static const char HOUR_ES[] PROGMEM = "DOCE\0UNA\0DOS\0TRES\0CUATRO\0CINCO\0SEIS\0SIETE\0OCHO\0NUEVE\0DIEZ\0ONCE";
static const char MIN_ES[] PROGMEM = "\0CINCO\0DIEZ\0CUARTO\0VEINTE\0VEINTICINCO\0MEDIA";

static PGM_P nth(PGM_P list, int i) {
  while (i-- > 0) list += strlen_P(list) + 1;
  return list;
}
static void put(Tok *t, int &n, PGM_P s, uint16_t c) { t[n++] = {s, (uint8_t)strlen_P(s), c}; }
// uma entrada de tabela, palavra a palavra; o "E" do português fica apagado, as outras em destaque
static void putWords(Tok *t, int &n, PGM_P s) {
  while (pgm_read_byte(s)) {
    uint8_t k = 0;
    while (pgm_read_byte(s + k) && pgm_read_byte(s + k) != ' ') k++;
    t[n++] = {s, k, k == 1 && pgm_read_byte(s) == 'E' ? MUTED : TEXT};
    s += k + (pgm_read_byte(s + k) == ' ');
  }
}

static void wordText(int x, int y, uint8_t sz, uint16_t c, PGM_P str, uint8_t len) {
  for (uint8_t i = 0; i < len; i++, x += 6 * sz) {
    char ch = pgm_read_byte(str + i), base = ch == '\x01' || ch == '\x03' ? 'E' : ch == '\x02' ? 'A' : ch == '\x04' ? 'N' : ch;
    G->drawChar(x, y, base, c, c, sz);
    float u = sz;
    if (ch == '\x01') {  // circunflexo
      thickLine(x + 0.8f * u, y - 2, x + 2.5f * u, y - 2 - 1.6f * u, 2, c);
      thickLine(x + 2.5f * u, y - 2 - 1.6f * u, x + 4.2f * u, y - 2, 2, c);
    } else if (ch == '\x02' || ch == '\x04') {  // til
      thickLine(x + 0.5f * u, y - 3, x + 1.6f * u, y - 2 - 1.5f * u, 2, c);
      thickLine(x + 1.6f * u, y - 2 - 1.5f * u, x + 3.2f * u, y - 3, 2, c);
      thickLine(x + 3.2f * u, y - 3, x + 4.4f * u, y - 2 - 1.5f * u, 2, c);
    } else if (ch == '\x03') {  // agudo
      thickLine(x + 2.0f * u, y - 2, x + 3.4f * u, y - 2 - 1.8f * u, 2, c);
    }
  }
}

// "IT'S TEN PAST NINE IN THE MORNING" / "IT'S QUARTER TO NOON"
static int phraseEn(int h, int r, Tok *t, uint16_t hourC) {
  int n = 0;
  bool to = r >= 35;
  int hh = to ? (h + 1) % 24 : h, m = to ? (60 - r) / 5 : r / 5;
  PGM_P hw = hh == 0 ? PSTR("MIDNIGHT") : hh == 12 ? PSTR("NOON") : nth(HOUR_EN, hh % 12);
  put(t, n, PSTR("IT'S"), MUTED);
  if (r) {
    putWords(t, n, nth(MIN_EN, m));
    put(t, n, to ? PSTR("TO") : PSTR("PAST"), MUTED);
  }
  put(t, n, hw, hourC);
  if (hh != 0 && hh != 12) {
    if (!r) put(t, n, PSTR("O'CLOCK"), MUTED);
    if (hh < 5) put(t, n, PSTR("AT"), MUTED), put(t, n, PSTR("NIGHT"), MUTED);
    else {
      put(t, n, PSTR("IN"), MUTED);
      put(t, n, PSTR("THE"), MUTED);
      put(t, n, hh < 12 ? PSTR("MORNING") : hh < 18 ? PSTR("AFTERNOON") : PSTR("EVENING"), MUTED);
    }
  }
  return n;
}

// "SON LAS DOS Y MEDIA DE LA TARDE" / "ES LA UNA MENOS CUARTO" / "ES MEDIODIA"
static int phraseEs(int h, int r, Tok *t, uint16_t hourC) {
  int n = 0;
  bool menos = r >= 35;
  int hh = menos ? (h + 1) % 24 : h, m = menos ? (60 - r) / 5 : r / 5;
  if (!r && (hh == 0 || hh == 12)) {
    put(t, n, PSTR("ES"), MUTED);
    put(t, n, hh ? PSTR("MEDIODIA") : PSTR("MEDIANOCHE"), hourC);
    return n;
  }
  bool una = hh % 12 == 1;
  put(t, n, una ? PSTR("ES") : PSTR("SON"), MUTED);
  put(t, n, una ? PSTR("LA") : PSTR("LAS"), MUTED);
  put(t, n, nth(HOUR_ES, hh % 12), hourC);
  if (!r) put(t, n, PSTR("EN PUNTO"), MUTED);
  else {
    put(t, n, menos ? PSTR("MENOS") : PSTR("Y"), MUTED);
    put(t, n, nth(MIN_ES, m), TEXT);
  }
  if (hh != 0 && hh != 12) {
    put(t, n, PSTR("DE"), MUTED);
    put(t, n, PSTR("LA"), MUTED);
    put(t, n, hh < 5 ? PSTR("MADRUGADA") : hh < 12 ? PSTR("MA\x04ANA") : hh < 19 ? PSTR("TARDE") : PSTR("NOCHE"), MUTED);
  }
  return n;
}

static int buildPhrase(const UiState &s, Tok *t, uint16_t hourC) {
  int n = 0;
  int h = s.now.tm_hour, r = (s.now.tm_min + 2) / 5 * 5;
  if (r == 60) { r = 0; h = (h + 1) % 24; }
  if (uiLang == 1) return phraseEn(h, r, t, hourC);
  if (uiLang == 2) return phraseEs(h, r, t, hourC);
  bool para = r >= 35;
  int hh = para ? (h + 1) % 24 : h;
  PGM_P hw = hh == 0 ? PSTR("MEIA-NOITE") : hh == 12 ? PSTR("MEIO-DIA") : nth(HOURW, hh % 12);
  bool singular = hh == 0 || hh == 12 || hh % 12 == 1;
  if (!para) {
    put(t, n, singular ? PSTR("\x03") : PSTR("S\x02O"), MUTED);
    put(t, n, hw, hourC);
    if (r == 0) {
      if (hh != 0 && hh != 12) put(t, n, PSTR("EM PONTO"), MUTED);
    } else putWords(t, n, nth(MIN_E, r / 5));
  } else {
    putWords(t, n, nth(MIN_PARA, (r - 35) / 5));
    put(t, n, PSTR("PARA"), MUTED);
    put(t, n, hh == 12 ? PSTR("O") : hh == 0 || hh % 12 == 1 ? PSTR("A") : PSTR("AS"), MUTED);
    put(t, n, hw, hourC);
  }
  if (hh != 0 && hh != 12) {
    put(t, n, PSTR("DA"), MUTED);
    put(t, n, hh < 5 ? PSTR("MADRUGADA") : hh < 12 ? PSTR("MANH\x02") : hh < 18 ? PSTR("TARDE") : PSTR("NOITE"), MUTED);
  }
  return n;
}

void themeWords(const UiState &s, bool full) {
  statusBar(s, s.ip ? s.ip : "", BG);
  if (!s.timeOk) return;
  Tok t[16];
  int n = buildPhrase(s, t, WORK);
  char key[64] = "";
  for (int i = 0; i < n; i++) {
    size_t kl = strlen(key), k = sizeof key - kl - 2;
    if (t[i].len < k) k = t[i].len;
    memcpy_P(key + kl, t[i].s, k);
    key[kl + k] = 0;
    strncat(key, " ", sizeof key - strlen(key) - 1);
  }
  if (changed(cA, key)) {
    G->fillRect(0, 24, 240, 184, BG);
    // quebra em linhas de até 12 caracteres (tamanho 3 = 18 px por caractere)
    int lineStart[8], lineLen[8], lines = 0, len = 0;
    for (int i = 0; i < n && lines <= 8; i++) {
      int wl = t[i].len;
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
      for (int k = 0; k < lineLen[l]; k++) chars += t[lineStart[l] + k].len + (k ? 1 : 0);
      int x = 120 - (chars * 18 - 3) / 2;
      for (int k = 0; k < lineLen[l]; k++) {
        const Tok &tk = t[lineStart[l] + k];
        wordText(x, y, 3, tk.c, tk.s, tk.len);
        x += (tk.len + 1) * 18;
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

