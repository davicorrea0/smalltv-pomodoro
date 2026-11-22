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
  // teto igual ao do firmware (as ocorrências de 300 no código do rotSet, sem comentários)
  const ROTMAX = +html.match(/const ROTMAX=(\d+);/)[1];
  const album = fs.readFileSync(path.join(__dirname, '../smalltv-pomodoro/modulos/album.cpp'), 'utf8');
  const rs = album.slice(album.indexOf('void rotSet('), album.indexOf('void rotLoop(')).replace(/\/\/.*$/gm, '');  // sem os comentários
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
// Página Miniapps (web/index.html): os pacotes que leem dados de fora ("x" do /api/faces) saem de Relógio e vão para a
// aba própria, logo abaixo de Relógio no menu; o cartão do Home Assistant mora lá; o mesmo envio de arquivo serve às duas
// páginas; o "Editar rodízio" lista os dois com um selo; e os textos novos têm inglês e espanhol.
{
  const isApp = new Function('return ' + html.match(/const isApp=(f=>[^;]*);/)[1])();
  assert(isApp({ x: 'lv_est,lv_pct' }) && !isApp({ x: '' }) && !isApp({}), 'isApp: só com campos de fora');
  const bf = html.slice(html.indexOf('function buildFaces()'), html.indexOf('const rotKeys='));
  assert(bf.includes("(isApp(f)?a:f.pkg?d:g).appendChild(b)"), 'buildFaces: miniapp vai para #apps, não para Relógio');
  assert(html.includes("x:f.x||''"), 'loadFaces guarda os campos de fora de cada pacote');
  // aba: seção t4 depois da t3 (o tab() acende pela ordem das seções) e botão logo depois de Relógio no menu
  const t3 = html.indexOf('<section class="tab" id="t3">'), t4 = html.indexOf('<section class="tab" id="t4">');
  assert(t3 > 0 && t4 > t3 && html.indexOf('<section class="tab"', t3 + 10) == t4, 'a seção dos miniapps vem logo depois de Ajustes');
  const nav = html.slice(html.indexOf('<nav>'), html.indexOf('</nav>'));
  assert(nav.indexOf('data-t="0"') < nav.indexOf('data-t="4"') && nav.indexOf('data-t="4"') < nav.indexOf('data-t="1"'), 'Miniapps logo abaixo de Relógio');
  const pg = html.slice(t4, html.indexOf('</section>', t4));
  for (const id of ['id="apps"', 'id="appsnone"', 'id="apf"', 'id="hakey"', 'HOME-ASSISTANT.md']) assert(pg.includes(id), 'página Miniapps sem ' + id);
  assert(!html.slice(t3, t4).includes('id="hakey"'), 'o cartão do Home Assistant saiu de Ajustes');
  assert(html.includes("$('pkf').onchange=$('apf').onchange="), 'um envio só para mostrador e miniapp');
  assert(html.includes("if(i==4)appsLoad()"), 'a aba carrega o estado ao abrir');
  assert(html.includes("isApp(f)?[el('em','sel',t('miniapp'))]:[]"), 'selo de miniapp no Editar rodízio');
  const dict = JSON.parse(fs.readFileSync(path.join(__dirname, '../web/i18n.json'), 'utf8')).dict;
  for (const k of ['Miniapps', 'miniapp', 'recebendo dados', 'sem dados agora', 'Instalar miniapp de um arquivo',
    'Nenhum miniapp instalado. Um miniapp mostra no relógio dados da casa que o Home Assistant manda, como a lavadora, as luzes ou as tomadas.',
    'Escolha o .json do miniapp. Toque num miniapp para usar agora ou pôr no rodízio, que é o mesmo dos mostradores.'])
    assert(dict[k] && dict[k].length == 2 && dict[k].every(x => x), 'sem tradução: ' + k);
  console.log('painel: página Miniapps abaixo de Relógio, com o cartão do Home Assistant; Relógio sem miniapps; rodízio com selo');
}
// Declarações que não podem virar comentário, avisos que não apagam os endereços guardados e dicionário sem protótipo.
(async () => {
  // `let shareJs` colado no fim de um comentário `//` sumia da página: "Compartilhar meu dia" dava ReferenceError (desde a 1.14)
  assert(/^let shareJs=null;/m.test(html), 'let shareJs=null precisa começar a linha (não pode ficar dentro de um comentário)');
  const sc = html.slice(html.lastIndexOf('<script'), html.lastIndexOf('</script>'));
  for (const l of sc.split('\n')) assert(!/\/\/.*\blet \w+=/.test(l.replace(/'[^']*'|"[^"]*"|`[^`]*`/g, '')), 'declaração dentro de comentário: ' + l.slice(0, 80));
  // avisos: sem ler os endereços do relógio, salvar ou testar mandaria campos vazios e apagaria o que estava guardado
  const run = async (loadOk) => {
    const sent = [], el = { hkw: { value: '' }, hkb: { value: '' } };
    const code = html.slice(html.indexOf('let hooksOk=false'), html.indexOf('// ---------- configurações (salva sozinho)'));
    const f = new Function('$', 'fetch', 'post', 'toast', 't', code + ';return {hooksLoad,hooksSave,hookTest}');
    const api = f(id => el[id], async () => { if (!loadOk) throw new Error('rede'); return { json: async () => ({ w: 'http://a', b: 'http://b' }) }; },
      async (u, b) => { sent.push(b.toString()); return { ok: true, status: 200, text: async () => '200' }; }, () => {}, s => s);
    return { api, sent, el };
  };
  let r = await run(false);
  await r.api.hooksLoad(); await r.api.hookTest('w'); await r.api.hooksSave();
  assert.deepStrictEqual(r.sent, [], 'leitura falhou: não pode salvar nem testar');
  r = await run(true);
  await r.api.hooksLoad();
  assert.strictEqual(r.el.hkw.value, 'http://a');
  await r.api.hooksSave();
  assert.deepStrictEqual(r.sent, ['w=http%3A%2F%2Fa&b=http%3A%2F%2Fb']);
  // nome de mostrador "constructor": não é tradução
  assert(html.includes('I18N=Object.create(null)'), 'I18N precisa ser um objeto sem protótipo');
  console.log('painel: compartilhar declarado, avisos só salvam depois de ler os endereços e o dicionário ignora nomes como "constructor"');
})();
// Envio de GIF e de fotos (web/index.html): o relógio mostra só o canto superior esquerdo de GIF acima de 240×240 e
// recusa acima de 480×480 (album.cpp, gifcheck.h); o painel avisa antes de enviar e confere o espaço livre.
(async () => {
  const j = html.indexOf('async function gifDim('), gifDim = new Function('return ' + html.slice(j, html.indexOf('\n', j)))();
  const hdr = (w, h) => new Blob([Buffer.from([0x47, 0x49, 0x46, 0x38, 0x39, 0x61, w & 255, w >> 8, h & 255, h >> 8, 0, 0])]);
  assert.deepStrictEqual(await gifDim(hdr(240, 240)), [240, 240]);
  assert.deepStrictEqual(await gifDim(hdr(498, 280)), [498, 280]);
  assert.deepStrictEqual(await gifDim(new Blob([Buffer.from('GIF89')])), [0, 0]);   // arquivo curto: sem aviso, o relógio decide
  // a recusa e o aviso usam as mesmas medidas do firmware
  const gifc = fs.readFileSync(path.join(__dirname, '../smalltv-pomodoro/modulos/album.cpp'), 'utf8'), h = fs.readFileSync(path.join(__dirname, '../smalltv-pomodoro/gifcheck.h'), 'utf8');
  assert(/gifCheck\(gifCheckRead, &file, 480, 480\)/.test(gifc) && /int y = p->iY \+ p->y \+ gifOffY;\s+if \(y < 0 \|\| y >= 240\) return;/.test(gifc), 'o corte de GIF no relógio mudou: rever os avisos do painel');
  assert(h.includes('static const char *gifCheck('));
  const up = html.slice(html.indexOf("$('up').onchange="), html.indexOf("$('op').onsubmit="));
  assert(up.includes('gw>480||gh>480') && up.includes('gw>240||gh>240') && up.includes('room'), 'envio sem os avisos de GIF e de espaço');
  assert(html.includes("x.imageSmoothingQuality='high'"), 'foto reduzida sem suavização de qualidade');
  // o fluxo do envio, com o painel simulado: GIF só passa de 480 se o relógio aceitar, de 240 só com confirmação, e o espaço é conferido
  const flow = async (gifs, { free = 1e9, ok = true } = {}) => {
    const log = { sent: [], toasts: [], asked: [], alerts: [], gifDimCalls: 0 };
    const box = {};
    const code = html.slice(html.indexOf("$('up').onchange="), html.indexOf("$('op').onsubmit=")).replace(/^\$\('up'\)\.onchange=/, 'box.h=').replace(/;\s*$/, '');
    new Function('box', '$', 't', 'kb', 'toast', 'st', 'files', 'prep', 'fetch', 'confirm', 'gifDim', 'alert', code)(box, () => ({}), (s, ...a) => s.replace(/\{(\d)\}/g, (m, i) => a[i]),
      n => Math.round(n / 1024) + ' KB', m => log.toasts.push(m), { fs: { ok: true, total: free, used: 0 } }, () => {}, async f => (f.foto ? { blob: new Blob([Buffer.alloc(3000)]), name: f.name } : { blob: f, name: f.name }),
      async () => { log.sent.push(1); return { ok: true, text: async () => 'ok' }; }, m => (log.asked.push(m), ok), f => (log.gifDimCalls++, gifDim(f)), m => log.alerts.push(m));
    await box.h({ target: { files: gifs } });
    return log;
  };
  const g = (w, h, name = 'a.gif', size = 1000) => { const b = new Blob([Buffer.from([0x47, 0x49, 0x46, 0x38, 0x39, 0x61, w & 255, w >> 8, h & 255, h >> 8]), Buffer.alloc(size)]); b.name = name; return b; };
  let r = await flow([g(498, 280)]);
  assert.strictEqual(r.sent.length, 0); assert(r.alerts[0].includes('498×280') && r.alerts[0].includes('480×480'));   // recusado sem enviar
  r = await flow([g(498, 280, 'grande.gif'), g(240, 240, 'bom.gif')]);
  assert.strictEqual(r.sent.length, 1); assert(r.alerts[0].includes('grande.gif'));                                  // em lote, a recusa sobrevive ao "enviado" e ao próximo arquivo
  const foto = Object.assign(new Blob([Buffer.alloc(10)]), { name: 'a.jpg', foto: true });
  r = await flow([foto]);
  assert.strictEqual(r.sent.length, 1); assert.strictEqual(r.gifDimCalls, 0); assert.strictEqual(r.asked.length, 0);  // foto: não lê cabeçalho de GIF nem pergunta
  r = await flow([foto, foto, foto], { free: 16384, ok: true });
  assert.strictEqual(r.sent.length, 3); assert.strictEqual(r.asked.length, 1);                                       // sem espaço: uma pergunta por lote
  r = await flow([foto, foto, foto], { free: 16384, ok: false });
  assert.strictEqual(r.sent.length, 0); assert.strictEqual(r.asked.length, 1);                                       // "não" vale para o resto do lote
  r = await flow([g(300, 300)], { ok: false });
  assert.strictEqual(r.sent.length, 0); assert.strictEqual(r.asked.length, 1);                                       // cortado: não confirmou, não envia
  r = await flow([g(300, 300)], { ok: true });
  assert.strictEqual(r.sent.length, 1);                                                                              // cortado: confirmou
  r = await flow([g(240, 240)]);
  assert.strictEqual(r.sent.length, 1); assert.strictEqual(r.asked.length, 0);                                       // cabe: envia direto
  r = await flow([g(240, 240, 'a.gif', 40000)], { free: 30000, ok: false });
  assert.strictEqual(r.sent.length, 0); assert(r.asked[0].includes('Enviar mesmo assim'));                           // sem espaço: pergunta
  r = await flow([g(240, 240, 'a.gif', 5000), g(240, 240, 'b.gif', 5000)], { free: 16384 + 8192 + 4096, ok: false });
  assert.strictEqual(r.sent.length, 1); assert.strictEqual(r.asked.length, 1);                                       // o 1º cabe, o 2º já não (espaço descontado)
  const dict = JSON.parse(fs.readFileSync(path.join(__dirname, '../web/i18n.json'), 'utf8')).dict;
  for (const k of ['GIF de {0}×{1}: o relógio recusa acima de 480×480. Reduza o GIF antes de enviar.',
    'GIF de {0}×{1}: o relógio mostra só o canto superior esquerdo de 240×240. Enviar mesmo assim?', 'Só {0} livres e {1} necessários. Enviar mesmo assim?'])
    assert(dict[k] && dict[k].length == 2 && dict[k].every(x => x), 'sem tradução: ' + k);
  const note = [...html.matchAll(/<small>(Fotos são recortadas[^<]*)<\/small>/g)][0][1];
  assert(dict[note] && dict[note].every(x => x), 'a nota do álbum precisa de tradução');
  console.log('painel: avisa GIF acima de 240×240 (cortado) e de 480×480 (recusado), confere o espaço livre e reduz fotos com suavização alta');
})();
