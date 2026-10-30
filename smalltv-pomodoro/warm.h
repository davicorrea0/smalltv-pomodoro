#pragma once
#include <stdint.h>
// "Noite quente": o gama DIGITAL do ST7789V (DGMEN 0xBA, DGMLUTR 0xE2, DGMLUTB 0xE3) troca os níveis de vermelho
// e azul antes de chegarem ao vidro, sem redesenhar um pixel. Cada tabela tem 64 bytes (nível de entrada de
// 6 bits -> nível de saída de 6 bits); o verde não tem tabela. À noite o azul é reduzido e o vermelho fica como
// está; de dia o DGMEN fica desligado (valor de reset do chip, 0x00) e as tabelas nem são usadas.
// O gama ANALÓGICO (0xE0/0xE1) não é tocado: a Adafruit_ST7789 também não o envia, mas o datasheet não deixa
// o valor de reset legível com segurança, então não há como devolvê-lo ao que era.
// Sem Arduino aqui: o teste do simulador (sim/warm_test.cpp) inclui este arquivo e registra os comandos.
static const uint8_t WARM_DGMEN = 0xBA, WARM_LUT_R = 0xE2, WARM_LUT_B = 0xE3;
static const uint8_t WARM_BLUE_CUT = 45;  // a 100 %, o azul cai 45 % (de ~6500 K para ~3000 K, a olho)

// Intensidade que vale agora: a prévia vence; senão o ajuste só vale à noite
static inline uint8_t warmTarget(bool night, bool preview, uint8_t cfgPct, uint8_t previewPct) {
  uint8_t p = preview ? previewPct : night ? cfgPct : 0;
  return p > 100 ? 100 : p;
}

// Tabela do azul para a intensidade pct (0..100): saída = entrada * (100 - corte) / 100, arredondado
static inline void warmLutBlue(uint8_t *lut, uint8_t pct) {
  uint16_t k = 100 - (uint16_t)WARM_BLUE_CUT * pct / 100;
  for (uint8_t i = 0; i < 64; i++) lut[i] = (uint8_t)((i * k + 50) / 100);
}

// Manda ao chip o estado de intensidade pct. send(cmd, dados, n) é o tft.sendCommand. pct 0 só desliga o DGMEN
// (de dia o chip volta ao padrão exato). Com pct > 0 as tabelas vão antes de ligar o DGMEN, sem lampejo.
template <class Send>
static inline void warmApply(Send send, uint8_t pct) {
  if (!pct) {
    uint8_t off = 0;
    send(WARM_DGMEN, &off, 1);
    return;
  }
  uint8_t lut[64];
  for (uint8_t i = 0; i < 64; i++) lut[i] = i;  // vermelho intacto (identidade)
  send(WARM_LUT_R, lut, 64);
  warmLutBlue(lut, pct);
  send(WARM_LUT_B, lut, 64);
  uint8_t on = 0x04;  // DGMEN é o bit 2 do parâmetro
  send(WARM_DGMEN, &on, 1);
}
