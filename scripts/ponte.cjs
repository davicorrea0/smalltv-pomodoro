// Ponte entre os layouts da flash (4M3M → 4M2M) no firmware/latest.json e no firmware/beta.json.
//
// Os painéis antigos (todos até a 1.24.11) só leem o TOPO do JSON: comparam a versão com a do relógio e mandam o
// firmware para o /update. Um relógio no layout antigo (4M3M, versões até a 1.23.x) só enxerga 1 MB para a
// atualização: firmware arredondado + .gz arredondado (+512 bytes do envio) <= 1 MB. Acima disso ele recusa com
// segurança ("firmware grande demais para descompactar", desde a 1.14), mas ficaria preso pedindo uma versão que
// nunca instala. Regra:
//  - o TOPO é sempre uma versão que cabe na janela antiga e cujo painel conhece a ponte ("ponte": 1). Os relógios
//    antigos (4M3M ou 4M2M com painel antigo) instalam essa e passam a ter o painel novo;
//  - "l2" traz a versão maior, que só cabe no 4M2M. Só o painel novo lê, e só se o relógio informa fs.l2.
// Uma versão que não cabe só pode ser publicada se o topo atual (ou, no beta, o do latest.json) já for uma ponte.
// Usado pelo scripts/publicar.sh e testado pelo sim/ponte_test.cjs.
const JANELA = 1048576;
const r4k = n => Math.ceil(n / 4096) * 4096;
const cabeJanela = (size, gz) => r4k(size) + r4k(gz + 512) <= JANELA;
const vnum = v => String(v || '0').split('.').reduce((a, x) => a * 1000 + (+x || 0), 0);  // a mesma conta do painel
const topo = j => { if (!j) return null; const { l2, ...t } = j; return t; };

// atual: o JSON que está no site (ou null); reserva: no beta, o latest.json (o topo dele serve de ponte);
// nova: a entrada da versão publicada (file/gz já com o prefixo da pasta); cabe: se cabe na janela antiga.
function montar({ atual, reserva, nova, cabe }) {
  if (cabe) {  // vira o topo e a ponte; um "l2" mais novo que ela (raro: uma correção menor depois) continua valendo
    const j = { ...nova, ponte: 1 };
    if (atual && atual.l2 && vnum(atual.l2.version) > vnum(nova.version)) j.l2 = atual.l2;
    return j;
  }
  const p = [atual, reserva].find(j => j && j.ponte && cabeJanela(j.size, j.gzsize || 0));
  if (!p) throw new Error('a versão ' + nova.version + ' não cabe na janela de 1 MB do layout antigo e o site ainda não tem uma versão-ponte' +
    ' ("ponte": 1 no topo do JSON). Publique antes como estável uma versão que caiba (painel com a ponte), senão os relógios antigos ficariam presos.');
  const t = topo(p);
  if (vnum(t.version) >= vnum(nova.version)) throw new Error('a ponte (' + t.version + ') não é anterior à versão ' + nova.version);
  return { ...t, l2: nova };
}

// O que cada painel instala (simulação; o novo é o checkUpdate de web/index.html, conferido no teste).
// antigo: só o topo. novo: o "l2" quando o relógio já está no 4M2M e ele é mais novo que o topo.
const ofertaAntigo = j => j;
const ofertaNovo = (j, l2) => j && l2 && j.l2 && vnum(j.l2.version) > vnum(j.version) ? j.l2 : j;

module.exports = { JANELA, cabeJanela, vnum, montar, ofertaAntigo, ofertaNovo };

// node scripts/ponte.cjs <atual.json|-> <reserva.json|-> <nova.json> <cabe 0|1>  → imprime o JSON a gravar
if (require.main === module) {
  const fs = require('fs'), [a, b, n, c] = process.argv.slice(2), ler = f => f && f != '-' && fs.existsSync(f) ? JSON.parse(fs.readFileSync(f, 'utf8')) : null;
  try { process.stdout.write(JSON.stringify(montar({ atual: ler(a), reserva: ler(b), nova: ler(n), cabe: c == '1' }), null, 2) + '\n'); }
  catch (e) { console.error('ERRO: ' + e.message); process.exit(1); }
}
