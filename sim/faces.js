// Gera as prévias dos mostradores para a galeria do painel web, a partir de sim/out/face-N.raw:
//  - docs/faces/face-N.png (240x240) para o README
//  - smalltv-pomodoro/faces.h com os mesmos PNGs em PROGMEM, servidos pelo relógio em /face?t=N
// Rode depois do simulador (sim/build.sh já chama este script).
const fs = require('fs'), zlib = require('zlib'), path = require('path');
const root = path.join(__dirname, '..');
const T = [];
for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; T[n] = c >>> 0; }
const crc = b => { let c = 0xffffffff; for (const x of b) c = T[(c ^ x) & 255] ^ (c >>> 8); return (c ^ 0xffffffff) >>> 0; };
const chunk = (t, d) => {
  const l = Buffer.alloc(4); l.writeUInt32BE(d.length);
  const td = Buffer.concat([Buffer.from(t), d]); const c = Buffer.alloc(4); c.writeUInt32BE(crc(td));
  return Buffer.concat([l, td, c]);
};
function png(raw565) {
  const raw = Buffer.alloc((240 * 3 + 1) * 240);
  for (let y = 0; y < 240; y++) for (let x = 0; x < 240; x++) {
    const v = raw565.readUInt16LE((y * 240 + x) * 2), o = y * 721 + 1 + x * 3;
    raw[o] = ((v >> 11) & 31) * 255 / 31; raw[o + 1] = ((v >> 5) & 63) * 255 / 63; raw[o + 2] = (v & 31) * 255 / 31;
  }
  const ih = Buffer.alloc(13); ih.writeUInt32BE(240, 0); ih.writeUInt32BE(240, 4); ih[8] = 8; ih[9] = 2;
  return Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', ih),
    chunk('IDAT', zlib.deflateSync(raw, { level: 9 })), chunk('IEND', Buffer.alloc(0))]);
}

fs.mkdirSync(path.join(root, 'docs', 'faces'), { recursive: true });
let h = '// Gerado por sim/faces.js — não edite à mão.\n// Prévias PNG (240x240) dos mostradores, servidas em /face?t=N para a galeria do painel.\n#pragma once\n#include <pgmspace.h>\n\n';
const names = [];
let total = 0;
for (let i = 0; ; i++) {
  const src = path.join(__dirname, 'out', `face-${i}.raw`);
  if (!fs.existsSync(src)) break;
  const p = png(fs.readFileSync(src));
  fs.writeFileSync(path.join(root, 'docs', 'faces', `face-${i}.png`), p);
  total += p.length;
  h += `static const uint8_t FACE_PNG_${i}[] PROGMEM = {`;
  for (let k = 0; k < p.length; k++) h += (k % 24 ? '' : '\n  ') + '0x' + p[k].toString(16).padStart(2, '0') + ',';
  h += '\n};\n';
  names.push(i);
}
h += `\nstatic const uint8_t *const FACE_PNG[] = {${names.map(i => `FACE_PNG_${i}`).join(', ')}};\n`;
h += `static const uint16_t FACE_PNG_LEN[] = {${names.map(i => `sizeof(FACE_PNG_${i})`).join(', ')}};\n`;
h += `static const uint8_t FACE_PNG_COUNT = ${names.length};\n`;
fs.writeFileSync(path.join(root, 'smalltv-pomodoro', 'faces.h'), h);
console.log(`${names.length} prévias, ${(total / 1024).toFixed(1)} KB`);
