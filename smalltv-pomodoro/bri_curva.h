// Gerado por sim/gerar_brilho.js; não edite à mão. Backlight aceso em 1/256 (sigma-delta) para brilho 0..99:
// BL_MIN + (BL_FULL - BL_MIN) * (b/100)^2.2, arredondado (BL_MIN 3, BL_FULL 256). 100 % = BL_FULL, fora da tabela.
#pragma once
#include <stdint.h>
static const uint8_t BRI_CURVA[100] PROGMEM = {
  0, 3, 3, 3, 3, 3, 4, 4, 4, 4,
  5, 5, 5, 6, 6, 7, 7, 8, 9, 10,
  10, 11, 12, 13, 14, 15, 16, 17, 18, 20,
  21, 22, 24, 25, 27, 28, 30, 31, 33, 35,
  37, 39, 41, 43, 45, 47, 49, 51, 53, 56,
  58, 61, 63, 66, 68, 71, 74, 76, 79, 82,
  85, 88, 91, 95, 98, 101, 104, 108, 111, 115,
  118, 122, 126, 130, 133, 137, 141, 145, 149, 154,
  158, 162, 166, 171, 175, 180, 185, 189, 194, 199,
  204, 209, 214, 219, 224, 229, 234, 240, 245, 250,
};
