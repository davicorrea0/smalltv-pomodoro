// Sessões guardadas no relógio: modelo real do painel, backup/restauração, traduções, XSS e ligação das rotas.
const fs = require('fs'), path = require('path'), assert = require('assert');
const html = fs.readFileSync(path.join(__dirname, '../web/index.html'), 'utf8');
const portal = fs.readFileSync(path.join(__dirname, '../smalltv-pomodoro/modulos/web_portal.cpp'), 'utf8');
const storage = fs.readFileSync(path.join(__dirname, '../smalltv-pomodoro/modulos/storage_stats.cpp'), 'utf8');
const dict = JSON.parse(fs.readFileSync(path.join(__dirname, '../web/i18n.json'), 'utf8')).dict;

const line = s => { const i = html.indexOf(s); assert(i >= 0, 'não achei ' + s); return html.slice(i, html.indexOf('\n', i)); };
const fn = s => { const i = html.indexOf('function ' + s + '('); assert(i >= 0, s); const a = html.indexOf('{', i); let n = 1, q = '', esc = false, k = a + 1; for (; n && k < html.length; k++) { const c = html[k]; if (q) { if (esc) esc = false; else if (c == '\\') esc = true; else if (c == q) q = ''; } else if (c == '"' || c == "'" || c == '`') q = c; else if (c == '{') n++; else if (c == '}') n--; } return html.slice(i, k); };

// Fórmula: a pausa final conta no total finito; cycles=0 não inventa duração.
const mins = new Function('return ' + fn('sessionMinutes'))();
assert.strictEqual(mins({ work: 50, short: 10, long: 25, every: 3, cycles: 3 }), 195);
assert.strictEqual(mins({ work: 25, short: 5, long: 20, every: 2, cycles: 2 }), 75);
assert.strictEqual(mins({ work: 25, short: 5, long: 20, every: 2, cycles: 0 }), 0);
assert(fn('sessionRender').includes("x.cycles?sessionDuration(sessionMinutes(x)):t('sem fim')"), 'chip sem fim não pode mostrar total falso');

// Executa o carregamento real com uma árvore DOM mínima.
const consts = line('const DEEP_FOCUS=') + '\n' + fn('sessionMinutes') + '\n' + fn('sessionDuration');
const a = html.indexOf('let sessOk=false'), b = html.indexOf('async function applySession', a);
assert(a > 0 && b > a);
const sessionCode = html.slice(a, b);
const make = resposta => {
  const els = { sessions: { children: [], appendChild(x) { this.children.push(x); } }, sessedit: {}, sesswarn: {}, cyclesum: {} };
  let fetch = async () => { if (resposta instanceof Error) throw resposta; return { ok: resposta.ok !== false, status: resposta.status || 500, json: async () => resposta.body }; };
  const el = (tag, cls, txt) => ({ tag, className: cls, textContent: txt == null ? '' : txt, append(...x) { this.children = x; }, classList: { toggle() {} }, addEventListener() {} });
  const api = new Function('$', 'fetch', 't', 'el', 'TextEncoder', 'st', 'applySession', consts + '\n' + sessionCode + ';return {sessLoad,sessionRender,state:()=>({sessOk,sessSet,SESSIONS})}')(
    id => els[id], fetch, (s, ...v) => s.replace(/\{(\d)\}/g, (_, i) => v[i]), el, TextEncoder, null, () => {});
  return { api, els };
};

(async () => {
  let x = make({ body: { set: 0, l: [] } }); await x.api.sessLoad();
  assert.strictEqual(x.api.state().sessOk, true); assert.strictEqual(x.api.state().sessSet, false); assert.deepStrictEqual(x.api.state().SESSIONS.map(s => s.name), ['Deep focus', 'Gentle focus']);
  // chips não são recriados a cada poll (1 s): só a classe muda; recriar derrubaria o toque e o foco do teclado
  {
    const y = make({ body: { set: 0, l: [] } }); await y.api.sessLoad();
    const n0 = y.els.sessions.children.slice();
    y.api.sessionRender({ cfg: { work: 50, short: 10, long: 25, every: 3, cycles: 3 } }); y.api.sessionRender(null);
    assert.strictEqual(y.els.sessions.children.length, 2, 'sessionRender recriou os chips sem a lista mudar');
    assert(y.els.sessions.children.every((c, i) => c === n0[i]), 'os nós dos chips têm de ser os mesmos');
  }
  x = make({ body: { set: 1, l: [{ name: 'Minha', work: 40, short: 5, long: 15, every: 3, cycles: 2 }] } }); await x.api.sessLoad();
  assert.deepStrictEqual(x.api.state().SESSIONS.map(s => s.name), ['Minha']);
  x = make({ body: { set: 1, l: [] } }); await x.api.sessLoad();
  assert.deepStrictEqual(x.api.state().SESSIONS, [], 'set:1 vazio não restaura exemplos');
  x = make(new Error('rede')); await x.api.sessLoad();
  assert.strictEqual(x.api.state().sessOk, false); assert.strictEqual(x.els.sessedit.disabled, true); assert(x.els.sesswarn.textContent);

  // Backup só acrescenta o campo após GET válido com set:1; restauração só cria a etapa quando a chave existe (vazia inclusive).
  const backup = fn('backup'), restore = html.slice(html.indexOf("$('rbk').onchange"), html.indexOf('// ---------- avisos de fim de fase'));
  assert(backup.includes("fetch('/api/sessoes')") && backup.includes('if(sj&&sj.set)b.sessoes=sj.l'));
  const monta = new Function('sj', 'const b={};if(sj&&sj.set)b.sessoes=sj.l;return b');
  assert(!Object.hasOwn(monta(null), 'sessoes') && !Object.hasOwn(monta({ set: 0 }), 'sessoes'));
  assert.deepStrictEqual(monta({ set: 1, l: [] }).sessoes, []);
  assert(restore.includes("Object.prototype.hasOwnProperty.call(b,'sessoes')") && restore.includes("post('/api/sessoes'") && restore.includes('sessionCheck(b.sessoes)'));
  assert.strictEqual(Object.prototype.hasOwnProperty.call({}, 'sessoes'), false);
  assert.strictEqual(Object.prototype.hasOwnProperty.call({ sessoes: [] }, 'sessoes'), true);

  // Nomes e valores nunca entram em HTML nem em atributos de evento montados como texto.
  const xss = html.slice(html.indexOf('const DEEP_FOCUS='), html.indexOf('function tab('));
  // inclui o editor e a restauração, onde o nome do usuário também passa
  const edit = html.slice(html.indexOf('async function sessEdit'), html.indexOf("$('sessedit').onclick")), xss2 = xss + edit + restore;
  assert(edit.length > 500, 'trecho do editor não achado');
  assert(!xss2.includes('innerHTML'), 'sessões não podem usar innerHTML');
  assert(!/setAttribute\(\s*['"]on/i.test(xss2), 'sessões não podem montar atributo on*');
  // atributo onclick="..." montado como texto com dados; "x.onclick=()=>" (propriedade com closure) é o jeito certo
  assert(!/onclick\s*=\s*["'`][^"'`]*(?:name|work|short|long|every|cycles)/i.test(xss2), 'sessões não podem montar onclick com dados');

  const chaves = ['Sessões', 'Editar sessões', 'Não foi possível ler as sessões do relógio.', 'sem fim', 'Focos de {0} min, sem fim',
    'Lista de sessões inválida', 'Limite de 8 sessões', 'Use de 1 a 24 caracteres, sem espaços nas pontas',
    'Os nomes não podem se repetir', 'Número fora da faixa', 'A lista passa de 1024 bytes', 'Adicionar', 'Salvar', 'Sessões salvas'];
  for (const k of chaves) assert(dict[k] && dict[k].length == 2 && dict[k].every(Boolean), 'sem pt/en/es: ' + k);
  assert.deepStrictEqual(dict['Sessões'], ['Focus plans', 'Sesiones']);
  assert.deepStrictEqual(dict['Editar sessões'], ['Edit focus plans', 'Editar sesiones']);

  // Não há shim completo do LittleFS/ESP8266WebServer para executar estas rotas; cobre a ligação do handler real.
  const get = portal.slice(portal.indexOf('server.on(F("/api/sessoes"), HTTP_GET'), portal.indexOf('server.on(F("/api/sessoes"), HTTP_POST'));
  const post = portal.slice(portal.indexOf('server.on(F("/api/sessoes"), HTTP_POST'), portal.indexOf('server.on(F("/api/statsimport"'));
  assert(get.includes('readOk()') && !get.includes('guard()'));
  assert(post.includes('guard()') && post.includes('sessoesValidar') && post.includes('/sessoes.tmp') && post.includes('LittleFS.rename') && post.includes('sendSessoes()'));
  assert(portal.includes('{\\"set\\":0,\\"l\\":[]}') && portal.includes('memoria indisponivel'));
  assert(storage.includes('p == "/sessoes.txt"') && storage.includes('p == "/sessoes.tmp"'));
  assert(line('const STG=').includes("'/sessoes.txt'"));
  const del = portal.slice(portal.indexOf('server.on(F("/api/delete")'), portal.indexOf('server.on(F("/api/storage")'));
  const fsdel = portal.slice(portal.indexOf('server.on(F("/api/fsdelete")'), portal.indexOf('server.on(F("/api/show")'));
  assert(del.includes('mediaPath(f)') && fsdel.includes('fsKeep(f)'));
  console.log('sessoes_lista_painel: ok (estados, backup/restauração, idiomas, XSS e rotas)');
})().catch(e => { console.error(e); process.exit(1); });
