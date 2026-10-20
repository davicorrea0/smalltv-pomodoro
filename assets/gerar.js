// Gera smalltv-pomodoro/assets.h a partir dos sprites em assets/*.txt.
//
// Uso: node assets/gerar.js   (o sim/build.sh já roda este script antes de compilar)
//
// Formato de um sprite (um arquivo por sprite; o nome do arquivo vira o nome do asset):
//
//   # comentário (também no fim de uma linha: "cor k #280e0c   # olhos")
//   paleta tomate          inclui as cores de assets/paletas/tomate.txt (opcional)
//   cor k #280e0c          caractere -> cor (sobrescreve a paleta incluída)
//   ms 200                 duração de cada quadro, para animações (opcional)
//   quadro                 começa um quadro novo (opcional se houver só um)
//   fundo #a6b48c          cor de fundo na folha de prévia (opcional; para sprites de fundo claro)
//   ......gGGg......       uma linha de pixels por linha de texto; '.' = transparente
//
// Todos os quadros têm o mesmo tamanho. Até 15 cores por sprite (4 bits por pixel).
// tomate_feliz.txt vira SPR_TOMATE_FELIZ em C++.

const fs = require('fs'), path = require('path');
const DIR = __dirname, OUT = path.join(DIR, '..', 'smalltv-pomodoro', 'assets.h');

function fail(file, line, msg) {
  console.error(`${path.relative(process.cwd(), file)}:${line}: ${msg}`);
  process.exit(1);
}

// "#e8402f" -> RGB565 (a mesma conta de uiRgb)
function rgb565(file, line, hex) {
  const m = /^#?([0-9a-f]{6})$/i.exec(hex);
  if (!m) fail(file, line, `cor inválida "${hex}" (use #rrggbb)`);
  const v = parseInt(m[1], 16), r = v >> 16, g = (v >> 8) & 255, b = v & 255;
  return ((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3);
}

function readPalette(name, into, from, fromLine) {
  const file = path.join(DIR, 'paletas', name + '.txt');
  if (!fs.existsSync(file)) fail(from, fromLine, `paleta "${name}" não existe (${path.relative(DIR, file)})`);
  fs.readFileSync(file, 'utf8').split(/\r?\n/).forEach((raw, i) => {
    const l = raw.replace(/\s+#\s.*$/, '').trim();  // "# texto" no fim da linha é comentário
    if (!l || l.startsWith('#')) return;
    const m = /^cor\s+(\S)\s+(\S+)$/.exec(l);
    if (!m) fail(file, i + 1, `esperava "cor <caractere> #rrggbb"`);
    into.set(m[1], rgb565(file, i + 1, m[2]));
  });
}

function parse(file) {
  const colors = new Map(), frames = [];
  let ms = 0, bg = -1, cur = null, w = 0;
  fs.readFileSync(file, 'utf8').split(/\r?\n/).forEach((raw, i) => {
    const ln = i + 1, l = raw.replace(/\s+#\s.*$/, '').trim();
    if (!l || l.startsWith('#')) return;
    let m;
    if ((m = /^paleta\s+(\S+)$/.exec(l))) return readPalette(m[1], colors, file, ln);
    if ((m = /^cor\s+(\S)\s+(\S+)$/.exec(l))) {
      if (m[1] === '.') fail(file, ln, `"." é reservado para transparente`);
      return colors.set(m[1], rgb565(file, ln, m[2]));
    }
    if ((m = /^ms\s+(\d+)$/.exec(l))) return (ms = Math.min(+m[1], 65535));
    if ((m = /^fundo\s+(\S+)$/.exec(l))) return (bg = rgb565(file, ln, m[1]));
    if (l === 'quadro') return frames.push((cur = []));
    if (/\s/.test(l)) fail(file, ln, `linha não reconhecida: "${l}"`);
    if (!cur) frames.push((cur = []));
    if (w && l.length !== w) fail(file, ln, `linha com ${l.length} pixels; as anteriores têm ${w}`);
    w = l.length;
    for (const ch of l) if (ch !== '.' && !colors.has(ch)) fail(file, ln, `caractere "${ch}" sem cor (falta "cor ${ch} #rrggbb")`);
    cur.push({ row: l, ln });
  });
  if (!frames.length || !frames[0].length) fail(file, 1, 'nenhum pixel');
  const h = frames[0].length;
  frames.forEach(f => f.length !== h && fail(file, f[0] ? f[0].ln : 1, `quadro com ${f.length} linhas; o primeiro tem ${h}`));
  if (frames.length > 1 && !ms) fail(file, 1, 'animação sem "ms <duração de cada quadro>"');
  if (w > 255 || h > 255 || frames.length > 255) fail(file, 1, 'sprite grande demais (máx. 255x255, 255 quadros)');
  // só as cores realmente usadas entram no sprite, na ordem em que aparecem
  const used = [];
  for (const f of frames) for (const { row } of f) for (const ch of row) if (ch !== '.' && !used.includes(ch)) used.push(ch);
  if (used.length > 15) fail(file, 1, `${used.length} cores; o máximo é 15`);
  const bytes = [];
  for (const f of frames) {
    const idx = [];
    for (const { row } of f) for (const ch of row) idx.push(ch === '.' ? 0 : used.indexOf(ch) + 1);
    for (let k = 0; k < idx.length; k += 2) bytes.push((idx[k] << 4) | (idx[k + 1] || 0));
  }
  return { w, h, frames: frames.length, ms, bg, pal: used.map(ch => colors.get(ch)), bytes };
}

const files = fs.readdirSync(DIR).filter(f => f.endsWith('.txt')).sort();
const hex = (v, n) => '0x' + v.toString(16).padStart(n, '0');
const wrap = (items, per) => items.reduce((o, v, k) => o + (k % per ? ' ' : '\n    ') + v + ',', '');
let out = `// assets.h — GERADO por assets/gerar.js a partir de assets/*.txt. Não edite à mão:
// mude o .txt e rode "node assets/gerar.js" (o sim/build.sh já faz isso).
#pragma once
#include "ui.h"
`;
const names = [];
for (const f of files) {
  const name = f.replace(/\.txt$/, '');
  if (!/^[a-z0-9_]+$/.test(name)) fail(path.join(DIR, f), 1, 'nome de arquivo deve ter só a-z, 0-9 e _');
  const s = parse(path.join(DIR, f)), id = 'SPR_' + name.toUpperCase();
  names.push([name, id, s.bg]);
  out += `
// ${name}: ${s.w}x${s.h}${s.frames > 1 ? `, ${s.frames} quadros de ${s.ms} ms` : ''}
static const uint16_t ${id}_PAL[] PROGMEM = {${s.pal.map(v => hex(v, 4)).join(', ')}};
static const uint8_t ${id}_PX[] PROGMEM = {${wrap(s.bytes.map(v => hex(v, 2)), 16)}
};
static const UiSprite ${id} PROGMEM = {${s.w}, ${s.h}, ${s.frames}, ${s.ms}, ${id}_PAL, ${id}_PX};
`;
}
out += `
// Lista de todos os assets (só para a folha de prévia do simulador; não ocupa a flash do firmware)
#ifdef UI_ASSET_LIST
struct UiAssetEntry { const char *name; const UiSprite *sprite; int32_t bg; };  // bg -1 = fundo padrão
static const UiAssetEntry UI_ASSETS[] = {
${names.map(([n, id, bg]) => `    {"${n}", &${id}, ${bg < 0 ? -1 : hex(bg, 4)}},`).join('\n')}
};
#endif
`;
fs.writeFileSync(OUT, out);
console.log(`${files.length} assets -> ${path.relative(process.cwd(), OUT)}`);
