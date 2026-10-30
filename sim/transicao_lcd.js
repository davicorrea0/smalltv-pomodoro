// Monta a tira de quadros e o GIF da troca LCD retrô <-> Previsão LCD a partir de out/inc_segmentos_*.raw
// (gravados pelo sim.cpp, checkLcdCurtain). Saída: out/segmentos_<de>_<para>.gif e out/segmentos_tira.png.
// Sem dependências: PNG (zlib) e GIF (LZW) escritos à mão.
const fs = require('fs'), zlib = require('zlib'), path = require('path');
const W = 240, dir = path.join(__dirname, 'out');
const CT = []; for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; CT[n] = c >>> 0; }
const crc = b => { let c = 0xffffffff; for (const x of b) c = CT[(c ^ x) & 255] ^ (c >>> 8); return (c ^ 0xffffffff) >>> 0; };
function chunk(type, data) { const l = Buffer.alloc(4); l.writeUInt32BE(data.length); const td = Buffer.concat([Buffer.from(type), data]); const c = Buffer.alloc(4); c.writeUInt32BE(crc(td)); return Buffer.concat([l, td, c]); }
function png(w, h, rgb) {
  const raw = Buffer.alloc((w * 3 + 1) * h);
  for (let y = 0; y < h; y++) rgb.copy(raw, y * (w * 3 + 1) + 1, y * w * 3, (y + 1) * w * 3);
  const ihdr = Buffer.alloc(13); ihdr.writeUInt32BE(w, 0); ihdr.writeUInt32BE(h, 4); ihdr[8] = 8; ihdr[9] = 2;
  return Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', ihdr), chunk('IDAT', zlib.deflateSync(raw)), chunk('IEND', Buffer.alloc(0))]);
}
const rgb565 = v => [((v >> 11) & 31) * 255 / 31 | 0, ((v >> 5) & 63) * 255 / 63 | 0, (v & 31) * 255 / 31 | 0];
// LZW do GIF, índices de 8 bits
function lzw(idx) {
  const out = []; let cur = 0, nb = 0;
  const put = (code, bits) => { cur |= code << nb; nb += bits; while (nb >= 8) { out.push(cur & 255); cur >>>= 8; nb -= 8; } };
  let size = 9, next = 258, dict = new Map(); put(256, size);
  let p = idx[0];
  for (let i = 1; i < idx.length; i++) {
    const k = p * 256 + idx[i], q = dict.get(k);
    if (q !== undefined) { p = q; continue; }
    put(p, size); dict.set(k, next++);
    if (next > (1 << size) && size < 12) size++;
    if (next >= 4095) { put(256, size); dict = new Map(); size = 9; next = 258; }
    p = idx[i];
  }
  put(p, size); put(257, size); if (nb) out.push(cur & 255);
  return Buffer.from(out);
}
function gif(frames, delays) {
  const pal = new Map();
  for (const f of frames) for (let i = 0; i < f.length; i += 2) { const v = f.readUInt16LE(i); if (!pal.has(v)) pal.set(v, pal.size); }
  if (pal.size > 256) throw new Error('mais de 256 cores: ' + pal.size);
  const pt = Buffer.alloc(768); for (const [v, i] of pal) pt.set(rgb565(v), i * 3);
  const parts = [Buffer.from('GIF89a'), Buffer.from([W & 255, W >> 8, W & 255, W >> 8, 0xF7, 0, 0]), pt, Buffer.from([0x21, 0xFF, 11, ...Buffer.from('NETSCAPE2.0'), 3, 1, 0, 0, 0])];
  frames.forEach((f, n) => {
    const idx = Buffer.alloc(W * W); for (let i = 0; i < W * W; i++) idx[i] = pal.get(f.readUInt16LE(i * 2));
    const data = lzw(idx), d = delays[n];
    parts.push(Buffer.from([0x21, 0xF9, 4, 0, d & 255, d >> 8, 0, 0, 0x2C, 0, 0, 0, 0, W & 255, W >> 8, W & 255, W >> 8, 0, 8]));
    for (let o = 0; o < data.length; o += 255) { const s = data.subarray(o, o + 255); parts.push(Buffer.from([s.length]), s); }
    parts.push(Buffer.from([0]));
  });
  parts.push(Buffer.from([0x3B]));
  return Buffer.concat(parts);
}
const sets = {};
for (const f of fs.readdirSync(dir).filter(f => /^inc_segmentos_.*_\d+\.raw$/.test(f)).sort()) (sets[f.replace(/_\d+\.raw$/, '').slice(4)] ||= []).push(fs.readFileSync(path.join(dir, f)));
const names = Object.keys(sets);
if (!names.length) { console.log('sem quadros da segmentos (rode o simulador)'); process.exit(0); }
// tira: 6 quadros por linha em 1x (0, ..., fim), uma tira por sentido, empilhadas
const pick = fr => [...Array(9)].map((_, i) => Math.round(i * (fr.length - 1) / 8));
const cols = 9, cell = W / 2 + 6, rows = names.length, sw = cols * cell + 6, sh = rows * cell + 6, sheet = Buffer.alloc(sw * sh * 3, 40);
names.forEach((n, r) => pick(sets[n]).forEach((k, c) => {
  const fr = sets[n][k], ox = 6 + c * cell, oy = 6 + r * cell;
  for (let y = 0; y < W / 2; y++) for (let x = 0; x < W / 2; x++) {  // 1/2: média de 2x2 pelo canto, o bastante para a tira
    const [R, G, B] = rgb565(fr.readUInt16LE(((y * 2) * W + x * 2) * 2)), o = ((oy + y) * sw + ox + x) * 3;
    sheet[o] = R; sheet[o + 1] = G; sheet[o + 2] = B;
  }
}));
fs.writeFileSync(path.join(dir, 'segmentos_tira.png'), png(sw, sh, sheet));
for (const n of names) {
  const fr = sets[n], d = fr.map((_, i) => (i === 0 || i === fr.length - 1 ? 90 : 6));  // centésimos: segura o primeiro e o último
  fs.writeFileSync(path.join(dir, 'segmentos_' + n.replace('segmentos_', '') + '.gif'), gif(fr, d));
}
console.log('segmentos: ' + names.length + ' GIFs e a tira em out/');
