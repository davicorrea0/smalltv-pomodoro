#pragma once
#include <stdint.h>

// Letra sem acento de U+00C0..U+00FF (segundo byte d de "C3 d", 0x80..0xBF): a redução usada nas fontes sem acento.
// Conferida contra o FOLD de face_mundo.cpp em sim/utf8_test.cpp. Sem tabela: o grupo de 32 (k) dá a letra e d >= 0xA0 a caixa baixa.
// Æ Ð Þ ß viram A D P s; × e ÷ viram x e /; Ø vira O; ÿ vira y.
static inline unsigned char latin1Base(unsigned char d) {
  const unsigned char k = d & 0x1F;
  const bool baixa = d >= 0xA0;
  if (k == 23) return baixa ? '/' : 'x';
  if (k == 31) return baixa ? 'y' : 's';
  const unsigned char c = k <= 6 ? 'A' : k == 7 ? 'C' : k <= 11 ? 'E' : k <= 15 ? 'I' : k == 16 ? 'D' : k == 17 ? 'N'
                        : k <= 22 ? 'O' : k == 24 ? 'O' : k <= 28 ? 'U' : k == 29 ? 'Y' : 'P';
  return baixa ? c + 32 : c;
}

// Retorna 1..255 para CP437 e 0x100+i para cirílico. A ordem cirílica é:
// А Б В Г Д Е Ж З И Й К Л М Н О П Р С Т У Ф Х Ц Ч Ш Щ Ъ Ы Ь Э Ю Я Ё,
// seguida por а..я e ё (maiúsculas 0..32, minúsculas 33..65).
static inline uint16_t utf8ToCp437(const unsigned char *&p) {
  const uint8_t c = *p;
  if (c < 0x80) {
    const bool letra = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
    if (letra && p[1] == 0xCC && p[2] == 0x88) {
      p += 3;
      return c == 'a' ? 0x84 : c == 'o' ? 0x94 : c == 'u' ? 0x81
           : c == 'A' ? 0x8E : c == 'O' ? 0x99 : c == 'U' ? 0x9A : c;
    }
    p++;
    return c;
  }

  if (c == 0xC3 && p[1] >= 0x80 && p[1] <= 0xBF) {
    const uint8_t d = p[1];
    p += 2;
    if (d == 0xA4) return 0x84;
    if (d == 0xB6) return 0x94;
    if (d == 0xBC) return 0x81;
    if (d == 0x9F) return 0xE1;
    if (d == 0x84) return 0x8E;
    if (d == 0x96) return 0x99;
    if (d == 0x9C) return 0x9A;
    return latin1Base(d);
  }

  if (c == 0xD0) {
    const uint8_t d = p[1];
    if (d == 0x81) { p += 2; return 0x100 + 32; }       // Ё
    if (d >= 0x90 && d <= 0xAF) { p += 2; return 0x100 + d - 0x90; }  // А..Я
    if (d >= 0xB0 && d <= 0xBF) { p += 2; return 0x100 + 33 + d - 0xB0; }  // а..п
  } else if (c == 0xD1) {
    const uint8_t d = p[1];
    if (d >= 0x80 && d <= 0x8F) { p += 2; return 0x100 + 49 + d - 0x80; }  // р..я
    if (d == 0x91) { p += 2; return 0x100 + 65; }       // ё
  }

  uint8_t n = c < 0xC0 ? 1 : c <= 0xDF ? 2 : c <= 0xEF ? 3 : c <= 0xF7 ? 4
            : c <= 0xFB ? 5 : c <= 0xFD ? 6 : 1;
  p++;
  for (uint8_t i = 1; i < n && *p >= 0x80 && *p <= 0xBF; i++) p++;
  return 0;
}
