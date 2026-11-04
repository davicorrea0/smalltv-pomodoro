// Ponte de layout (scripts/ponte.cjs + checkUpdate de web/index.html): simula os relógios em cada versão e layout
// atualizando pelo painel ao longo de uma sequência de publicações e confere que nenhum fica preso, que nenhum
// painel antigo vê uma versão que o firmware dele recusaria e que o painel usa a mesma regra do script.
const fs = require('fs'), path = require('path'), assert = require('assert');
const P = require('../scripts/ponte.cjs');
const html = fs.readFileSync(path.join(__dirname, '../web/index.html'), 'utf8');
const vnum = new Function('return ' + html.match(/vnum=(v=>.*?\(\+x\|\|0\),0\))/)[1])();
const updL2 = new Function('vnum', 'return ' + html.match(/const updL2=([^;]*);/)[1])(vnum);
assert(html.includes('a=updL2(a,l2);b=updL2(b,l2)') && html.includes('const l2=!!(st&&st.fs&&st.fs.l2)'), 'checkUpdate deve aplicar a ponte no latest e no beta');

// tamanhos reais: 1.24.9 e 1.24.11 cabem na janela (583760/584096 + .gz ~431 KB); a 1.25 imaginária não
const v = (version, size, gzsize, panelNovo, dir = '') => ({ version, size, gzsize, file: dir + 'smalltv-pomodoro.bin', gz: dir + 'smalltv-pomodoro.bin.gz', layout: '4m2m', panelNovo });
assert(P.cabeJanela(583760, 431000) && P.cabeJanela(584096, 431500));
assert(!P.cabeJanela(700000, 520000) && !P.cabeJanela(1044464, 760000));
assert(P.cabeJanela(1048576 - 4096, 4096 - 512) && !P.cabeJanela(1048576 - 4096, 4096 - 511));

// o site hoje: estável 1.24.9 e beta 1.24.11, sem ponte (painéis antigos)
let latest = v('1.24.9', 583760, 431000, false), beta = v('1.24.11', 584096, 431500, false, 'beta/');
const pub = (j, reserva, nova) => P.montar({ atual: j, reserva, nova, cabe: P.cabeJanela(nova.size, nova.gzsize) });

// 1) sem ponte no site, uma versão grande é recusada (estável e beta)
assert.throws(() => pub(latest, null, v('1.25.0', 700000, 520000, true, 'l2/')), /versão-ponte/);
assert.throws(() => pub(beta, latest, v('1.25.0', 700000, 520000, true, 'beta/l2/')), /versão-ponte/);
// 2) a ponte: uma estável que cabe, com o painel novo
latest = pub(latest, null, v('1.24.12', 590000, 435000, true));
assert.strictEqual(latest.ponte, 1); assert(!latest.l2);
// 3) as grandes vão para "l2" (estável e beta), o topo continua sendo a ponte
latest = pub(latest, null, v('1.25.0', 700000, 520000, true, 'l2/'));
assert.strictEqual(latest.version, '1.24.12'); assert.strictEqual(latest.l2.version, '1.25.0'); assert.strictEqual(latest.file, 'smalltv-pomodoro.bin');
beta = pub(beta, latest, v('1.25.1', 710000, 530000, true, 'beta/l2/'));
assert.strictEqual(beta.version, '1.24.12'); assert.strictEqual(beta.l2.version, '1.25.1'); assert(!beta.l2.l2);
// a ponte não pode ser mais nova que a versão grande
assert.throws(() => pub(latest, null, v('1.24.1', 700000, 520000, true, 'l2/')), /não é anterior/);
// uma correção pequena depois mantém o l2 mais novo
assert.strictEqual(pub(latest, null, v('1.24.13', 590000, 435000, true)).l2.version, '1.25.0');
assert(!pub(latest, null, v('1.25.2', 590000, 435000, true)).l2);

// 4) cada tipo de relógio atualiza pelo painel até parar. O firmware recusa (com segurança) o que não cabe na área livre
// dele: 4M3M = janela de 1 MB; 4M2M = linker (1044464) e 2 MB.
const versoes = { '1.24.12': latest, '1.25.0': latest.l2, '1.25.1': beta.l2 };
function atualiza(rel) {
  for (let passo = 0; passo < 10; passo++) {
    const pega = j => rel.panelNovo ? updL2(j, rel.l2) : P.ofertaAntigo(j);
    let u = pega(latest);
    const b = rel.beta && pega(beta);
    if (b && (!u || vnum(b.version) > vnum(u.version))) u = b;
    if (!u || vnum(u.version) <= vnum(rel.ver)) return rel;
    const cabe = rel.l2 ? u.size <= 1044464 : P.cabeJanela(u.size, u.gzsize);
    assert(cabe, `${rel.nome}: o painel ofereceu a ${u.version}, que o firmware ${rel.ver} recusaria (ficaria preso)`);
    rel.ver = u.version; rel.l2 = true; rel.panelNovo = !!(versoes[u.version] || u).panelNovo;
  }
  throw new Error(rel.nome + ': não parou');
}
for (const [nome, ver, l2, beta1] of [['1.23.1 (4M3M)', '1.23.1', false, false], ['1.23.5 beta (4M3M)', '1.23.5', false, true],
  ['1.24.9 (4M2M, painel antigo)', '1.24.9', true, false], ['1.24.11 beta (4M2M, painel antigo)', '1.24.11', true, true],
  ['1.24.12 (ponte)', '1.24.12', true, false], ['1.25.0 beta', '1.25.0', true, true]]) {
  const fim = atualiza({ nome, ver, l2, beta: beta1, panelNovo: vnum(ver) >= vnum('1.24.12') });
  assert.strictEqual(fim.ver, beta1 ? '1.25.1' : '1.25.0', nome + ' ficou na ' + fim.ver);
}
// a 1.23.2/1.23.3 só instala pelo modo de recuperação (envio manual): a ponte cabe na janela dela, a grande é recusada
assert(P.cabeJanela(latest.size, latest.gzsize) && !P.cabeJanela(latest.l2.size, latest.l2.gzsize));
console.log('ponte: 4M3M e painéis antigos recebem a ponte (cabe em 1 MB), só o 4M2M com painel novo vê as versões maiores');
