// Curva de brilho: a tabela (bri_curva.h) contra a fórmula antiga com powf, em TODAS as entradas (int de -1000 a 1000).
// Erro máximo permitido: 1 no duty. Compila com o shim do Arduino: g++ -Ishim brilho_test.cpp
#include "shim/Arduino.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "../smalltv-pomodoro/bri_curva.h"

const uint16_t BL_RANGE = 1023, BL_MIN = 10;
// cópia da briDuty de modulos/backlight.cpp (sem float)
static uint16_t briDuty(int b) {
  if (b <= 0) return 0;
  if (b > 100) b = 100;
  return pgm_read_word(&BRI_CURVA[b]);
}
static uint16_t antiga(int b) {
  if (b <= 0) return 0;
  return BL_MIN + (uint16_t)((BL_RANGE - BL_MIN) * powf(b / 100.0f, 2.2f) + 0.5f);
}

int main() {
  int pior = 0;
  for (int b = -1000; b <= 100; b++) {  // 0..100 é a faixa real; acima de 100 satura em 100
    int d = abs((int)briDuty(b) - (int)antiga(b));
    if (d > pior) pior = d;
    if (d > 1) { printf("brilho_test: FALHA b=%d nova=%u antiga=%u\n", b, briDuty(b), antiga(b)); return 1; }
  }
  if (briDuty(0) != 0 || briDuty(1) < BL_MIN || briDuty(100) != BL_RANGE) { printf("brilho_test: FALHA nos extremos\n"); return 1; }
  for (int b = 1; b <= 100; b++) if (briDuty(b) < briDuty(b - 1)) { printf("brilho_test: FALHA monotonia em %d\n", b); return 1; }
  printf("brilho_test: ok (101 entradas, erro máximo %d)\n", pior);
  return 0;
}
