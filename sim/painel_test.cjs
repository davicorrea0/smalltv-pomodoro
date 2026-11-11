// Restaurar backup do painel (web/index.html): cada etapa roda à parte e uma falha não impede as outras.
// Extrai o runSteps real do painel, confere a ordem/contabilidade e que todo texto novo tem inglês e espanhol.
const fs = require('fs'), path = require('path'), assert = require('assert');
const html = fs.readFileSync(path.join(__dirname, '../web/index.html'), 'utf8');
const i = html.indexOf('async function runSteps('), runSteps = new Function('return ' + html.slice(i, html.indexOf('\n', i)))();
(async () => {
  const ran = [];
  const R = await runSteps([
    ['Mostradores', async () => ran.push('m')],
    ['Ajustes', async () => ran.push('a')],
    ['Histórico', async () => { ran.push('h'); throw 'Backup invalido'; }],
    ['Avisos', async () => { ran.push('v'); throw new Error('falhou'); }],
    ['Fotos e GIFs', async () => ran.push('f')]]);
  assert.deepStrictEqual(ran, ['m', 'a', 'h', 'v', 'f']);   // a falha do histórico não impediu as fotos
  assert.deepStrictEqual(R.ok, ['Mostradores', 'Ajustes', 'Fotos e GIFs']);
  assert.deepStrictEqual(R.bad, [['Histórico', 'Backup invalido'], ['Avisos', 'falhou']]);
  assert.deepStrictEqual((await runSteps([])).bad, []);
  // o handler usa as cinco etapas, o cartão de migração depende só das fotos e os textos estão traduzidos
  const h = html.slice(html.indexOf("$('rbk').onchange"), html.indexOf('// ---------- avisos de fim de fase'));
  for (const n of ['Mostradores', 'Ajustes', 'Histórico', 'Avisos', 'Fotos e GIFs']) assert(h.includes(`['${n}',`), n);
  assert(h.includes("if(!R.bad.some(x=>x[0]=='Fotos e GIFs')&&st&&st.fs&&st.fs.mig)migDone()"));
  const dict = JSON.parse(fs.readFileSync(path.join(__dirname, '../web/i18n.json'), 'utf8')).dict;
  const keys = ['Mostradores', 'Ajustes', 'Histórico', 'Avisos', 'Fotos e GIFs', 'Backup restaurado com falhas.', 'Entrou: {0}', 'Falhou:',
    '{0} mostrador(es) não estão na galeria e precisam ser instalados de novo pelo arquivo', '{0} foto(s)/GIF(s) não couberam e ficaram de fora'];
  for (const k of keys) assert(dict[k] && dict[k].length == 2 && dict[k].every(x => x), 'sem tradução: ' + k);
  console.log('painel: restaurar backup segue após falha de uma etapa e resume o que entrou e o que falhou');
})();
// Filtro dos mostradores (web/index.html): os nativos precisam de estilos na ordem de faces_list.h, só o Flip anima,
// o filtro lê o campo anim do catálogo e todo texto novo tem inglês e espanhol.
{
  const reg = fs.readFileSync(path.join(__dirname, '../smalltv-pomodoro/faces_list.h'), 'utf8'), ids = [...reg.replace(/^\/\/.*$/gm, '').matchAll(/X\(([A-Z_]+), /g)].map(m => m[1]);
  const BTAG = new Function('return ' + html.match(/BTAG=(\[[^\]]*\])/)[1])(), BANIM = new Function('return ' + html.match(/BANIM=(\[[^\]]*\])/)[1])();
  const sty = ['pixel', 'digital', 'analogico', 'retro', 'esportivo'];
  assert.strictEqual(BTAG.length, ids.length, 'um mostrador novo em faces_list.h precisa de estilos em BTAG (web/index.html)');
  assert(BTAG.every(s => s.split(' ').every(x => sty.includes(x))), 'estilo desconhecido em BTAG');
  assert.deepStrictEqual(BANIM.map(i => ids[i]), ['FLIP']);
  const mtok = new Function('return ' + html.match(/mtok=(m=>[^,]*),ftok/)[1])();
  assert.strictEqual(mtok(['pixel digital', 1]), 'pixel digital anim');
  assert.strictEqual(mtok(['retro', 0]), 'retro');
  assert.strictEqual(mtok(undefined), '');   // pacote fora do catálogo: sem estilo nem animação
  assert(html.includes("e.anim?1:0"), 'setMeta deve guardar se o pacote do catálogo é animado');
  const dict = JSON.parse(fs.readFileSync(path.join(__dirname, '../web/i18n.json'), 'utf8')).dict;
  for (const k of ['Todos', 'Nativos', 'Baixados', 'Animados', 'Digital', 'Pixel art', 'Analógico', 'Retrô', 'Esportivo', 'Nenhum mostrador neste filtro.',
    'Nenhum mostrador neste filtro ainda. Abra a galeria online logo abaixo para ver mais.'])
    assert(dict[k] && dict[k].length == 2 && dict[k].every(x => x), 'sem tradução: ' + k);
  console.log('painel: filtro dos mostradores com estilos para os ' + ids.length + ' nativos, só o Flip animado e textos traduzidos');
}
// Rodízio em lote (web/index.html): "Editar rodízio" monta a lista inteira e grava num POST só; "Tirar todos" pede
// confirmação; nada passa do teto do rotSet (album.cpp), que cortaria a lista em silêncio.
{
  const line = s => { const i = html.indexOf(s); assert(i >= 0, s); return html.slice(i, html.indexOf('\n', i)); };
  const rotMerge = new Function('return ' + line('const rotMerge=').replace(/^const rotMerge=/, '').replace(/;$/, ''))();
  const known = ['t:0', 't:1', 't:2', 'p:koi', 'p:bento'];
  // novos no fim, na ordem do toque; os que ficam mantêm o lugar; nada duplica
  assert.deepStrictEqual(rotMerge([], known, ['p:koi', 't:2', 't:0']), ['p:koi', 't:2', 't:0']);
  assert.deepStrictEqual(rotMerge(['t:1', 'p:koi'], known, ['t:1', 'p:koi', 't:0', 'p:bento']), ['t:1', 'p:koi', 't:0', 'p:bento']);
  assert.deepStrictEqual(rotMerge(['t:1', 'p:koi'], known, ['p:koi', 't:1']), ['t:1', 'p:koi']);   // desmarcar e remarcar não muda a ordem
  // desmarcados saem; chave sem cartão (pacote que não carregou ou foi removido) fica
  assert.deepStrictEqual(rotMerge(['t:1', 'p:velho', 'p:koi', 't:2'], known, ['t:2']), ['p:velho', 't:2']);
  assert.deepStrictEqual(rotMerge(['t:1', 'p:koi'], known, []), []);
  // teto igual ao do firmware (as duas ocorrências de 300 no rotSet)
  const ROTMAX = +html.match(/const ROTMAX=(\d+);/)[1];
  const album = fs.readFileSync(path.join(__dirname, '../smalltv-pomodoro/modulos/album.cpp'), 'utf8');
  const rs = album.slice(album.indexOf('void rotSet('), album.indexOf('void rotLoop('));
  assert.deepStrictEqual(rs.match(/\b\d{3}\b/g), [String(ROTMAX), String(ROTMAX), String(ROTMAX)], 'ROTMAX do painel diferente do rotSet');
  // limite conferido no toque, ao salvar e no incluir de um: nunca grava pela metade
  const ed = html.slice(html.indexOf('function rotEdit('), html.indexOf('function rotToggle('));
  assert((ed.match(/rotOver\(/g) || []).length == 2, 'rotEdit confere o teto no toque e ao salvar');
  assert(line('function rotToggle(').includes('if(!on&&rotOver(l.concat(k)))return;'), 'rotToggle confere o teto');
  const toasts = [], rotOver = new Function('ROTMAX', 'toast', 't', 'return ' + line('const rotOver=').replace(/^const rotOver=/, '').replace(/;\s*\/\/.*$/, ''))(ROTMAX, m => toasts.push(m), x => x);
  assert(!rotOver(['x'.repeat(ROTMAX)]) && !toasts.length);   // no teto ainda cabe
  assert(rotOver(['x'.repeat(ROTMAX - 1), 'y']) && toasts.length == 1);   // a vírgula conta
  const big = Array.from({ length: 30 }, (_, i) => 'p:pacote-numero-' + String(i).padStart(2, '0'));
  assert(rotMerge([], big, big).join(',').length > ROTMAX);   // 13 nativos + 41 pacotes podem passar do teto
  // salvar: um POST só (rotPost), sem POST quando nada mudou
  assert(ed.includes("if(l.join(',')==rotKeys().join(','))closeSheet();else if(!rotOver(l))") && (ed.match(/rotPost\(/g) || []).length == 1);
  assert(line('function rotPost(').includes('s.cfg.rot!=l?'), 'rotPost confere o rodízio devolvido pelo relógio');
  // tirar todos: cancelar não grava; confirmar grava a lista vazia uma vez; rodízio vazio nem pergunta
  const mk = (keys, ok) => { const calls = [], asked = [];
    const f = new Function('rotKeys', 'confirm', 't', 'rotPost', 'return ' + line('function rotClear('))(() => keys, q => (asked.push(q), ok), (s, ...a) => s.replace(/\{(\d)\}/g, (m, i) => a[i]), (l, m) => calls.push([l, m]));
    f(); return { calls, asked }; };
  assert.deepStrictEqual(mk(['t:1', 't:2'], false).calls, []);
  const yes = mk(['t:1', 't:2', 'p:koi'], true);
  assert.deepStrictEqual(yes.calls, [['', 'Rodízio vazio']]);
  assert(yes.asked[0].includes('3 mostradores'));
  assert.deepStrictEqual(mk([], true), { calls: [], asked: [] });
  const dict = JSON.parse(fs.readFileSync(path.join(__dirname, '../web/i18n.json'), 'utf8')).dict;
  for (const k of ['Editar rodízio', 'Tirar todos do rodízio', 'Os novos entram no fim, na ordem do toque.',
    'Não cabe mais no relógio', 'Rodízio salvo', 'Rodízio salvo. Escolha o intervalo em "Trocar de mostrador".',
    'O relógio não guardou. Tente de novo.', 'Tirar os {0} mostradores do rodízio? O relógio fica no atual.', 'Rodízio vazio', 'Cancelar', 'Salvar'])
    assert(dict[k] && dict[k].length == 2 && dict[k].every(x => x), 'sem tradução: ' + k);
  console.log('painel: rodízio em lote mantém a ordem, não duplica, preserva pacote sem cartão, respeita o teto de ' + ROTMAX + ' e confirma antes de esvaziar');
}
