#include "../smalltv-pomodoro/utf8_pixel.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int casos = 0;

static void confere(const unsigned char *s, uint16_t esperado, size_t avanco) {
  const unsigned char *p = s;
  assert(utf8ToCp437(p) == esperado);
  assert((size_t)(p - s) == avanco);
  casos++;
}

static void percorre(const unsigned char *s) {
  const unsigned char *fim = s;
  while (*fim) fim++;
  const unsigned char *p = s;
  int passos = 0;
  while (*p) {
    const unsigned char *antes = p;
    (void)utf8ToCp437(p);
    assert(p > antes);
    assert(p <= fim);
    assert(++passos <= 13);
  }
  assert(p == fim);
  casos++;
}

static void buffer(const unsigned char *src, size_t n) {
  unsigned char *s = (unsigned char *)malloc(n + 1);
  assert(s);
  memcpy(s, src, n);
  s[n] = 0;
  percorre(s);
  free(s);
}

int main() {
  struct Mapa { uint8_t segundo, cp; char letra; } mapa[] = {
    {0xA4, 0x84, 'a'}, {0xB6, 0x94, 'o'}, {0xBC, 0x81, 'u'}, {0x9F, 0xE1, 0},
    {0x84, 0x8E, 'A'}, {0x96, 0x99, 'O'}, {0x9C, 0x9A, 'U'},
  };
  for (const Mapa &m : mapa) {
    const unsigned char pre[] = {0xC3, m.segundo, 0};
    confere(pre, m.cp, 2);
    if (m.letra) {
      const unsigned char dec[] = {(unsigned char)m.letra, 0xCC, 0x88, 0};
      confere(dec, m.cp, 3);
    }
  }
  const unsigned char outroTrema[] = {'e', 0xCC, 0x88, 0};
  confere(outroTrema, 'e', 3);
  const unsigned char controleTrema[] = {1, 0xCC, 0x88, 0};
  confere(controleTrema, 1, 1);
  const unsigned char *combina = controleTrema + 1;
  confere(combina, 0, 2);

  const uint8_t latin[] = {0xA1,0xA9,0xAD,0xB3,0xBA,0xA3,0xB5,0xA7,0xB1,
                           0x81,0x89,0x8D,0x93,0x9A,0x83,0x95,0x87,0x91};
  // Mantém exatamente a redução anterior do runText, inclusive ñ/Ñ no intervalo de o/O.
  const char base[] = "aeiouaocoAEIOUAOCO";
  int bi = 0;
  for (uint8_t d : latin) {
    const unsigned char s[] = {0xC3, d, 0};
    confere(s, (uint8_t)base[bi++], 2);
  }

  for (uint8_t d = 0x90; d <= 0xAF; d++) {
    const unsigned char s[] = {0xD0, d, 0};
    confere(s, (uint16_t)(0x100 + d - 0x90), 2);  // А..Я
  }
  confere((const unsigned char *)"\xD0\x81", 0x100 + 32, 2);  // Ё
  for (uint8_t d = 0xB0; d <= 0xBF; d++) {
    const unsigned char s[] = {0xD0, d, 0};
    confere(s, (uint16_t)(0x100 + 33 + d - 0xB0), 2);  // а..п
  }
  for (uint8_t d = 0x80; d <= 0x8F; d++) {
    const unsigned char s[] = {0xD1, d, 0};
    confere(s, (uint16_t)(0x100 + 49 + d - 0x80), 2);  // р..я
  }
  confere((const unsigned char *)"\xD1\x91", 0x100 + 65, 2);  // ё

  confere((const unsigned char *)"І", 0, 2);  // І (ucraniano): fora do russo, ignorado sem largura
  confere((const unsigned char *)"ї", 0, 2);  // ї
  const uint8_t valores[] = {0x41,0x80,0x81,0x8F,0x91,0xA4,0xAF,0xB0,0xBC,
                             0xC0,0xC3,0xCC,0xD0,0xD1,0xE2,0xF0,0xFF};
  for (int a = 1; a < 256; a++) {
    unsigned char s[] = {(unsigned char)a};
    buffer(s, 1);
  }
  for (int a = 1; a < 256; a++)
    for (int b = 0; b < 256; b++) {
      unsigned char s[] = {(unsigned char)a, (unsigned char)b};
      buffer(s, 2);
    }
  for (uint8_t a : valores) for (uint8_t b : valores) for (uint8_t c : valores) {
    unsigned char s[] = {a, b, c};
    buffer(s, 3);
  }

  uint32_t r = 0x5A17C9E3u;
  for (int k = 0; k < 20000; k++) {
    r = r * 1664525u + 1013904223u;
    size_t n = r % 13;
    unsigned char s[12];
    for (size_t i = 0; i < n; i++) {
      r = r * 1664525u + 1013904223u;
      s[i] = (unsigned char)(r >> 24);
    }
    buffer(s, n);
  }
  printf("utf8_pixel: ok (%d casos)\n", casos);
}
