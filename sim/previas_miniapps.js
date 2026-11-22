'use strict';
const fs = require('fs');
const path = require('path');
const zlib = require('zlib');
const { spawnSync } = require('child_process');

// Miniapp novo: acrescente apenas mais um par nesta lista.
const MINIAPPS = [
  ['../docs/exemplos/casa-lavadora.json', 'miniapps-estados/casa-lavadora.json'],
  ['../docs/exemplos/casa-luzes.json', 'miniapps-estados/casa-luzes.json'],
  ['../docs/exemplos/casa-tomadas.json', 'miniapps-estados/casa-tomadas.json'],
  ['apps-seeds/tokensmall.json', 'miniapps-estados/tokensmall.json'],
];

const T = [];
for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; T[n] = c >>> 0; }
const crc = b => { let c = 0xffffffff; for (const x of b) c = T[(c ^ x) & 255] ^ (c >>> 8); return (c ^ 0xffffffff) >>> 0; };
const chunk = (t, d) => { const l = Buffer.alloc(4); l.writeUInt32BE(d.length); const td = Buffer.concat([Buffer.from(t), d]); const c = Buffer.alloc(4); c.writeUInt32BE(crc(td)); return Buffer.concat([l, td, c]); };
function png(raw565, largura, altura = 240) {
  const raw = Buffer.alloc((largura * 3 + 1) * altura);
  for (let y = 0; y < altura; y++) for (let x = 0; x < largura; x++) {
    const v = raw565.readUInt16LE((y * largura + x) * 2), o = y * (largura * 3 + 1) + 1 + x * 3;
    raw[o] = ((v >> 11) & 31) * 255 / 31; raw[o + 1] = ((v >> 5) & 63) * 255 / 63; raw[o + 2] = (v & 31) * 255 / 31;
  }
  const ih = Buffer.alloc(13); ih.writeUInt32BE(largura, 0); ih.writeUInt32BE(altura, 4); ih[8] = 8; ih[9] = 2;
  return Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', ih), chunk('IDAT', zlib.deflateSync(raw, { level: 9 })), chunk('IEND', Buffer.alloc(0))]);
}

function pixelsPng(arq) {
  const b = fs.readFileSync(arq);
  if (!b.subarray(0, 8).equals(Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]))) throw new Error(`PNG invalido: ${arq}`);
  let p = 8, largura = 0, altura = 0; const idat = [];
  while (p < b.length) {
    const n = b.readUInt32BE(p), tipo = b.toString('ascii', p + 4, p + 8), dados = b.subarray(p + 8, p + 8 + n); p += n + 12;
    if (tipo === 'IHDR') { largura = dados.readUInt32BE(0); altura = dados.readUInt32BE(4); if (dados[8] !== 8 || dados[9] !== 2) throw new Error(`PNG nao e RGB 8 bits: ${arq}`); }
    if (tipo === 'IDAT') idat.push(dados);
    if (tipo === 'IEND') break;
  }
  const filtrado = zlib.inflateSync(Buffer.concat(idat)), passo = largura * 3 + 1, pixels = Buffer.alloc(largura * altura * 3);
  if (filtrado.length !== passo * altura) throw new Error(`tamanho PNG inesperado: ${arq}`);
  for (let y = 0; y < altura; y++) { if (filtrado[y * passo] !== 0) throw new Error(`filtro PNG inesperado em ${arq}, linha ${y}`); filtrado.copy(pixels, y * largura * 3, y * passo + 1, (y + 1) * passo); }
  return { largura, altura, pixels };
}

function tira(quadros) {
  const saida = Buffer.alloc(quadros.length * 240 * 240 * 2), linha = 240 * 2;
  for (let y = 0; y < 240; y++) quadros.forEach((q, i) => q.copy(saida, (y * quadros.length + i) * linha, y * linha, (y + 1) * linha));
  return saida;
}

const modo = process.argv[2] || '';
if (modo && modo !== '--gravar' && modo !== '--conferir') { console.error('Uso: node sim/previas_miniapps.js [--gravar|--conferir]'); process.exit(1); }
const raiz = __dirname, saida = path.join(raiz, 'out', 'miniapps');
fs.mkdirSync(saida, { recursive: true });
const binario = path.join(raiz, 'out', process.platform === 'win32' ? 'previa_miniapp.exe' : 'previa_miniapp');
for (const [pacoteRel, estadosRel] of MINIAPPS) {
  const pacote = path.resolve(raiz, pacoteRel), estadosArq = path.resolve(raiz, estadosRel);
  const pacoteJson = JSON.parse(fs.readFileSync(pacote, 'utf8')), estados = JSON.parse(fs.readFileSync(estadosArq, 'utf8')), id = pacoteJson.id;
  const r = spawnSync(binario, [pacote, estadosArq, saida], { encoding: 'utf8' });
  if (r.stdout) process.stdout.write(r.stdout);
  if (r.status !== 0) { if (r.stderr) process.stderr.write(r.stderr); else console.error(`nao foi possivel executar ${binario}`); process.exit(1); }
  const quadros = estados.estados.map((_, i) => fs.readFileSync(path.join(saida, `${id}-${i}.raw`)));
  if (!Number.isInteger(estados.capa) || !quadros[estados.capa]) { console.error(`capa invalida em ${estadosRel}`); process.exit(1); }
  fs.writeFileSync(path.join(saida, `${id}.png`), png(quadros[estados.capa], 240));
  fs.writeFileSync(path.join(saida, `${id}-estados.png`), png(tira(quadros), quadros.length * 240));
  if (modo === '--gravar' && id.startsWith('casa-')) for (const sufixo of ['', '-estados']) fs.copyFileSync(path.join(saida, `${id}${sufixo}.png`), path.resolve(raiz, '../docs/exemplos', `${id}${sufixo}.png`));
}
if (modo === '--conferir') {
  for (const [pacoteRel] of MINIAPPS.filter(([p]) => p.includes('/casa-'))) {
    const id = JSON.parse(fs.readFileSync(path.resolve(raiz, pacoteRel), 'utf8')).id;
    for (const sufixo of ['', '-estados']) {
      const atual = path.join(saida, `${id}${sufixo}.png`), salvo = path.resolve(raiz, '../docs/exemplos', `${id}${sufixo}.png`);
      if (!fs.existsSync(salvo)) { console.error(`${salvo} nao existe; rode node sim/previas_miniapps.js --gravar`); process.exit(1); }
      const a = pixelsPng(atual), b = pixelsPng(salvo);
      if (a.largura !== b.largura || a.altura !== b.altura || !a.pixels.equals(b.pixels)) { console.error(`${path.basename(salvo)} difere; rode node sim/previas_miniapps.js --gravar`); process.exit(1); }
    }
  }
  console.log('previas_miniapps: PNGs dos exemplos conferem por pixels');
}
