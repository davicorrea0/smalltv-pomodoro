// Curva de brilho: a tabela (bri_curva.h, em 1/256 do sigma-delta) contra a fórmula com powf, em TODAS as entradas
// (int de -1000 a 1000). Erro máximo permitido: 1 passo. O 1 % não pode ficar mais escuro que o do PWM antigo
// (10/1023). Compila com o shim do Arduino: g++ -Ishim brilho_test.cpp
#include "shim/Arduino.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "../smalltv-pomodoro/bri_curva.h"
#include "../smalltv-pomodoro/noite.h"
#ifndef pgm_read_byte
#define pgm_read_byte(a) (*(const uint8_t *)(a))
#endif

const uint16_t BL_FULL = 256, BL_MIN = 3;
// cópia da briDuty de modulos/backlight.cpp (sem float)
static uint16_t briDuty(int b) {
  if (b <= 0) return 0;
  if (b >= 100) return BL_FULL;
  return pgm_read_byte(&BRI_CURVA[b]);
}
static uint16_t formula(int b) {
  if (b <= 0) return 0;
  if (b > 100) b = 100;
  return BL_MIN + (uint16_t)((BL_FULL - BL_MIN) * powf(b / 100.0f, 2.2f) + 0.5f);
}

int main() {
  int pior = 0;
  for (int b = -1000; b <= 1000; b++) {  // 0..100 é a faixa real; acima de 100 satura em 100
    int d = abs((int)briDuty(b) - (int)formula(b));
    if (d > pior) pior = d;
    if (d > 1) { printf("brilho_test: FALHA b=%d tabela=%u fórmula=%u\n", b, briDuty(b), formula(b)); return 1; }
  }
  if (briDuty(0) != 0 || briDuty(1) < BL_MIN || briDuty(100) != BL_FULL || briDuty(99) >= BL_FULL) { printf("brilho_test: FALHA nos extremos\n"); return 1; }
  if (briDuty(1) * 1023 < 10 * 256) { printf("brilho_test: FALHA: 1 %% mais escuro que o PWM antigo (10/1023)\n"); return 1; }
  for (int b = 1; b <= 100; b++) if (briDuty(b) < briDuty(b - 1)) { printf("brilho_test: FALHA monotonia em %d\n", b); return 1; }
  // brilho noturno 0 = apagado; tocar/avisar acende no mínimo útil e, passado o tempo, volta a apagar
  uint32_t ate = 1000 + 10000;
  if (briDuty(briEfetivo(0, wakeAtivo(0, 5000))) != 0) { printf("brilho_test: FALHA noite 0 sem aviso deve apagar\n"); return 1; }
  if (briDuty(briEfetivo(0, wakeAtivo(ate, 5000))) < BL_MIN) { printf("brilho_test: FALHA aviso deve acender\n"); return 1; }
  if (briDuty(briEfetivo(0, wakeAtivo(ate, ate))) != 0) { printf("brilho_test: FALHA depois do tempo deve apagar\n"); return 1; }
  if (!wakeAtivo(0xFFFFFF00u + 0x1000u, 0xFFFFFF00u)) { printf("brilho_test: FALHA virada do millis\n"); return 1; }  // até depois da virada de 32 bits
  for (int b = 1; b <= 100; b++) if (briEfetivo(b, true) != b || briEfetivo(b, false) != b) { printf("brilho_test: FALHA brilho %d mudou\n", b); return 1; }
  printf("brilho_test: ok (101 entradas em 1/256, erro máximo %d)\n", pior);
  return 0;
}
