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

// Glifos latinos desenhados no ui.cpp (acentos que a CP437 não tem): 0x100 + 66 + i, i < UTF8_LAT_N.
static const uint16_t UTF8_LAT = 0x100 + 66;
static const int UTF8_LAT_N = 21;

// Retorna 1..255 para CP437 e 0x100+i para cirílico (e os latinos acima). A ordem cirílica é:
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
    // Acentos do francês, do português e do espanhol: os que a fonte CP437 tem viram o glifo dela; os que ela não
    // tem (e o ç, que nela sai torto) vêm da tabela latina do ui.cpp, depois dos 66 cirílicos (0x100 + UTF8_LAT + i).
    switch (d) {
      case 0x80: return UTF8_LAT + 0;   // À
      case 0x81: return UTF8_LAT + 1;   // Á
      case 0x82: return UTF8_LAT + 2;   // Â
      case 0x83: return UTF8_LAT + 3;   // Ã
      case 0x85: return 0x8F;           // Å
      case 0x86: return 0x92;           // Æ
      case 0x87: return 0x80;           // Ç
      case 0x88: return UTF8_LAT + 4;   // È
      case 0x89: return 0x90;           // É
      case 0x8A: return UTF8_LAT + 5;   // Ê
      case 0x8B: return UTF8_LAT + 6;   // Ë
      case 0x8C: return UTF8_LAT + 7;   // Ì
      case 0x8D: return UTF8_LAT + 8;   // Í
      case 0x8E: return UTF8_LAT + 9;   // Î
      case 0x8F: return UTF8_LAT + 10;  // Ï
      case 0x91: return 0xA5;           // Ñ
      case 0x92: return UTF8_LAT + 11;  // Ò
      case 0x93: return UTF8_LAT + 12;  // Ó
      case 0x94: return UTF8_LAT + 13;  // Ô
      case 0x95: return UTF8_LAT + 14;  // Õ
      case 0x99: return UTF8_LAT + 15;  // Ù
      case 0x9A: return UTF8_LAT + 16;  // Ú
      case 0x9B: return UTF8_LAT + 17;  // Û
      case 0xA0: return 0x85;           // à
      case 0xA1: return 0xA0;           // á
      case 0xA2: return 0x83;           // â
      case 0xA3: return UTF8_LAT + 18;  // ã
      case 0xA5: return 0x86;           // å
      case 0xA6: return 0x91;           // æ
      case 0xA7: return UTF8_LAT + 20;  // ç
      case 0xA8: return 0x8A;           // è
      case 0xA9: return 0x82;           // é
      case 0xAA: return 0x88;           // ê
      case 0xAB: return 0x89;           // ë
      case 0xAC: return 0x8D;           // ì
      case 0xAD: return 0xA1;           // í
      case 0xAE: return 0x8C;           // î
      case 0xAF: return 0x8B;           // ï
      case 0xB1: return 0xA4;           // ñ
      case 0xB2: return 0x95;           // ò
      case 0xB3: return 0xA2;           // ó
      case 0xB4: return 0x93;           // ô
      case 0xB5: return UTF8_LAT + 19;  // õ
      case 0xB9: return 0x97;           // ù
      case 0xBA: return 0xA3;           // ú
      case 0xBB: return 0x96;           // û
      case 0xBF: return 0x98;           // ÿ
    }
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
