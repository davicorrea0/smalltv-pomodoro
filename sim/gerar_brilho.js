// Gera smalltv-pomodoro/bri_curva.h: duty do backlight (curva gama 2,2) para brilho 0..100, em PROGMEM.
// Mesma fórmula do firmware antigo: BL_MIN + (BL_RANGE - BL_MIN) * (b/100)^2.2, arredondada; 0 para b <= 0.
// Uso: node sim/gerar_brilho.js [saída]   (sem argumento, escreve smalltv-pomodoro/bri_curva.h)
const fs = require('fs');
const path = require('path');
const BL_RANGE = 1023, BL_MIN = 10;
const out = process.argv[2] || path.join(__dirname, '..', 'smalltv-pomodoro', 'bri_curva.h');
const v = [];
for (let b = 0; b <= 100; b++) v.push(b === 0 ? 0 : BL_MIN + Math.floor((BL_RANGE - BL_MIN) * Math.pow(b / 100, 2.2) + 0.5));
let s = '// Gerado por sim/gerar_brilho.js; não edite à mão. Duty do backlight (0..1023) para brilho 0..100:\n';
s += '// BL_MIN + (BL_RANGE - BL_MIN) * (b/100)^2.2, arredondado. Tabela no lugar do powf (que puxava ~2,4 KB de libm).\n';
s += '#pragma once\n#include <stdint.h>\nstatic const uint16_t BRI_CURVA[101] PROGMEM = {\n';
for (let i = 0; i < 101; i += 10) s += '  ' + v.slice(i, i + 10).join(', ') + ',\n';
s += '};\n';
fs.writeFileSync(out, s.replace(/\r/g, ''));
