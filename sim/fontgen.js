// Gera as tabelas de contorno de smalltv-pomodoro/fonts.cpp a partir de um TTF (dígitos 0-9 e ':').
// uso: npm i opentype.js@1.3.4 && node fontgen.js <ttf> <NOME> [tolerância=10]
//      com PREVIEW=arquivo.png desenha "14:37" para conferir o preenchimento (regra não-zero)
//      com TEXTO=1 gera texto (A-Z, a-z, 0-9, espaço, ':', '-' e '%'): a célula de 720 vai do topo das
//      maiúsculas ao fim das descendentes (g, p, y), os avanços são os da fonte e sai também NOME_CHARS
const opentype = require('opentype.js'), fs = require('fs'), zlib = require('zlib');
const [, , file, NAME, tolArg] = process.argv;
const font = opentype.loadSync(file), CH = 720, tol = +(tolArg || 10);
const TEXT = !!process.env.TEXTO;
const chars = TEXT ? ' -%:0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz' : '0123456789:';
let top = -1e9, bot = 1e9;
for (const c of TEXT ? 'Hdgpy' : '0123456789') { const b = font.charToGlyph(c).getBoundingBox(); top = Math.max(top, b.y2); bot = Math.min(bot, b.y1); }
const k = CH / (top - bot);
const X = x => Math.round(x * k), Y = y => Math.round((top - y) * k);

function flatten(cmds) {
  const cs = []; let cur = null, px = 0, py = 0;
  const add = (x, y) => { const p = [X(x), Y(y)]; const l = cur[cur.length - 1]; if (!l || l[0] != p[0] || l[1] != p[1]) cur.push(p); };
  const steps = (len) => Math.max(1, Math.ceil(Math.sqrt(len * k / tol)));
  for (const c of cmds) {
    if (c.type == 'M') { cur = []; cs.push(cur); add(c.x, c.y); }
    else if (c.type == 'L') add(c.x, c.y);
    else if (c.type == 'Q') { const n = steps(Math.hypot(c.x1 - px, c.y1 - py) + Math.hypot(c.x - c.x1, c.y - c.y1));
      for (let i = 1; i <= n; i++) { const t = i / n, u = 1 - t; add(u * u * px + 2 * u * t * c.x1 + t * t * c.x, u * u * py + 2 * u * t * c.y1 + t * t * c.y); } }
    else if (c.type == 'C') { const n = steps(Math.hypot(c.x1 - px, c.y1 - py) + Math.hypot(c.x2 - c.x1, c.y2 - c.y1) + Math.hypot(c.x - c.x2, c.y - c.y2));
      for (let i = 1; i <= n; i++) { const t = i / n, u = 1 - t; add(u*u*u*px + 3*u*u*t*c.x1 + 3*u*t*t*c.x2 + t*t*t*c.x, u*u*u*py + 3*u*u*t*c.y1 + 3*u*t*t*c.y2 + t*t*t*c.y); } }
    else if (c.type == 'Z') { if (cur.length > 1 && cur[0][0] == cur[cur.length-1][0] && cur[0][1] == cur[cur.length-1][1]) cur.pop(); continue; }
    px = c.x; py = c.y;
  }
  return cs.filter(c => c.length > 2);
}

const pts = [], cend = [], first = [], adv = [];
for (const ch of chars) {
  const g = font.charToGlyph(ch);
  first.push(cend.length);
  for (const c of flatten(g.getPath(0, 0, font.unitsPerEm).commands.map(c => ({ ...c, y: c.y === undefined ? c.y : -c.y, y1: c.y1 === undefined ? c.y1 : -c.y1, y2: c.y2 === undefined ? c.y2 : -c.y2 })))) {
    pts.push(...c); cend.push(pts.length);
  }
  adv.push(X(g.advanceWidth));
}
first.push(cend.length);
// dígitos tabulares (o relógio não balança): célula = maior avanço de 2-9, glifo centralizado
const tab = Math.max(...adv.slice(2, 10));
if (!TEXT) for (let d = 0; d < 10; d++) { const dx = Math.round((tab - adv[d]) / 2); for (let c = first[d]; c < first[d + 1]; c++) for (let i = c ? cend[c - 1] : 0; i < cend[c]; i++) pts[i][0] += dx; adv[d] = tab; }
// getPath já usa y para baixo com escala unitsPerEm/unitsPerEm = 1: desfeito acima (-y) para voltar ao sistema da fonte

const out = [];
out.push(`static const int16_t ${NAME}_PT[${pts.length}][2] PROGMEM = {`);
for (let i = 0; i < pts.length; i += 10) out.push('  ' + pts.slice(i, i + 10).map(p => `{${p[0]}, ${p[1]}}`).join(', ') + ',');
out.push('};');
out.push(`static const uint16_t ${NAME}_CEND[${cend.length}] PROGMEM = {${cend.join(', ')}};`);
out.push(`static const uint8_t ${NAME}_GLYPH[${first.length}] PROGMEM = {${first.join(', ')}};`);
out.push(`static const uint16_t ${NAME}_ADV[${adv.length}] PROGMEM = {${adv.join(', ')}};`);
if (TEXT) out.push(`static const char ${NAME}_CHARS[] PROGMEM = "${chars}";`);
console.log(out.join('\n'));
console.error(`${NAME}: ${pts.length} pontos, ${cend.length} contornos, ${pts.length * 4 + cend.length * 2 + 34} bytes`);

if (process.env.PREVIEW) {  // "14:37" com 96 px de altura, preenchimento par-ímpar
  const H = 96, s = '14:37', W = 240, img = Buffer.alloc(W * 120);
  let ox = 4;
  for (const ch of s) {
    const gi = chars.indexOf(ch);
    for (let y = 0; y < H; y++) {
      const sy = (y + 0.5) * CH / H, xs = [];
      for (let c = first[gi]; c < first[gi + 1]; c++) {
        const s0 = c ? cend[c - 1] : 0, e = cend[c];
        for (let i = s0; i < e; i++) { const a = pts[i], b = pts[i + 1 < e ? i + 1 : s0];
          if ((a[1] <= sy && sy < b[1]) || (b[1] <= sy && sy < a[1])) xs.push([a[0] + (sy - a[1]) * (b[0] - a[0]) / (b[1] - a[1]), a[1] < b[1] ? 1 : -1]); }
      }
      xs.sort((a, b) => a[0] - b[0]);
      let wn = 0;
      for (let i = 0; i + 1 < xs.length; i++) { wn += xs[i][1]; if (wn) for (let x = Math.round(ox + xs[i][0] * H / CH); x < Math.round(ox + xs[i + 1][0] * H / CH); x++) if (x >= 0 && x < W) img[(y + 12) * W + x] = 255; }
    }
    ox += adv[gi] * H / CH;
  }
  const raw = Buffer.alloc((W + 1) * 120); for (let y = 0; y < 120; y++) img.copy(raw, y * (W + 1) + 1, y * W, y * W + W);
  const T = []; for (let n = 0; n < 256; n++) { let c = n; for (let j = 0; j < 8; j++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; T[n] = c >>> 0; }
  const crc = b => { let c = ~0; for (const x of b) c = T[(c ^ x) & 255] ^ (c >>> 8); return (~c) >>> 0; };
  const chunk = (t, d) => { const l = Buffer.alloc(4); l.writeUInt32BE(d.length); const td = Buffer.concat([Buffer.from(t), d]); const c = Buffer.alloc(4); c.writeUInt32BE(crc(td)); return Buffer.concat([l, td, c]); };
  const ih = Buffer.alloc(13); ih.writeUInt32BE(W, 0); ih.writeUInt32BE(120, 4); ih[8] = 8; ih[9] = 0;
  fs.writeFileSync(process.env.PREVIEW, Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', ih), chunk('IDAT', zlib.deflateSync(raw)), chunk('IEND', Buffer.alloc(0))]));
}
