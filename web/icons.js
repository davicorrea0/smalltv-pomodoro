// Ícones do painel em pixel art (grade 12x12). Edite as grades abaixo e rode `node web/icons.js`: ele reescreve
// o bloco <!--icons-->...<!--/icons--> de web/index.html (sprite SVG de símbolos, usados com <use>) e gera
// web/out/icones.html (folha de conferência, nos dois estados).
// Legenda: @ contorno (sempre) | o miolo (só no estado ativo) | d detalhe (contorno no inativo, escuro no ativo)
//          g folha verde | h brilho | . vazio. Os símbolos "s-" (controles) são sólidos: só @.
// Estados (CSS no painel): inativo = só linhas na cor secundária; ativo = miolo na cor da fase (--ac), contorno mais escuro.
const fs = require('fs'), path = require('path');
const I = {
  relogio: ['...@@@@@@...', '.@@oooooo@@.', '.@oooddooo@.', '@ooooddoooo@', '@ooooddoooo@', '@ooooddddoo@', '@oooooooooo@', '@oooooooooo@', '@oooooooooo@', '.@oooooooo@.', '.@@oooooo@@.', '...@@@@@@...'],
  foco: ['.....gg.....', '...gggggg...', '....gggg....', '..@@@gg@@@..', '.@oooooooo@.', '@ohhooooooo@', '@ohoooooooo@', '@oooooooooo@', '@oooooooooo@', '.@oooooooo@.', '..@oooooo@..', '...@@@@@@...'],
  fotos: ['............', '.@@@@@@@@@@.', '@oooooooooo@', '@ooooooddoo@', '@ooooooddoo@', '@ooodooodoo@', '@oodddodddo@', '@oddddddddo@', '@oooooooooo@', '@oooooooooo@', '.@@@@@@@@@@.', '............'],
  ajustes: ['.....@@.....', '.@@.@oo@.@@.', '.@o@@oo@@o@.', '..@oooooo@..', '.@oo@@@@oo@.', '@oo@....@oo@', '@oo@....@oo@', '.@oo@@@@oo@.', '..@oooooo@..', '.@o@@oo@@o@.', '.@@.@oo@.@@.', '.....@@.....'],
};
const S = {
  zerar: ['......@.....', '...@@@@@....', '..@@...@....', '.@@......@@.', '.@........@.', '.@........@.', '.@........@.', '.@........@.', '.@@......@@.', '..@@....@@..', '...@@@@@@...', '............'],
  tocar: ['............', '..@@........', '..@@@@......', '..@@@@......', '..@@@@@@....', '..@@@@@@@@..', '..@@@@@@@@..', '..@@@@@@....', '..@@@@@@....', '..@@@@......', '..@@........', '............'],
  pausar: ['............', '...@@..@@...', '...@@..@@...', '...@@..@@...', '...@@..@@...', '...@@..@@...', '...@@..@@...', '...@@..@@...', '...@@..@@...', '...@@..@@...', '...@@..@@...', '............'],
  pular: ['............', '............', '.@@.......@@', '.@@@@.....@@', '.@@@@@@...@@', '.@@@@@@@@.@@', '.@@@@@@@@.@@', '.@@@@@@...@@', '.@@@@.....@@', '.@@.......@@', '............', '............'],
};
const A = {  // atividades do foco (aparecem ao lado da lista, no estado ativo)
  ler: ['............', '............', '.@@@@@@@@@@.', '@oooo@@oooo@', '@oddo@@oddo@', '@oooo@@oooo@', '@oddo@@oddo@', '@oooo@@oooo@', '.@@@@@@@@@@.', '............', '............', '............'],
  estudar: ['............', '..@@@@@@@@..', '..@oooooo@..', '.@@oddddo@..', '..@oooooo@..', '.@@oddddo@..', '..@oooooo@..', '.@@oddddo@..', '..@oooooo@..', '..@oooooo@..', '..@@@@@@@@..', '............'],
  codar: ['............', '.@@@@@@@@@@.', '.@oooooooo@.', '.@oddooooo@.', '.@ooooddoo@.', '.@oddooooo@.', '.@oooooooo@.', '.@@@@@@@@@@.', '@oooooooooo@', '@@@@@@@@@@@@', '............', '............'],
  tarefas: ['....@@@@....', '.@@@@@@@@@@.', '.@oooooooo@.', '.@ododdddo@.', '.@oooooooo@.', '.@ododdddo@.', '.@oooooooo@.', '.@ododdddo@.', '.@oooooooo@.', '.@oooooooo@.', '.@@@@@@@@@@.', '............'],
  desenhar: ['............', '...@@@@@@...', '.@@oooooo@@.', '@ooddoooooo@', '@ooddooddoo@', '@ooooooddoo@', '@oooooooooo@', '.@oooooooo@.', '.@@oooo@@@@.', '...@@@@@@...', '............', '............'],
};
// converte uma grade em retângulos (corridas horizontais, fundidas na vertical quando iguais)
function rects(rows, set) {
  const runs = [];
  rows.forEach((r, y) => { for (let x = 0; x < 12;) { if (!set.includes(r[x])) { x++; continue } let e = x; while (e < 12 && set.includes(r[e])) e++; runs.push({ x, y, w: e - x, h: 1 }); x = e } });
  const out = [];
  for (const r of runs) { const p = out.find(o => o.x == r.x && o.w == r.w && o.y + o.h == r.y); p ? p.h++ : out.push(r) }
  return out.map(r => `M${r.x} ${r.y}h${r.w}v${r.h}h-${r.w}z`).join('');
}
function sym(id, rows, solid) {
  rows.forEach((r, i) => { if (r.length != 12) throw new Error(`${id}: linha ${i} tem ${r.length} colunas (${r})`) });
  if (rows.length != 12) throw new Error(id + ': precisa de 12 linhas');
  const P = (cls, set) => { const d = rects(rows, set); return d ? `<path${cls ? ` class="${cls}"` : ''} d="${d}"/>` : '' };
  return `<symbol id="${id}" viewBox="0 0 12 12">${solid ? P('', '@') : P('f', 'o') + P('x', 'h') + P('k', 'd') + P('v', 'g') + P('', '@')}</symbol>`;
}
const all = [...Object.entries(I).map(([k, v]) => sym('i-' + k, v)), ...Object.entries(S).map(([k, v]) => sym('s-' + k, v, true)), ...Object.entries(A).map(([k, v]) => sym('a-' + k, v))];
const block = `<!--icons-->\n<svg width="0" height="0" style="position:absolute" aria-hidden="true">${all.join('')}</svg>\n<!--/icons-->`;
const f = path.join(__dirname, 'index.html');
let h = fs.readFileSync(f, 'utf8');
if (/<!--icons-->/.test(h)) h = h.replace(/<!--icons-->[\s\S]*?<!--\/icons-->/, () => block);
else h = h.replace('<nav>', () => block + '\n<nav>');
fs.writeFileSync(f, h);
// folha de conferência
const cell = (id, cls, label) => `<div class="c ${cls}"><svg class="pi" viewBox="0 0 12 12"><use href="#${id}"/></svg><small>${label}</small></div>`;
const ids = [...Object.keys(I).map(k => 'i-' + k), ...Object.keys(S).map(k => 's-' + k), ...Object.keys(A).map(k => 'a-' + k)];
const css = h.match(/<style>([\s\S]*?)<\/style>/)[1];
fs.mkdirSync(path.join(__dirname, 'out'), { recursive: true });
fs.writeFileSync(path.join(__dirname, 'out', 'icones.html'), `<!doctype html><meta charset=utf-8><style>${css}
body{padding:24px}.row{display:flex;gap:16px;flex-wrap:wrap;margin:12px 0}.c{display:flex;flex-direction:column;align-items:center;gap:6px;background:var(--card);border-radius:12px;padding:14px 18px;min-width:92px}.c svg{width:48px;height:48px}.c small{color:var(--mut)}
h3{margin:22px 0 0;color:var(--mut);font-weight:500}.c.on .pi{--pf:var(--ac);--pk:#2a0c0a;--pv:#60c454;--px:#ff9178;--po:color-mix(in srgb,var(--ac) 60%,#000);color:var(--ac)}.c.off{color:var(--mut)}</style>
${block}<h3>Inativo (contorno)</h3><div class=row>${ids.map(i => cell(i, 'off', i)).join('')}</div><h3>Ativo (preenchido)</h3><div class=row>${ids.map(i => cell(i, 'on', i)).join('')}</div>`);
console.log('ok', all.join('').length, 'bytes de sprite');
