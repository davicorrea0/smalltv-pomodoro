// Monta a prévia dos pacotes tokensmall: junta os .raw (RGB565 240x240) que o tokensmall_test grava em
// <pasta> (tokensmall_test <pacote> <pasta>) numa só imagem PNG, 4 telas por pacote, com moldura escura.
// Uso: node tokensmall_previa.js <pasta dos .raw> <saida.png> <id> [<id>...]
// Cada <id> vira uma linha: <id>-1-normal, -2-mais-largos, -3-uma-janela, -4-sem-dados.
const fs = require('fs'), zlib = require('zlib'), path = require('path');
const W = 240, PAD = 12, CELL = W + PAD * 2;
const ESTADOS = ['1-normal', '2-mais-largos', '3-uma-janela', '4-sem-dados'];
const crcT = (() => { const t = []; for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; t[n] = c >>> 0; } return t; })();
const crc = b => { let c = 0xffffffff; for (const x of b) c = crcT[(c ^ x) & 255] ^ (c >>> 8); return (c ^ 0xffffffff) >>> 0; };
function chunk(type, data) { const l = Buffer.alloc(4); l.writeUInt32BE(data.length); const td = Buffer.concat([Buffer.from(type), data]); const c = Buffer.alloc(4); c.writeUInt32BE(crc(td)); return Buffer.concat([l, td, c]); }
function png(w, h, rgb) {
  const raw = Buffer.alloc((w * 3 + 1) * h);
  for (let y = 0; y < h; y++) { raw[y * (w * 3 + 1)] = 0; rgb.copy(raw, y * (w * 3 + 1) + 1, y * w * 3, (y + 1) * w * 3); }
  const ihdr = Buffer.alloc(13); ihdr.writeUInt32BE(w, 0); ihdr.writeUInt32BE(h, 4); ihdr[8] = 8; ihdr[9] = 2;
  return Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', ihdr), chunk('IDAT', zlib.deflateSync(raw)), chunk('IEND', Buffer.alloc(0))]);
}
const [pasta, saida, ...ids] = process.argv.slice(2);
if (!pasta || !saida || !ids.length) { console.error('uso: node tokensmall_previa.js <pasta> <saida.png> <id>...'); process.exit(2); }
const sw = ESTADOS.length * CELL, sh = ids.length * CELL, folha = Buffer.alloc(sw * sh * 3, 40);
ids.forEach((id, r) => ESTADOS.forEach((e, c) => {
  const raw = fs.readFileSync(path.join(pasta, `${id}-${e}.raw`));
  if (raw.length !== W * W * 2) { console.error(`${id}-${e}.raw com tamanho errado`); process.exit(1); }
  for (let y = 0; y < W; y++) for (let x = 0; x < W; x++) {
    const v = raw.readUInt16LE((y * W + x) * 2), i = ((r * CELL + PAD + y) * sw + c * CELL + PAD + x) * 3;
    folha[i] = ((v >> 11) & 31) * 255 / 31; folha[i + 1] = ((v >> 5) & 63) * 255 / 63; folha[i + 2] = (v & 31) * 255 / 31;
  }
}));
fs.writeFileSync(saida, png(sw, sh, folha));
console.log(`${saida}: ${sw}x${sh}`);
