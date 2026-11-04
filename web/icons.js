// Ícones do painel: vetoriais em contorno arredondado (viewBox 24x24, traço 2, pontas e junções redondas).
// Desenhos próprios, na linguagem de traço do Lucide/Tabler (ambos MIT; nenhum path foi copiado).
// Rode `node web/icons.js`: reescreve o bloco de ícones de web/index.html (sprite SVG de <symbol>, usados com <use>)
// e gera web/out/icones.html (folha de conferência, inativo e ativo, 1x e 2x).
// Classes nas formas: a = área fechada, preenchida só no estado ativo (var --pa); s = sólido sempre (var currentColor).
// Cores e traço vêm do CSS do painel (.pi): inativo = só linha na cor do texto secundário; ativo (.on) = vermelho do tomate.
const fs = require('fs'), path = require('path');
const A = d => `<path class="a" d="${d}"/>`, L = d => `<path d="${d}"/>`, S = (x, y, r = 1.1) => `<circle class="s" cx="${x}" cy="${y}" r="${r}"/>`;
const I = {
  relogio: A('M12 3a9 9 0 1 0 0 18 9 9 0 0 0 0-18z') + L('M12 7.5V12l3 2'),
  foco: A('M12 8.5c5 0 8.5 2.8 8.5 6.3S16.8 21 12 21s-8.5-2.7-8.5-6.2S7 8.5 12 8.5z') + A('M12 8.5C11 6.5 9 5.5 6.5 6c1 2 3 2.8 5.5 2.5z') + A('M12 8.5c1-2 3-3 5.5-2.5-1 2-3 2.8-5.5 2.5z') + L('M12 8.5V4'),
  fotos: A('M5 4h14a2 2 0 0 1 2 2v12a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V6a2 2 0 0 1 2-2z') + L('M3.5 17.5l5-5 4 4 3-3 5 5') + S(8.5, 9),
  ajustes: L('M4 6h8') + L('M18 6h2') + A('M15 3.8a2.2 2.2 0 1 0 0 4.4 2.2 2.2 0 0 0 0-4.4z') +
    L('M4 12h2') + L('M12 12h8') + A('M9 9.8a2.2 2.2 0 1 0 0 4.4 2.2 2.2 0 0 0 0-4.4z') +
    L('M4 18h9') + L('M19 18h1') + A('M16 15.8a2.2 2.2 0 1 0 0 4.4 2.2 2.2 0 0 0 0-4.4z'),
  zerar: L('M4.5 12a7.5 7.5 0 1 0 2.2-5.3L4.5 9') + L('M4.5 4.5V9H9'),
  tocar: A('M8 5.5v13l10.5-6.5z'),
  pausar: A('M7.5 5h2a1 1 0 0 1 1 1v12a1 1 0 0 1-1 1h-2a1 1 0 0 1-1-1V6a1 1 0 0 1 1-1z') + A('M14.5 5h2a1 1 0 0 1 1 1v12a1 1 0 0 1-1 1h-2a1 1 0 0 1-1-1V6a1 1 0 0 1 1-1z'),
  pular: A('M5.5 6.5v11l8.5-5.5z') + L('M18.5 6v12'),
  ler: A('M12 6.5C10 5 7 4.5 3.5 5v12.5c3.5-.4 6.5.1 8.5 1.8 2-1.7 5-2.2 8.5-1.8V5C17 4.5 14 5 12 6.5z') + L('M12 6.5v12.8'),
  codar: A('M5 4h14a2 2 0 0 1 2 2v9a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V6a2 2 0 0 1 2-2z') + L('M9 8.5l-2.5 2.5L9 13.5') + L('M15 8.5l2.5 2.5-2.5 2.5') + L('M8 20.5h8'),
  estudar: A('M8 3h6l5 5v11a2 2 0 0 1-2 2H8a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2z') + L('M14 3v5h5') + L('M9.5 13h5') + L('M9.5 17h5'),
  tarefas: A('M7 4.5h10a2 2 0 0 1 2 2V19a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6.5a2 2 0 0 1 2-2z') + A('M10 3h4a1 1 0 0 1 1 1v1.5a1 1 0 0 1-1 1h-4a1 1 0 0 1-1-1V4a1 1 0 0 1 1-1z') + L('M9 14l2.5 2.5L16 12'),
  desenhar: A('M12 3a9 9 0 0 0 0 18c1.5 0 2-1 2-2 0-1.2-.7-1.7-.7-2.7 0-1 .8-1.8 1.8-1.8H17a4 4 0 0 0 4-4c0-4.4-4-7.5-9-7.5z') + S(7.5, 11.5) + S(9.5, 7.5) + S(14.5, 7.5),
};
const ids = Object.keys(I);
const sym = k => `<symbol id="i-${k}" viewBox="0 0 24 24">${I[k]}</symbol>`;
const sprite = `<svg width="0" height="0" style="position:absolute" aria-hidden="true">${ids.map(sym).join('')}</svg>`;
const block = `<!--icons-->\n${sprite}\n<!--/icons-->`;
const f = path.join(__dirname, 'index.html');
let h = fs.readFileSync(f, 'utf8');
if (/\n<!--icons-->/.test(h)) h = h.replace(/\n<!--icons-->[\s\S]*?<!--\/icons-->/, () => '\n' + block);
else h = h.replace('<nav>', () => block + '\n<nav>');
fs.writeFileSync(f, h);
const css = h.match(/<style[^>]*>([\s\S]*?)<\/style>/)[1];
const cell = (id, cls, px) => `<svg class="pi ${cls}" style="width:${px}px;height:${px}px"><use href="#i-${id}"/></svg>`;
fs.mkdirSync(path.join(__dirname, 'out'), { recursive: true });
fs.writeFileSync(path.join(__dirname, 'out', 'icones.html'), `<!doctype html><meta charset=utf-8><style>${css}
body{padding:24px;display:block}.g{display:grid;grid-template-columns:repeat(4,1fr);gap:14px}.c{background:var(--card);border-radius:14px;padding:14px;display:flex;flex-direction:column;align-items:center;gap:10px}.p{display:flex;gap:18px}.real{display:flex;flex-wrap:wrap;gap:16px;background:var(--card);padding:16px;border-radius:14px}.real span{display:flex;gap:6px}h3{color:var(--mut);font-weight:500}</style>
${sprite}<h3>Inativo e ativo, 64 px</h3><div class=g>${ids.map(i => `<div class=c><div class=p>${cell(i, 'off', 64)}${cell(i, 'on', 64)}</div><small>${i}</small></div>`).join('')}</div>
<h3>Tamanho do painel (26 px)</h3><div class=real>${ids.map(i => `<span>${cell(i, 'off', 26)}${cell(i, 'on', 26)}</span>`).join('')}</div>`);
console.log('ok', sprite.length, 'bytes de sprite');
