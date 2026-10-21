// Gera o catálogo de mostradores instaláveis a partir de faces/*.json (rode depois do simulador):
//  - faces/<id>.png: prévia 240x240 renderizada pelo simulador (out/pack-<id>.raw)
//  - faces/catalog.json: lista lida pelo site (galeria) e pelo painel do relógio (instalar)
const fs = require('fs'), zlib = require('zlib'), path = require('path');
const root = path.join(__dirname, '..'), dir = path.join(root, 'faces');
if (!fs.existsSync(dir)) process.exit(0);

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
// mesmo id que o firmware calcula (facepack.cpp: safeId)
const safeId = s => {
  let o = '';
  for (let c of String(s || '')) {
    c = c.toLowerCase();
    if (/[a-z0-9_-]/.test(c)) o += c; else if (c === ' ' && o && !o.endsWith('-')) o += '-';
    if (o.length >= 23) break;
  }
  return o || 'pacote';
};

const list = [];
for (const f of fs.readdirSync(dir).filter(f => f.endsWith('.json') && f !== 'catalog.json').sort()) {
  const j = JSON.parse(fs.readFileSync(path.join(dir, f), 'utf8'));
  const id = safeId(j.id || j.name);
  const raw = path.join(__dirname, 'out', `pack-${id}.raw`);
  if (fs.existsSync(raw)) fs.writeFileSync(path.join(dir, `${id}.png`), png(fs.readFileSync(raw)));
  list.push({ id, name: j.name || id, author: j.author || '', desc: j.desc || '', file: f, preview: `${id}.png`,
    size: fs.statSync(path.join(dir, f)).size });
}
fs.writeFileSync(path.join(dir, 'catalog.json'), JSON.stringify({ format: 1, faces: list }, null, 2) + '\n');
console.log(`catálogo: ${list.length} pacote(s)`);
