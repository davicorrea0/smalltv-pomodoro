// Gera smalltv-pomodoro/bri_curva.h: quanto o backlight fica aceso (em 1/256, o passo do sigma-delta do ESP8266) para o
// brilho 0..99, curva gama 2,2, em PROGMEM. 100 % não está na tabela: é BL_FULL (256), aceso direto, sem modulação.
// Fórmula: BL_MIN + (BL_FULL - BL_MIN) * (b/100)^2.2, arredondada; 0 para b <= 0. BL_MIN = 3 (1,17 %) não deixa o 1 %
// mais escuro que o do PWM antigo (10/1023 = 0,98 %).
// Uso: node sim/gerar_brilho.js [saída]   (sem argumento, escreve smalltv-pomodoro/bri_curva.h)
const fs = require('fs');
const path = require('path');
const BL_FULL = 256, BL_MIN = 3;
const out = process.argv[2] || path.join(__dirname, '..', 'smalltv-pomodoro', 'bri_curva.h');
const v = [];
for (let b = 0; b < 100; b++) v.push(b === 0 ? 0 : BL_MIN + Math.floor((BL_FULL - BL_MIN) * Math.pow(b / 100, 2.2) + 0.5));
if (v.some(x => x > 255)) throw Error('a tabela precisa caber em uint8_t');
let s = '// Gerado por sim/gerar_brilho.js; não edite à mão. Backlight aceso em 1/256 (sigma-delta) para brilho 0..99:\n';
s += '// BL_MIN + (BL_FULL - BL_MIN) * (b/100)^2.2, arredondado (BL_MIN 3, BL_FULL 256). 100 % = BL_FULL, fora da tabela.\n';
s += '#pragma once\n#include <stdint.h>\nstatic const uint8_t BRI_CURVA[100] PROGMEM = {\n';
for (let i = 0; i < 100; i += 10) s += '  ' + v.slice(i, i + 10).join(', ') + ',\n';
s += '};\n';
fs.writeFileSync(out, s.replace(/\r/g, ''));
