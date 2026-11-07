// Gera smalltv-pomodoro/tunel_lut.h: a tabela polar do túnel das cenas 3D (camada "world3d", geometry "tunnel").
// Uma amostra a cada 2x2 pixels num quarto da tela (60x60, espelhado nos outros três):
//  - profundidade (0-255) = 38,4 / distância ao centro, guardada por linha como diferenças de -7 a +7 num nibble
//    (15 = valor cheio nos dois nibbles seguintes), sem perda;
//  - ângulo em 4 setores por quadrante (2 bits);
//  - profundidade do túnel quadrado por anel (60 valores).
// Rode depois de mudar a fórmula: node sim/gerar_tunel.js (confere a descompactação antes de gravar).
const fs = require('fs'), path = require('path');
const out = path.join(__dirname, '..', 'smalltv-pomodoro', 'tunel_lut.h');

// round() do Python 3 (empate vai para o par), para a tabela não mudar em relação à primeira versão
const roundEven = v => { const f = Math.floor(v), d = v - f; return d > 0.5 ? f + 1 : d < 0.5 ? f : (f % 2 ? f + 1 : f); };

const depths = [], angles = [], offsets = [], packed = [];
for (let y = 0; y < 60; y++) {
  const row = [], ns = [];
  for (let x = 0; x < 60; x++) {
    const dx = (x + 0.5) / 60, dy = (y + 0.5) / 60;
    row.push(Math.min(255, roundEven(38.4 / Math.sqrt(dx * dx + dy * dy))));
    angles.push(Math.min(3, Math.trunc(Math.atan2(dy, dx) * 8 / Math.PI)));
  }
  depths.push(row);
  offsets.push(packed.length);
  let prev = 0;
  for (const v of row) {
    const d = v - prev;
    prev = v;
    if (d >= -7 && d <= 7) ns.push(d + 7);
    else ns.push(15, v >> 4, v & 15);
  }
  if (ns.length % 2) ns.push(0);
  for (let i = 0; i < ns.length; i += 2) packed.push(ns[i] << 4 | ns[i + 1]);
}
const angleBytes = [];
for (let i = 0; i < angles.length; i += 4) angleBytes.push(angles[i] | angles[i + 1] << 2 | angles[i + 2] << 4 | angles[i + 3] << 6);
const square = [];
for (let q = 0; q < 60; q++) square.push(Math.min(255, roundEven(4608 / (2 * q + 1))));

// confere a leitura como o firmware faz (facepack.cpp: drawTunnel)
for (let i = 0; i < 3600; i++) if ((angleBytes[i >> 2] >> ((i & 3) * 2) & 3) !== angles[i]) throw new Error('ângulo ' + i);
for (let y = 0; y < 60; y++) {
  let nib = offsets[y] * 2, prev = 0;
  const next = () => { const b = packed[nib >> 1], v = nib & 1 ? b & 15 : b >> 4; nib++; return v; };
  for (let x = 0; x < 60; x++) {
    const v = next();
    if (v === 15) { const hi = next(), lo = next(); prev = hi << 4 | lo; } else prev += v - 7;
    if (prev !== depths[y][x]) throw new Error(`profundidade ${x},${y}`);
  }
}

const rows = (items, n, f) => {
  const l = [];
  for (let i = 0; i < items.length; i += n) l.push('  ' + items.slice(i, i + n).map(f).join(', ') + ',');
  return l.join('\n');
};
const hex = v => '0x' + v.toString(16).padStart(2, '0'), dec = v => String(v);
const h = `// Gerado por sim/gerar_tunel.js (não edite à mão): tabela polar do túnel das cenas 3D, sem perda.
#pragma once
#include <pgmspace.h>
static const uint16_t TUNEL_ROWS[60] PROGMEM = {
${rows(offsets, 12, dec)}
};
static const uint8_t TUNEL_DEPTH[${packed.length}] PROGMEM = {
${rows(packed, 16, hex)}
};
static const uint8_t TUNEL_ANGLE[${angleBytes.length}] PROGMEM = {
${rows(angleBytes, 16, hex)}
};
static const uint8_t TUNEL_SQUARE[60] PROGMEM = {
${rows(square, 16, dec)}
};
`;
fs.writeFileSync(out, h);
console.log(`túnel: ${packed.length + angleBytes.length + square.length + 120} bytes na flash; 3600 profundidades e ângulos conferidos`);
