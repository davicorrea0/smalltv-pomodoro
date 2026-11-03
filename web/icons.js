// Ícones do painel em pixel art, grade 16x16. Cada desenho é montado por formas (disco, retângulo, triângulo...)
// e o contorno de 1 px sai sozinho em volta da forma, então contorno e miolo vêm do mesmo desenho.
// Rode `node web/icons.js`: reescreve o bloco de ícones de web/index.html (sprite SVG de <symbol>, usados com
// <use>) e gera web/out/icones.html (folha de conferência, nos dois estados).
// (b azul e y amarelo: toques de cor da paleta)
// Camadas (classe no SVG): @ contorno (sempre) | o miolo, só ativo (.f) | h brilho, só ativo (.w)
//   d detalhe: linha no inativo, creme no ativo (.k) | x creme só no ativo (.c) | g folha verde (.v)
// Cores (CSS no painel): inativo = só linhas na cor secundária; ativo (.on) = vermelho do tomate, contorno mais
// escuro, detalhes em creme, folha verde.
const fs = require('fs'), path = require('path');
const N = 16, K = (x, y) => x + ',' + y;
class G {
  constructor() { this.m = new Set(); this.c = {} }
  in(x, y) { return this.m.has(K(x, y)) }
  add(f) { for (let y = 0; y < N; y++) for (let x = 0; x < N; x++) if (f(x + .5, y + .5, x, y)) this.m.add(K(x, y)); return this }
  del(f) { for (let y = 0; y < N; y++) for (let x = 0; x < N; x++) if (f(x + .5, y + .5, x, y)) this.m.delete(K(x, y)); return this }
  put(ch, cells) { for (const [x, y] of cells) if (x >= 0 && y >= 0 && x < N && y < N) this.c[K(x, y)] = ch; return this }
  rect(ch, x0, y0, x1, y1) { const a = []; for (let y = y0; y <= y1; y++) for (let x = x0; x <= x1; x++) a.push([x, y]); return this.put(ch, a) }
  holes() {  // células vazias fechadas (não alcançam a borda): o furo
    const seen = new Set(), st = [];
    for (let i = 0; i < N; i++) for (const [x, y] of [[i, 0], [i, N - 1], [0, i], [N - 1, i]]) if (!this.in(x, y) && !seen.has(K(x, y))) { seen.add(K(x, y)); st.push([x, y]) }
    while (st.length) { const [x, y] = st.pop(); for (const [a, b] of [[1, 0], [-1, 0], [0, 1], [0, -1]]) { const X = x + a, Y = y + b, k = K(X, Y); if (X >= 0 && Y >= 0 && X < N && Y < N && !this.in(X, Y) && !seen.has(k)) { seen.add(k); st.push([X, Y]) } } }
    const h = new Set(); for (let y = 0; y < N; y++) for (let x = 0; x < N; x++) if (!this.in(x, y) && !seen.has(K(x, y))) h.add(K(x, y));
    return h;
  }
  rows() {
    const r = [], hole = this.holes();
    for (let y = 0; y < N; y++) {
      let s = '';
      for (let x = 0; x < N; x++) {
        const k = K(x, y); let ch = this.c[k];
        if (!ch) { if (this.m.has(k)) ch = [[1, 0], [-1, 0], [0, 1], [0, -1]].some(([a, b]) => hole.has(K(x + a, y + b))) ? '@' : 'o'; else if (hole.has(k)) ch = '.'; else ch = [[1, 0], [-1, 0], [0, 1], [0, -1]].some(([a, b]) => this.in(x + a, y + b)) ? '@' : '.' }
        s += ch;
      }
      r.push(s);
    }
    return r;
  }
}
const disc = (cx, cy, r) => (x, y) => (x - cx) ** 2 + (y - cy) ** 2 <= r * r;
const ell = (cx, cy, rx, ry) => (x, y) => ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2 <= 1;
const box = (x0, y0, x1, y1) => (x, y, X, Y) => X >= x0 && X <= x1 && Y >= y0 && Y <= y1;
const or = (...f) => (...a) => f.some(g => g(...a));
const lit = rows => { const g = new G(); rows.forEach((r, y) => [...r].forEach((c, x) => { if (c == 'X') g.m.add(K(x, y)) })); return g };
const inSet = cells => (x, y, X, Y) => cells.some(([a, b]) => a == X && b == Y);

const I = {
  relogio: () => {
    const g = new G().add(disc(8, 8, 7.3));
    g.put('h', [[3, 4], [3, 5], [4, 3], [5, 3], [3, 6]]);
    g.put('x', [[7, 2], [8, 2], [7, 13], [8, 13], [2, 7], [2, 8], [13, 7], [13, 8]]);
    g.rect('d', 7, 4, 7, 8).rect('d', 7, 8, 11, 8);
    return g;
  },
  foco: () => {
    const g = new G().add(ell(8, 9.6, 6.9, 5.9));
    g.put('h', [[3, 6], [3, 7], [4, 6], [4, 5], [5, 5]]);
    g.put('g', [[7, 0], [8, 0], [5, 1], [6, 1], [7, 1], [8, 1], [9, 1], [10, 1], [3, 2], [4, 2], [5, 2], [7, 2], [8, 2], [10, 2], [11, 2], [12, 2]]);
    return g;
  },
  fotos: () => {
    const g = new G().add(box(1, 2, 14, 13)).del((x, y, X, Y) => (X == 1 || X == 14) && (Y == 2 || Y == 13));
    g.rect('d', 10, 4, 11, 5);
    for (let y = 6; y <= 12; y++) for (let x = 1; x <= 14; x++) { const a = Math.abs(x - 5) <= (y - 6) * .8 && y >= 7, b = y >= 9 && Math.abs(x - 11) <= (y - 8) * .8; if (a || b) g.put('d', [[x, y]]) }
    return g;
  },
  ajustes: () => lit([  // engrenagem (opção A)
    '................', '......XXXX......', '......XXXX......', '..XX.XXXXXX.XX..', '..XXXXXXXXXXXX..', '...XXXXXXXXXX...', '.XXXXXX..XXXXXX.', '.XXXXX....XXXXX.',
    '.XXXXX....XXXXX.', '.XXXXXX..XXXXXX.', '...XXXXXXXXXX...', '..XXXXXXXXXXXX..', '..XX.XXXXXX.XX..', '......XXXX......', '......XXXX......', '................']),
  zerar: () => {
    const g = new G().add((x, y) => {
      const dx = x - 8, dy = y - 8.6, r = Math.hypot(dx, dy);
      if (r < 4.6 || r > 6.5) return false;
      const a = Math.atan2(dy, dx) * 180 / Math.PI;
      return !(a > -80 && a < -15);
    });
    const head = [];
    for (let x = 8; x <= 13; x++) { const h = x <= 9 ? [1, 6] : x <= 11 ? [2, 5] : [3, 4]; for (let y = h[0]; y <= h[1]; y++) head.push([x, y]) }
    g.add(inSet(head));
    return g;
  },
  tocar: () => new G().add((x, y, X) => X >= 3 && X <= 12 && Math.abs(y - 8) <= (13.4 - x) * .68),
  pausar: () => new G().add(or(box(3, 2, 5, 13), box(10, 2, 12, 13))),
  pular: () => new G().add(or((x, y, X) => X >= 1 && X <= 9 && Math.abs(y - 8) <= (10.4 - x) * .6, box(11, 2, 13, 13))),
  ler: () => {
    const yt = X => [2, 2, 3, 3, 4, 4, 4][Math.min(X, 15 - X) - 1];
    const g = new G().add((x, y, X, Y) => { const k = Math.min(X, 15 - X); if (k < 1 || k > 6) return false; return Y >= yt(X) && Y <= yt(X) + 9 });
    for (const X of [2, 3, 4, 5]) for (const y of [6, 8, 10]) g.put('x', [[X, y], [15 - X, y]]);
    return g;
  },
  codar: () => {
    const g = new G().add(or(box(2, 1, 13, 9), box(1, 12, 14, 13)));
    g.rect('x', 3, 2, 12, 8);
    g.put('@', [[5, 3], [4, 4], [3, 5], [4, 6], [5, 7], [8, 3], [8, 4], [7, 5], [7, 6], [7, 7], [10, 3], [11, 4], [12, 5], [11, 6], [10, 7]]);
    return g;
  },
  estudar: () => {
    const g = new G().add(box(3, 1, 13, 14));
    for (const y of [3, 6, 9, 12]) g.rect('d', 1, y, 4, y);
    for (const y of [4, 7, 10]) g.rect('d', 7, y, 11, y);
    return g;
  },
  tarefas: () => {
    const g = new G().add(or(box(2, 3, 13, 14), box(5, 1, 10, 3))).rect('x', 6, 2, 9, 2);
    for (const y of [6, 9, 12]) { g.rect('d', 4, y, 5, y); g.rect('d', 7, y, 11, y) }
    return g;
  },
  desenhar: () => {
    const g = new G().add(ell(7, 9.5, 6.2, 5.2)).del(disc(10.3, 13.2, 1.9));
    g.rect('y', 2, 9, 3, 10).rect('b', 4, 6, 5, 7).rect('g', 7, 8, 8, 9).rect('d', 3, 12, 4, 13);
    const br = [];
    for (let k = 0; k < 6; k++) br.push([14 - k, 1 + k], [13 - k, 1 + k]);
    g.add(inSet(br)); g.put('d', br); g.put('y', [[9, 6], [8, 6]]);
    return g;
  },
};
// opção B de "ajustes" (controles deslizantes), só na folha
const EXTRA = { ajustesb: () => {
  const g = new G(), kn = [[9, 2], [3, 6], [7, 10]];
  g.add((x, y, X, Y) => kn.some(([a, b]) => X >= a && X < a + 4 && Y >= b && Y < b + 4));
  for (const y of [3, 7, 11]) for (let x = 1; x <= 14; x++) if (!g.in(x, y) && !g.in(x, y + 1)) g.put('d', [[x, y], [x, y + 1]]);
  return g } };
Object.assign(I, EXTRA);
const ids = Object.keys(I).filter(k => !EXTRA[k]), sheetIds = Object.keys(I);
function rects(rows, set) {
  const runs = [];
  rows.forEach((r, y) => { for (let x = 0; x < N;) { if (!set.includes(r[x])) { x++; continue } let e = x; while (e < N && set.includes(r[e])) e++; runs.push({ x, y, w: e - x, h: 1 }); x = e } });
  const out = [];
  for (const r of runs) { const p = out.find(o => o.x == r.x && o.w == r.w && o.y + o.h == r.y); p ? p.h++ : out.push(r) }
  return out.map(r => `M${r.x} ${r.y}h${r.w}v${r.h}h-${r.w}z`).join('');
}
const sym = k => {
  const rows = I[k]().rows(), P = (cls, set) => { const d = rects(rows, set); return d ? `<path${cls ? ` class="${cls}"` : ''} d="${d}"/>` : '' };
  if (process.argv[2] == '-v') console.log(k + '\n' + rows.join('\n'));
  return `<symbol id="i-${k}" viewBox="0 0 16 16">${P('f', 'o') + P('w', 'h') + P('c', 'x') + P('k', 'd') + P('v', 'g') + P('b', 'b') + P('y', 'y') + P('', '@')}</symbol>`;
};
const all = ids.map(sym);
const block = `<!--icons-->\n<svg width="0" height="0" style="position:absolute" aria-hidden="true">${all.join('')}</svg>\n<!--/icons-->`;
const sheetBlock = `<svg width="0" height="0" style="position:absolute" aria-hidden="true">${sheetIds.map(sym).join("")}</svg>`;
const f = path.join(__dirname, 'index.html');
let h = fs.readFileSync(f, 'utf8');
if (/\n<!--icons-->/.test(h)) h = h.replace(/\n<!--icons-->[\s\S]*?<!--\/icons-->/, () => '\n' + block);
else h = h.replace('<nav>', () => block + '\n<nav>');
fs.writeFileSync(f, h);
// folha de conferência: grande (4x) nos dois estados lado a lado e no tamanho real do painel (2x)
const cell = (id, cls, px) => `<svg class="pi ${cls}" style="width:${px}px;height:${px}px"><use href="#i-${id}"/></svg>`;
const css = h.match(/<style[^>]*>([\s\S]*?)<\/style>/)[1];
fs.mkdirSync(path.join(__dirname, 'out'), { recursive: true });
fs.writeFileSync(path.join(__dirname, 'out', 'icones.html'), `<!doctype html><meta charset=utf-8><style>${css}
body{padding:24px;display:block}.g{display:grid;grid-template-columns:repeat(5,1fr);gap:14px}.c{background:var(--card);border-radius:14px;padding:14px;display:flex;flex-direction:column;align-items:center;gap:10px}.p{display:flex;gap:18px;align-items:center}.c small{color:var(--mut)}h3{color:var(--mut);font-weight:500;margin:22px 0 10px}.real{display:flex;gap:22px;flex-wrap:wrap;background:var(--card);border-radius:14px;padding:16px}.real span{display:flex;gap:8px;align-items:center}.off{color:var(--mut)}</style>
${sheetBlock}<h3>Contorno (inativo) e preenchido (ativo), 4x</h3><div class=g>${sheetIds.map(i => `<div class=c><div class=p>${cell(i, 'off', 64)}${cell(i, 'on', 64)}</div><small>${i}</small></div>`).join('')}</div>
<h3>No tamanho do painel (2x, 32 px)</h3><div class=real>${sheetIds.map(i => `<span>${cell(i, 'off', 32)}${cell(i, 'on', 32)}</span>`).join('')}</div>`);
console.log('ok', all.join('').length, 'bytes de sprite');
