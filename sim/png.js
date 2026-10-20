// Converte out/*.raw (RGB565, 240x240) em PNG ampliado 2x e monta uma folha com todas as telas.
const fs = require('fs'), zlib = require('zlib'), path = require('path');
const S = 2, W = 240;
function crcTable() { const t = []; for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; t[n] = c >>> 0; } return t; }
const CT = crcTable();
const crc = b => { let c = 0xffffffff; for (const x of b) c = CT[(c ^ x) & 255] ^ (c >>> 8); return (c ^ 0xffffffff) >>> 0; };
function chunk(type, data) { const l = Buffer.alloc(4); l.writeUInt32BE(data.length); const td = Buffer.concat([Buffer.from(type), data]); const c = Buffer.alloc(4); c.writeUInt32BE(crc(td)); return Buffer.concat([l, td, c]); }
function png(w, h, rgb) {
  const raw = Buffer.alloc((w * 3 + 1) * h);
  for (let y = 0; y < h; y++) { raw[y * (w * 3 + 1)] = 0; rgb.copy(raw, y * (w * 3 + 1) + 1, y * w * 3, (y + 1) * w * 3); }
  const ihdr = Buffer.alloc(13); ihdr.writeUInt32BE(w, 0); ihdr.writeUInt32BE(h, 4); ihdr[8] = 8; ihdr[9] = 2;
  return Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', ihdr), chunk('IDAT', zlib.deflateSync(raw)), chunk('IEND', Buffer.alloc(0))]);
}
function toRgb(raw, scale) {
  const w = W * scale, out = Buffer.alloc(w * w * 3);
  for (let y = 0; y < w; y++) for (let x = 0; x < w; x++) {
    const v = raw.readUInt16LE(((y / scale | 0) * W + (x / scale | 0)) * 2);
    const i = (y * w + x) * 3;
    out[i] = ((v >> 11) & 31) * 255 / 31; out[i + 1] = ((v >> 5) & 63) * 255 / 63; out[i + 2] = (v & 31) * 255 / 31;
  }
  return out;
}
const dir = path.join(__dirname, 'out');
const files = fs.readdirSync(dir).filter(f => f.endsWith('.raw') && !f.startsWith('inc_') && !f.startsWith('fresh_')).sort();
for (const f of fs.readdirSync(dir).filter(f => f.endsWith('.raw'))) fs.writeFileSync(path.join(dir, f.replace('.raw', '.png')), png(W * S, W * S, toRgb(fs.readFileSync(path.join(dir, f)), S)));
// folha: 4 colunas, telas em 1x com 12 px de moldura escura (como o vidro do aparelho)
const cols = 4, pad = 12, cell = W + pad * 2, rows = Math.ceil(files.length / cols);
const sw = cols * cell, sh = rows * cell, sheet = Buffer.alloc(sw * sh * 3, 40);
files.forEach((f, k) => {
  const rgb = toRgb(fs.readFileSync(path.join(dir, f)), 1), ox = (k % cols) * cell + pad, oy = (k / cols | 0) * cell + pad;
  for (let y = 0; y < W; y++) rgb.copy(sheet, ((oy + y) * sw + ox) * 3, y * W * 3, (y + 1) * W * 3);
});
fs.writeFileSync(path.join(dir, 'folha.png'), png(sw, sh, sheet));
console.log(files.map((f, i) => `${i + 1}. ${f.replace('.raw', '')}`).join('\n'));
// Uso extra: node png.js <saida.png> <tela1> <tela2> ...  -> folha só com essas telas
if (process.argv.length > 3) {
  const [out, ...names] = process.argv.slice(2);
  const cols = Math.min(4, names.length), rows2 = Math.ceil(names.length / cols);
  const w2 = cols * cell, h2 = rows2 * cell, sh2 = Buffer.alloc(w2 * h2 * 3, 40);
  names.forEach((n, k) => {
    const rgb = toRgb(fs.readFileSync(path.join(dir, n + '.raw')), 1), ox = (k % cols) * cell + pad, oy = (k / cols | 0) * cell + pad;
    for (let y = 0; y < W; y++) rgb.copy(sh2, ((oy + y) * w2 + ox) * 3, y * W * 3, (y + 1) * W * 3);
  });
  fs.writeFileSync(path.join(dir, out), png(w2, h2, sh2));
}
