#!/usr/bin/env node
// Gera as traduções da tela em PROGMEM a partir dos tr() literais do firmware.
// Não usa dependências: pode rodar no Windows, no Linux e na CI só com Node.js.

const fs = require('fs');
const path = require('path');

const RAIZ = path.resolve(__dirname, '..');
const FW = path.join(RAIZ, 'smalltv-pomodoro');
const SAIDA = path.join(FW, 'lang_tr.h');
const IDIOMAS = ['de', 'ru', 'fr'];

function fnv1a(texto) {
  let h = 2166136261;
  for (const byte of Buffer.from(texto, 'utf8')) {
    h ^= byte;
    h = Math.imul(h, 16777619) >>> 0;
  }
  return h >>> 0;
}

// Substitui comentários por espaços, preservando quebras de linha e posições.
function fimStringRaw(codigo, inicio) {
  if (codigo[inicio] !== 'R' || codigo[inicio + 1] !== '"') return -1;
  const abre = codigo.indexOf('(', inicio + 2);
  if (abre < 0 || abre - (inicio + 2) > 16) return -1;
  const delimitador = codigo.slice(inicio + 2, abre);
  if (/[\s\\()]/.test(delimitador)) return -1;
  const fecha = codigo.indexOf(`)${delimitador}"`, abre + 1);
  return fecha < 0 ? -1 : fecha + delimitador.length + 2;
}

function tirarComentarios(codigo) {
  let saida = '';
  let estado = 'codigo';
  for (let i = 0; i < codigo.length; i++) {
    const c = codigo[i];
    const p = codigo[i + 1];
    if (estado === 'linha') {
      if (c === '\n' || c === '\r') { saida += c; estado = 'codigo'; }
      else saida += ' ';
    } else if (estado === 'bloco') {
      if (c === '*' && p === '/') { saida += '  '; i++; estado = 'codigo'; }
      else saida += c === '\n' || c === '\r' ? c : ' ';
    } else if (estado === 'string' || estado === 'caractere') {
      saida += c;
      if (c === '\\' && p !== undefined) { saida += p; i++; }
      else if ((estado === 'string' && c === '"') || (estado === 'caractere' && c === "'")) estado = 'codigo';
    } else if (fimStringRaw(codigo, i) >= 0) {
      const fim = fimStringRaw(codigo, i);
      for (; i < fim; i++) saida += codigo[i] === '\n' || codigo[i] === '\r' ? codigo[i] : ' ';
      i--;
    } else if (c === '/' && p === '/') {
      saida += '  '; i++; estado = 'linha';
    } else if (c === '/' && p === '*') {
      saida += '  '; i++; estado = 'bloco';
    } else {
      saida += c;
      if (c === '"') estado = 'string';
      else if (c === "'") estado = 'caractere';
    }
  }
  return saida;
}

function decodificarLiteral(texto) {
  return texto.replace(/\\(["\\])/g, '$1');
}

function validarLiteralPt(texto) {
  for (let i = 0; i < texto.length; i++) {
    const n = texto.charCodeAt(i);
    if (n > 0x7f) return 'contém byte não ASCII';
    if (texto[i] === '\\') {
      if (texto[i + 1] !== '"' && texto[i + 1] !== '\\') return `contém escape inválido ${JSON.stringify(texto.slice(i, i + 2))}`;
      i++;
    }
  }
  return null;
}

// Encontra tr( no código, mas não em comentários, strings, caracteres nem no próprio #define tr(.
function localizarChamadasTr(codigo) {
  const chamadas = [];
  let estado = 'codigo';
  let inicioLinha = 0;
  for (let i = 0; i < codigo.length; i++) {
    const c = codigo[i];
    const p = codigo[i + 1];
    if (c === '\n' || c === '\r') inicioLinha = i + 1;
    if (estado === 'linha') {
      if (c === '\n' || c === '\r') estado = 'codigo';
    } else if (estado === 'bloco') {
      if (c === '*' && p === '/') { i++; estado = 'codigo'; }
    } else if (estado === 'string' || estado === 'caractere') {
      if (c === '\\' && p !== undefined) {
        if (p === '\n' || p === '\r') inicioLinha = i + 2;
        i++;
      }
      else if ((estado === 'string' && c === '"') || (estado === 'caractere' && c === "'")) estado = 'codigo';
    } else if (fimStringRaw(codigo, i) >= 0) {
      const fim = fimStringRaw(codigo, i);
      const trecho = codigo.slice(i, fim);
      const ultimaLf = trecho.lastIndexOf('\n');
      const ultimaCr = trecho.lastIndexOf('\r');
      const ultimaQuebra = Math.max(ultimaLf, ultimaCr);
      if (ultimaQuebra >= 0) inicioLinha = i + ultimaQuebra + 1;
      i = fim - 1;
    } else if (c === '/' && p === '/') {
      i++; estado = 'linha';
    } else if (c === '/' && p === '*') {
      i++; estado = 'bloco';
    } else if (c === '"') estado = 'string';
    else if (c === "'") estado = 'caractere';
    else if (codigo.startsWith('tr(', i) && (i === 0 || !/[A-Za-z0-9_]/.test(codigo[i - 1]))) {
      const prefixo = tirarComentarios(codigo.slice(inicioLinha, i));
      if (!/^\s*#\s*define\s*$/.test(prefixo)) {
        chamadas.push({ indice: i, linha: codigo.slice(0, i).split(/\n/).length });
      }
    }
  }
  return chamadas;
}

function extractTrCalls(codigo, arquivo = '<texto>') {
  const limpo = tirarComentarios(codigo);
  const re = /\btr\(\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*\)/g;
  const chamadas = [];
  let m;
  while ((m = re.exec(limpo)) !== null) {
    chamadas.push({
      pt: decodificarLiteral(m[1]),
      ptLiteral: m[1],
      en: decodificarLiteral(m[2]),
      es: decodificarLiteral(m[3]),
      arquivo,
      linha: limpo.slice(0, m.index).split(/\n/).length,
      indice: m.index,
    });
  }
  return chamadas;
}

function arquivosFirmware(dir = FW) {
  const resultado = [];
  for (const nome of fs.readdirSync(dir).sort()) {
    const completo = path.join(dir, nome);
    const relativo = path.relative(FW, completo).replace(/\\/g, '/');
    const st = fs.statSync(completo);
    if (st.isDirectory()) {
      if (relativo === 'src') continue;
      resultado.push(...arquivosFirmware(completo));
    } else if (/\.(?:cpp|h|ino)$/.test(nome) && nome !== 'web.h' && nome !== 'lang_tr.h') {
      resultado.push(completo);
    }
  }
  return resultado;
}

const RE_ESPECIFICADOR = /%(?:\d+\$)?[-+ #0']*(?:\*|\d+)?(?:\.(?:\*|\d+))?(?:hh|h|ll|l|j|z|t|L)?[diuoxXfFeEgGaAcspn%]/g;
function especificadores(texto) {
  return texto.match(RE_ESPECIFICADOR) || [];
}

function temPorcentagemSolta(texto) {
  return texto.replace(RE_ESPECIFICADOR, '').includes('%');
}

function linhaDaChave(json, chave) {
  const procurado = JSON.stringify(chave);
  const i = json.indexOf(procurado);
  return i < 0 ? 1 : json.slice(0, i).split(/\n/).length;
}

function caracterePermitido(texto, idioma) {
  // só o que a fonte da tela desenha (CP437 e os latinos de utf8_pixel.h)
  const extra = idioma === 'de' ? 'äöüßÄÖÜ' : idioma === 'fr' ? 'àâçéèêëîïôûùÀÂÇÉÈÊËÎÏÔÙÛ' : '';
  for (const c of texto) {
    const n = c.codePointAt(0);
    if ('"\'<>&\\'.includes(c) || n < 0x20 || n === 0x7f) return c;
    if (n >= 0x20 && n <= 0x7e) continue;
    if (extra.includes(c)) continue;
    if (idioma === 'ru' && ((n >= 0x410 && n <= 0x42f) || (n >= 0x430 && n <= 0x44f) || c === 'Ё' || c === 'ё')) continue;
    return c;
  }
  return null;
}

function escaparBytes(texto) {
  let s = '';
  for (const b of Buffer.from(texto, 'utf8')) {
    if (b >= 0x80) s += `\\${b.toString(8).padStart(3, '0')}`;
    else if (b === 0x22) s += '\\"';
    else if (b === 0x5c) s += '\\\\';
    else s += String.fromCharCode(b);
  }
  return s;
}

function gerarCabecalho(tabelas) {
  const linhas = [
    '// Gerado por scripts/idiomas-gerar.cjs a partir de scripts/idiomas/tr-*.json: não edite à mão.',
    '#pragma once',
    '#include <pgmspace.h>',
    '',
  ];
  for (const idioma of IDIOMAS) {
    const L = idioma.toUpperCase();
    const itens = tabelas[idioma];
    let offset = 0;
    for (const item of itens) { item.offset = offset; offset += Buffer.byteLength(item.tr, 'utf8') + 1; }
    linhas.push(`static const uint32_t LANG_${L}_H[${itens.length}] PROGMEM = {`);
    linhas.push(...itens.map((x, i) => `  0x${x.hash.toString(16).padStart(8, '0')}u${i + 1 === itens.length ? '' : ','}`));
    linhas.push('};');
    linhas.push(`static const uint16_t LANG_${L}_O[${itens.length}] PROGMEM = {`);
    linhas.push(...itens.map((x, i) => `  ${x.offset}${i + 1 === itens.length ? '' : ','}`));
    linhas.push('};');
    linhas.push(`static const char LANG_${L}_S[] PROGMEM =`);
    linhas.push(...itens.map(x => `  "${escaparBytes(x.tr)}\\000"`));
    linhas.push(';');
    linhas.push(`#define LANG_${L}_N ${itens.length}`);
    linhas.push('');
  }
  return linhas.join('\n');
}

function gerarIncludeSim(unicos, traducoesPorIdioma) {
  const linhasTraduzidas = [...unicos.values()]
    .filter(chamada => IDIOMAS.every(idioma => Object.prototype.hasOwnProperty.call(traducoesPorIdioma[idioma], chamada.pt)))
    .sort((a, b) => a.pt < b.pt ? -1 : a.pt > b.pt ? 1 : 0);
  const linhas = ['// Gerado por scripts/idiomas-gerar.cjs --sim-inc: nao edite a mao.'];
  const array = (nome, valores) => {
    linhas.push(`static const char *const ${nome}[] = {`);
    linhas.push(...valores.map(valor => `  "${escaparBytes(valor)}",`));
    linhas.push('};');
  };
  array('LANG_TEST_PT', linhasTraduzidas.map(x => x.pt));
  for (const idioma of IDIOMAS) array(`LANG_TEST_${idioma.toUpperCase()}`, linhasTraduzidas.map(x => traducoesPorIdioma[idioma][x.pt]));
  linhas.push(`static const int LANG_TEST_N = ${linhasTraduzidas.length};`, '');
  return linhas.join('\n');
}

function principal() {
  const erros = [];
  const avisos = [];
  const unicos = new Map();
  for (const arquivo of arquivosFirmware()) {
    const relativo = path.relative(RAIZ, arquivo).replace(/\\/g, '/');
    const codigo = fs.readFileSync(arquivo, 'utf8');
    const extraidas = extractTrCalls(codigo, relativo);
    const localizadas = localizarChamadasTr(codigo);
    const indicesExtraidos = new Set(extraidas.map(x => x.indice));
    for (const chamada of localizadas.filter(x => !indicesExtraidos.has(x.indice))) {
      erros.push(`${relativo}:${chamada.linha}: chamada tr() não extraída; use três literais simples na mesma chamada`);
    }
    if (localizadas.length !== extraidas.length) {
      erros.push(`${relativo}: contagem de tr( no código (${localizadas.length}) difere das chamadas literais extraídas (${extraidas.length})`);
    }
    for (const chamada of extraidas) {
      const erroLiteral = validarLiteralPt(chamada.ptLiteral);
      if (erroLiteral) erros.push(`${chamada.arquivo}:${chamada.linha}: literal pt de tr() ${erroLiteral}; só \\" e \\\\ são aceitos`);
      const anterior = unicos.get(chamada.pt);
      if (anterior && (anterior.en !== chamada.en || anterior.es !== chamada.es)) {
        erros.push(`${chamada.arquivo}:${chamada.linha}: tr() de ${JSON.stringify(chamada.pt)} diverge de ${anterior.arquivo}:${anterior.linha}`);
      } else if (!anterior) unicos.set(chamada.pt, chamada);
    }
  }

  const porHash = new Map();
  for (const chamada of unicos.values()) {
    const hash = fnv1a(chamada.pt);
    const anterior = porHash.get(hash);
    if (anterior && anterior.pt !== chamada.pt) {
      erros.push(`${chamada.arquivo}:${chamada.linha}: hash FNV-1a 0x${hash.toString(16).padStart(8, '0')} repetido entre ${JSON.stringify(anterior.pt)} (${anterior.arquivo}:${anterior.linha}) e ${JSON.stringify(chamada.pt)}`);
    } else porHash.set(hash, chamada);
  }

  const tabelas = {};
  const resumos = {};
  const traducoesPorIdioma = {};
  for (const idioma of IDIOMAS) {
    const arquivoJson = path.join(__dirname, 'idiomas', `tr-${idioma}.json`);
    const json = fs.readFileSync(arquivoJson, 'utf8');
    let traducoes;
    try { traducoes = JSON.parse(json); }
    catch (e) { erros.push(`${path.relative(RAIZ, arquivoJson)}:1: JSON inválido: ${e.message}`); traducoes = {}; }
    traducoesPorIdioma[idioma] = traducoes;
    for (const [pt, tr] of Object.entries(traducoes)) {
      const local = `scripts/idiomas/tr-${idioma}.json:${linhaDaChave(json, pt)}`;
      const chamada = unicos.get(pt);
      if (!chamada) { erros.push(`${local}: chave órfã ${JSON.stringify(pt)}; o texto pt mudou ou saiu do código? refaça a tradução em scripts/idiomas/tr-${idioma}.json`); continue; }
      const bytes = Buffer.byteLength(tr, 'utf8');
      if (bytes > 47) erros.push(`${local}: tradução de ${JSON.stringify(pt)} tem ${bytes} bytes UTF-8 (máximo 47)`);
      const invalido = caracterePermitido(tr, idioma);
      if (invalido !== null) erros.push(`${local}: tradução de ${JSON.stringify(pt)} contém caractere não permitido ${JSON.stringify(invalido)}`);
      if (temPorcentagemSolta(tr)) erros.push(`${local}: tradução de ${JSON.stringify(pt)} contém % solto`);
      const sTr = especificadores(tr);
      const sPt = especificadores(chamada.pt);
      const sEn = especificadores(chamada.en);
      if (JSON.stringify(sTr) !== JSON.stringify(sPt) || JSON.stringify(sTr) !== JSON.stringify(sEn)) {
        erros.push(`${local}: especificadores de ${JSON.stringify(pt)}: tradução ${JSON.stringify(sTr)}, pt ${JSON.stringify(sPt)}, en ${JSON.stringify(sEn)}`);
      }
    }
    for (const chamada of unicos.values()) {
      if (!Object.prototype.hasOwnProperty.call(traducoes, chamada.pt)) {
        avisos.push(`${chamada.arquivo}:${chamada.linha}: ${idioma} sem tradução para ${JSON.stringify(chamada.pt)}; usará inglês`);
      }
    }
    tabelas[idioma] = Object.entries(traducoes)
      .filter(([pt]) => unicos.has(pt))
      .map(([pt, tr]) => ({ pt, tr, hash: fnv1a(pt), local: `scripts/idiomas/tr-${idioma}.json:${linhaDaChave(json, pt)}` }))
      .sort((a, b) => a.hash - b.hash);
    let offset = 0;
    for (const item of tabelas[idioma]) {
      if (offset > 65535) erros.push(`${item.local}: deslocamento ${offset} de ${JSON.stringify(item.pt)} excede uint16_t`);
      offset += Buffer.byteLength(item.tr, 'utf8') + 1;
    }
    const strings = tabelas[idioma].reduce((n, x) => n + Buffer.byteLength(x.tr, 'utf8') + 1, 0);
    resumos[idioma] = { n: tabelas[idioma].length, strings, total: tabelas[idioma].length * 6 + strings };
  }

  for (const aviso of avisos) console.warn(`AVISO: ${aviso}`);
  if (erros.length) {
    for (const erro of erros) console.error(`ERRO: ${erro}`);
    process.exitCode = 1;
    return;
  }

  const iSim = process.argv.indexOf('--sim-inc');
  if (iSim >= 0) {
    const destino = process.argv[iSim + 1];
    if (!destino || destino.startsWith('--')) {
      console.error('ERRO: --sim-inc exige o caminho do arquivo de saída');
      process.exitCode = 1;
      return;
    }
    fs.writeFileSync(path.resolve(destino), gerarIncludeSim(unicos, traducoesPorIdioma), 'utf8');
  }

  const gerado = gerarCabecalho(tabelas);
  if (process.argv.includes('--check')) {
    const atual = fs.existsSync(SAIDA) ? fs.readFileSync(SAIDA, 'utf8') : null;
    if (atual !== gerado) {
      console.error('ERRO: smalltv-pomodoro/lang_tr.h está ausente ou desatualizado; rode node scripts/idiomas-gerar.cjs');
      process.exitCode = 1;
      return;
    }
    console.log(`idiomas: ok (${unicos.size} textos pt; ${IDIOMAS.map(l => `${resumos[l].n} ${l}`).join(', ')}; ${avisos.length} avisos)`);
    return;
  }
  fs.writeFileSync(SAIDA, gerado, 'utf8');
  console.log(`idiomas: ${unicos.size} textos pt; ${IDIOMAS.map(l => `${resumos[l].n} traduzidos ${l}`).join(', ')}; ${avisos.length} avisos`);
  for (const idioma of IDIOMAS) console.log(`${idioma}: ${resumos[idioma].total} bytes (${resumos[idioma].n * 4} hashes + ${resumos[idioma].n * 2} offsets + ${resumos[idioma].strings} textos)`);
}

module.exports = { fnv1a, extractTrCalls };
if (require.main === module) principal();
