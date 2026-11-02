#pragma once
#include <stdint.h>
// "Noite quente": o gama DIGITAL do ST7789V (DGMEN 0xBA, DGMLUTR 0xE2, DGMLUTB 0xE3) troca os níveis de vermelho
// e azul antes de chegarem ao vidro, sem redesenhar um pixel. Cada tabela tem 64 bytes (nível de entrada de
// 6 bits -> nível de saída de 8 bits, 0..255: identidade = i*255/63, não i); o verde não tem tabela. À noite o azul é reduzido e o vermelho fica como
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

// Identidade: entrada de 6 bits -> saída de 8 bits
static inline uint8_t warmId(uint8_t i) { return (uint8_t)((i * 255 + 31) / 63); }

// Tabela do azul para a intensidade pct (0..100): saída = identidade * (100 - corte) / 100, arredondado.
// conj: o chip aplicaria a tabela DEPOIS da inversão (INVON), com a entrada e a saída invertidas; a tabela
// conjugada (255 - f(255 - x)) dá o mesmo resultado nesse caso. O datasheet não diz em que ponto da cadeia a
// inversão fica (a figura 37 é imagem), então o normal é conj = false e a prévia do painel deixa comparar.
static inline void warmLutBlue(uint8_t *lut, uint8_t pct, bool conj = false) {
  uint16_t k = 100 - (uint16_t)WARM_BLUE_CUT * pct / 100;
  for (uint8_t i = 0; i < 64; i++) {
    uint16_t v = ((uint16_t)warmId(conj ? 63 - i : i) * k + 50) / 100;
    lut[i] = (uint8_t)(conj ? 255 - v : v);
  }
}

// Manda ao chip o estado de intensidade pct. send(cmd, dados, n) é o tft.sendCommand. pct 0 só desliga o DGMEN
// (de dia o chip volta ao padrão exato). Com pct > 0 as tabelas vão antes de ligar o DGMEN, sem lampejo.
template <class Send>
static inline void warmApply(Send send, uint8_t pct, bool conj = false) {
  if (!pct) {
    uint8_t off = 0;
    send(WARM_DGMEN, &off, 1);
    return;
  }
  uint8_t lut[64];
  for (uint8_t i = 0; i < 64; i++) lut[i] = warmId(i);  // vermelho intacto (identidade; a conjugada dela é ela mesma)
  send(WARM_LUT_R, lut, 64);
  warmLutBlue(lut, pct, conj);
  send(WARM_LUT_B, lut, 64);
  uint8_t on = 0x04;  // DGMEN é o bit 2 do parâmetro
  send(WARM_DGMEN, &on, 1);
}

// "Verde de LCD antigo": o que a tela mostrou por defeito quando as tabelas iam em 0..63 em vez de 0..255 (vermelho
// = i, ~25 %; azul = i * 55 / 100, o que a cor quente a 100 % mandava; verde intacto). Vale nos mostradores LCD
// (LCD retrô, Previsão LCD e Mundo) e vence a cor quente. WARM_GREEN é o código do estado no chip (> 100: não colide
// com uma intensidade), para o warmFlush comparar com o que já foi enviado.
static const uint8_t WARM_GREEN = 200;
template <class Send>
static inline void warmApplyGreen(Send send) {
  uint8_t lut[64];
  for (uint8_t i = 0; i < 64; i++) lut[i] = i;
  send(WARM_LUT_R, lut, 64);
  for (uint8_t i = 0; i < 64; i++) lut[i] = (uint8_t)(i * 55 / 100);
  send(WARM_LUT_B, lut, 64);
  uint8_t on = 0x04;
  send(WARM_DGMEN, &on, 1);
}
// Estado que o chip deve ter: o verde (ligado e num LCD) vence; senão a intensidade da cor quente
static inline uint8_t warmState(bool green, uint8_t pct) { return green ? WARM_GREEN : pct; }
template <class Send>
static inline void warmApplyState(Send send, uint8_t state, bool conj = false) {
  if (state == WARM_GREEN) warmApplyGreen(send);
  else warmApply(send, state, conj);
}
