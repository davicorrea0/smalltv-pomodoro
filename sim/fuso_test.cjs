// Fuso horário: o que o relógio faz com a regra POSIX do dicionário do painel (web/tz.json, nome IANA -> POSIX).
//  - A: Europe/Paris, America/Sao_Paulo, America/New_York e Australia/Sydney mudam de hora nos instantes certos
//       (um segundo antes e depois de cada troca), com a regra que o botão/lista do painel grava;
//  - B: o sintoma do relato (relógio 1 h atrasado no verão da França) é o de uma regra FIXA "<+01>-1", e o painel avisa
//       dela mesmo no inverno, quando o deslocamento de agora ainda bate;
//  - C: toda regra do dicionário cabe no cfg.tz do firmware e dá o mesmo deslocamento que o fuso IANA (Intl) em 2026-2027;
//  - D: o seletor do painel (lista por nome, cidade do clima, aviso) e os textos pt/en/es.
// O avaliador POSIX abaixo faz o que o tzset/localtime do newlib faz com regras Mm.w.d (o único formato do dicionário).
// Conferido à parte contra o glibc (setenv TZ + localtime_r) em todos os fusos e instantes deste teste.
const fs = require('fs'), path = require('path'), assert = require('assert');
const web = p => path.join(__dirname, '../web', p);
const TZ = JSON.parse(fs.readFileSync(web('tz.json'), 'utf8')), html = fs.readFileSync(web('index.html'), 'utf8');
const dict = JSON.parse(fs.readFileSync(web('i18n.json'), 'utf8')).dict;

// ---- avaliador de regra POSIX TZ: deslocamento (hora local - UTC) em segundos num instante UTC ----
const hms = s => { const m = /^([+-]?)(\d{1,3})(?::(\d{2}))?(?::(\d{2}))?$/.exec(s); assert(m, 'hora inválida: ' + s); return (m[1] == '-' ? -1 : 1) * (+m[2] * 3600 + (+m[3] || 0) * 60 + (+m[4] || 0)); };
function parse(rule) {
  const m = /^(<[^>]+>|[A-Za-z]{3,})([+-]?\d{1,2}(?::\d{2}(?::\d{2})?)?)(?:(<[^>]+>|[A-Za-z]{3,})([+-]?\d{1,2}(?::\d{2}(?::\d{2})?)?)?,(M\d+\.\d\.\d(?:\/[+-]?[\d:]+)?),(M\d+\.\d\.\d(?:\/[+-]?[\d:]+)?))?$/.exec(rule);
  assert(m, 'regra fora do formato do dicionário: ' + rule);
  const std = -hms(m[2]);  // POSIX: positivo = oeste de Greenwich
  if (!m[3]) return { std };
  const rl = r => { const x = /^M(\d+)\.(\d)\.(\d)(?:\/(.+))?$/.exec(r); return { mo: +x[1], w: +x[2], d: +x[3], t: x[4] ? hms(x[4]) : 7200 }; };
  return { std, dst: m[4] ? -hms(m[4]) : std + 3600, a: rl(m[5]), b: rl(m[6]) };
}
function transicao(y, r) {  // dia da regra Mm.w.d no ano y, como segundos "locais" desde a época + hora da regra
  const first = new Date(Date.UTC(y, r.mo - 1, 1)).getUTCDay(), dim = new Date(Date.UTC(y, r.mo, 0)).getUTCDate();
  let day = 1 + (r.d - first + 7) % 7 + (r.w - 1) * 7; if (day > dim) day -= 7;
  return Date.UTC(y, r.mo - 1, day) / 1000 + r.t;
}
function offsetOf(p, t) {
  if (p.dst === undefined) return p.std;
  const y = new Date(t * 1000).getUTCFullYear(), s = transicao(y, p.a) - p.std, e = transicao(y, p.b) - p.dst;
  const dst = s < e ? t >= s && t < e : !(t >= e && t < s);
  return dst ? p.dst : p.std;
}
const off = (rule, iso) => offsetOf(parse(rule), Date.parse(iso) / 1000);
const H = 3600;

// ---- A: os quatro fusos pedidos, um segundo antes e depois de cada troca ----
{
  const casos = [
    // [fuso, instante UTC, deslocamento esperado]
    ['Europe/Paris', '2026-03-29T00:59:59Z', 1 * H], ['Europe/Paris', '2026-03-29T01:00:00Z', 2 * H],      // 02:00 -> 03:00 CEST
    ['Europe/Paris', '2026-07-15T12:00:00Z', 2 * H],
    ['Europe/Paris', '2026-10-25T00:59:59Z', 2 * H], ['Europe/Paris', '2026-10-25T01:00:00Z', 1 * H],      // 03:00 -> 02:00 CET
    ['Europe/Paris', '2026-11-04T12:00:00Z', 1 * H],
    ['America/Sao_Paulo', '2026-01-15T12:00:00Z', -3 * H], ['America/Sao_Paulo', '2026-07-15T12:00:00Z', -3 * H],  // sem horário de verão
    ['America/New_York', '2026-03-08T06:59:59Z', -5 * H], ['America/New_York', '2026-03-08T07:00:00Z', -4 * H],
    ['America/New_York', '2026-11-01T05:59:59Z', -4 * H], ['America/New_York', '2026-11-01T06:00:00Z', -5 * H],
    ['Australia/Sydney', '2026-04-04T15:59:59Z', 11 * H], ['Australia/Sydney', '2026-04-04T16:00:00Z', 10 * H],   // fim do verão (abril)
    ['Australia/Sydney', '2026-10-03T15:59:59Z', 10 * H], ['Australia/Sydney', '2026-10-03T16:00:00Z', 11 * H],   // começo (outubro)
    ['Australia/Sydney', '2026-01-15T12:00:00Z', 11 * H], ['Australia/Sydney', '2026-07-15T12:00:00Z', 10 * H]];
  for (const [z, iso, esp] of casos) {
    assert(TZ[z], 'fuso fora do dicionário: ' + z);
    assert.strictEqual(off(TZ[z], iso), esp, `${z} ${iso}: regra ${TZ[z]}`);
  }
  assert.strictEqual(TZ['Europe/Paris'], 'CET-1CEST,M3.5.0,M10.5.0/3');
  assert.strictEqual(TZ['America/Sao_Paulo'], '<-03>3');
  console.log(`fuso: ${casos.length} instantes em Paris, São Paulo, Nova York e Sydney, antes e depois das trocas, com a regra do dicionário`);
}

// ---- B: regra fixa = 1 h de atraso no verão; o painel avisa mesmo quando o deslocamento de agora bate ----
const painel = new Function(html.slice(html.indexOf('const tzDiff='), html.indexOf('// seletor:')) + ';return{tzDiff,tzList}')();
{
  const fixa = '<+01>-1', paris = TZ['Europe/Paris'];
  assert.strictEqual(off(paris, '2026-07-15T12:00:00Z') - off(fixa, '2026-07-15T12:00:00Z'), H, 'no verão a regra fixa fica 1 h atrasada');
  assert.strictEqual(off(paris, '2026-11-04T12:00:00Z'), off(fixa, '2026-11-04T12:00:00Z'), 'no inverno as duas dão a mesma hora');
  const { tzDiff } = painel;
  assert.strictEqual(tzDiff(fixa, paris, 7200, 3600), 1, 'verão: deslocamento diferente');
  assert.strictEqual(tzDiff(fixa, paris, 3600, 3600), 2, 'inverno: bate hoje, mas a regra fixa vai errar em março');
  assert.strictEqual(tzDiff(paris, paris, 3600, 3600), 0);
  assert.strictEqual(tzDiff('CET-1CEST,M3.5.0/2,M10.5.0/3', paris, 3600, 3600), 0, 'regra equivalente escrita à mão não é cobrada');
  assert.strictEqual(tzDiff('<-03>3', TZ['America/Sao_Paulo'], -10800, -10800), 0, 'fuso sem horário de verão: nada a avisar');
  assert.strictEqual(tzDiff('<-03>3', undefined, null, null), 0, 'sem cidade/tabela: nada a avisar');
  console.log('fuso: regra fixa <+01>-1 fica 1 h atrasada no verão da França e o painel avisa, até no inverno');
}

// ---- C: o dicionário inteiro contra o fuso IANA (Intl), 2026-2027, duas amostras por dia ----
{
  const nomes = Object.keys(TZ).filter(k => k[0] != '_');
  assert(nomes.length > 400, 'dicionário pequeno demais: ' + nomes.length);
  let maxLen = 0;
  for (const z of nomes) { maxLen = Math.max(maxLen, TZ[z].length); parse(TZ[z]); }
  assert(maxLen <= 47, `regra de ${maxLen} caracteres não cabe em cfg.tz[48]`);
  // o POSIX não representa estes (horário de verão que muda de ano para ano, ou invertido no Ramadã)
  const NAO_CABE = new Set(['Africa/Casablanca', 'Africa/El_Aaiun', 'Asia/Gaza', 'Asia/Hebron']);
  // o dicionário vem da tzdata 2026e e traz regra fixa (CST6, MST7, EST5) onde o ICU do Node, mais antigo, ainda tem horário de
  // verão: não dá para conferir contra o Intl (a regra do dicionário é a da própria tzdata, conferida no WSL)
  const ICU_ANTIGO = new Set(['America/Edmonton', 'America/Inuvik', 'America/Vancouver', 'America/Winnipeg']);
  const ini = Date.UTC(2026, 0, 1) / 1000, fim = Date.UTC(2028, 0, 1) / 1000, ruins = []; let conferidos = 0;
  for (const z of nomes) {
    if (NAO_CABE.has(z) || ICU_ANTIGO.has(z)) continue;
    let fmt; try { fmt = new Intl.DateTimeFormat('en-US', { timeZone: z, timeZoneName: 'longOffset' }); } catch (e) { continue; }  // ICU sem esse nome
    const p = parse(TZ[z]); conferidos++;
    for (let t = ini + 30 * 60; t < fim; t += 12 * H) {
      const m = /GMT(?:([+-])(\d\d):(\d\d))?/.exec(fmt.formatToParts(t * 1000).find(x => x.type == 'timeZoneName').value);
      const esperado = m[1] ? (m[1] == '-' ? -1 : 1) * (+m[2] * 3600 + +m[3] * 60) : 0;
      if (offsetOf(p, t) != esperado) { ruins.push(`${z} ${new Date(t * 1000).toISOString()} regra ${TZ[z]}: ${offsetOf(p, t)} != ${esperado}`); break; }
    }
  }
  assert.deepStrictEqual(ruins, [], 'regras do dicionário que não dão o deslocamento do fuso IANA:\n' + ruins.join('\n'));
  assert(conferidos > 400, 'poucos fusos conferidos: ' + conferidos);
  console.log(`fuso: ${conferidos} de ${nomes.length} regras do dicionário dão o deslocamento do fuso IANA em 2026-2027 (as outras ${nomes.length - conferidos}: POSIX não representa ou o ICU do Node é mais antigo); maior regra: ${maxLen} de 47 caracteres`);
}

// ---- D: seletor do painel ----
{
  const { tzList } = painel, g = tzList(TZ), mapa = Object.fromEntries(g);
  assert.deepStrictEqual(g.map(x => x[0]), [...g.map(x => x[0])].sort(), 'continentes em ordem');
  assert(!('Etc' in mapa), 'Etc/ não entra na lista');
  assert(mapa.Europe.some(([v, l]) => v == 'Europe/Paris' && l == 'Paris'));
  assert(mapa.America.some(([v, l]) => v == 'America/Sao_Paulo' && l == 'Sao Paulo'));
  assert(mapa.America.some(([v, l]) => v == 'America/Argentina/Buenos_Aires' && l == 'Argentina/Buenos Aires'));
  for (const [, a] of g) assert.deepStrictEqual(a.map(x => x[1]), [...a.map(x => x[1])].sort((x, y) => x.localeCompare(y)));
  assert.strictEqual(g.reduce((n, x) => n + x[1].length, 0), Object.keys(TZ).filter(k => k[0] != '_' && !k.startsWith('Etc/')).length);
  // o botão antigo sumiu, a lista e o campo avançado existem, o POSIX continua gravando pelo mesmo campo
  assert(!/tzfix|tzFix/.test(html), 'o botão "ajustar pela cidade" foi substituído pela lista');
  assert(html.includes('<select id="tzsel">') && html.includes('<details id="tzadv">') && /<details id="tzadv">[\s\S]*data-k="tz"[\s\S]*<\/details>/.test(html));
  // fuso fora da tabela ou sem a tabela: não grava; a regra gravada é conferida no que o relógio devolve
  assert(html.includes('if(!x.r)throw x.e;return save({tz:x.r}).then(s=>{if(s.cfg.tz!==x.r)throw 1;'), 'tzSet confere a regra gravada');
  assert(html.includes("tzRule(p.timezone).then(x=>save(Object.assign({lat:p.latitude,lon:p.longitude,city:name,won:1},x.r?{tz:x.r}:{}))"), 'a cidade grava a regra da tabela, nunca outra');
  assert(!/'<'\s*\+|"<"\s*\+|\+\s*'>'/.test(html.slice(html.indexOf('// ---------- fuso: regra POSIX'), html.indexOf('// ---------- diagnóstico'))), 'o painel não monta regra POSIX a partir de deslocamento');
  // todo texto novo (dinâmico e fixo) tem inglês e espanhol
  const sec = html.slice(html.indexOf('// ---------- fuso: regra POSIX'), html.indexOf('// ---------- diagnóstico'));
  const chaves = [...sec.matchAll(/\bt\('((?:[^'\\]|\\.)*)'/g)].map(m => m[1]);
  assert(chaves.length >= 10, 'achou os textos do trecho');
  for (const k of [...chaves, 'Fuso horário', 'Avançado: regra POSIX do fuso', 'Fuso (POSIX TZ)', 'Fuso ajustado: {0}', 'Cidade: ',
    'Só se o seu fuso não estiver na lista. Brasília <-03>3 · Manaus <-04>4 · Lisboa WET0WEST,M3.5.0/1,M10.5.0'])
    assert(dict[k] && dict[k].length == 2 && dict[k].every(x => x), 'sem tradução: ' + k);
  for (const k of Object.keys(dict)) if (/^Fuso da cidade: |^Ajustar o fuso pela cidade$|^Automático: ao escolher a cidade/.test(k)) assert.fail('texto do botão antigo ficou no dicionário: ' + k);
  console.log(`fuso: seletor do painel com ${g.length} continentes, textos em pt/en/es e gravação conferida`);
}
