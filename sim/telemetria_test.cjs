// Telemetria de recursos v1 (diag.minblk, stack, pack, nomem + minigráficos + "Copiar diagnóstico").
// Parte 1 (sempre): confere no código real que os campos novos são ADITIVOS (nenhuma chave do /api/state sumiu
//   ou mudou de nome), que o heap é varrido uma vez por pedido e nunca no laço, e que a lista branca do painel
//   ("Copiar diagnóstico") não leva cfg, IP, nome, cidade, latitude/longitude nem nome de arquivo.
// Parte 2 (com saída): gera um .cpp que compila o código real do diagnóstico e do último pedaço do sendState com um
//   String/ESP de mentira, confere o JSON (chaves, valores, uma passada do heap, minblk que só desce) e que o pedaço
//   cabe no j.reserve(). Uso: node sim/telemetria_test.cjs [saida.cpp]; depois g++ -std=c++17 saida.cpp.
const fs = require('fs'), path = require('path'), assert = require('assert');
const source = require('./fonte.cjs');
const html = fs.readFileSync(path.join(__dirname, '../web/index.html'), 'utf8');
const dict = JSON.parse(fs.readFileSync(path.join(__dirname, '../web/i18n.json'), 'utf8')).dict;
const cut = (sig, from = 0) => { const a = source.indexOf(sig, from); if (a < 0) throw Error('não achei ' + sig); const b = source.indexOf('\n}', a); if (b < 0) throw Error('sem fim: ' + sig); return source.slice(a, b + 2); };

// ---------- 1. campos aditivos ----------
const OLD = ('phase running remaining total cycle today streak best clock showing wx ok t min max h rin tzn off loff rise set desc fs l2 mig used cfg work short long every bri auto h12 tz lock theme mode delay color non nstart nend nbri won fahr rain beta lat lon city bgr inv flip vcom wdown tok tund pkg goal act lang nsun warm lcdg alexa rotmin rot ago ip last pv name heap blk pdraw pseq pshown ver diag rst exc epc rssi minheap frag up draw stall stallAt boot scr wifi wf time src ntp crash n r x va').split(' ');
const NEW = ['minblk', 'stack', 'ramfix', 'lps', 'pack', 'nomem', 'jpg', 'gif', 'strip'];
const sendState = cut('static void sendState() {'), diagJson = cut('static void diagJson('), crashJson = cut('static void crashJson(');
const keys = new Set([...(sendState + diagJson + crashJson).matchAll(/\\"(\w+)\\":/g)].map(m => m[1]));
for (const k of OLD) assert(keys.has(k), 'chave antiga sumiu do /api/state: ' + k + ' (painéis antigos leem esse JSON)');
assert.deepStrictEqual([...keys].filter(k => !OLD.includes(k)).sort(), [...NEW].sort(), 'o /api/state só pode ganhar minblk, stack, ramfix, lps, pack e nomem{jpg,gif,strip}');
// os campos novos ficam dentro de diag
const d = sendState + diagJson;  // diag abre no sendState (\"diag\":{), os campos vêm no diagJson
for (const k of ['minblk', 'stack', 'ramfix', 'lps', 'pack', 'nomem']) assert(d.includes('\\"' + k + '\\":'), k + ' fora de diag');

// uma só varredura do heap por pedido (getHeapStats), nenhuma das duas chamadas antigas no pedido, nada no laço
const heapFn = cut('static void diagHeap(');
assert.strictEqual((heapFn.match(/getHeapStats\(/g) || []).length, 1, 'diagHeap: um getHeapStats');
assert.strictEqual((sendState.match(/diagHeap\(/g) || []).length, 1, 'sendState: uma chamada de diagHeap');
for (const [n, f] of [['sendState', sendState], ['diagJson', diagJson]]) assert(!/getHeapFragmentation|getMaxFreeBlockSize|getFreeHeap|getHeapStats/.test(f), n + ' não chama o heap direto (só via diagHeap)');
assert(!/getHeapFragmentation|getMaxFreeBlockSize/.test(sendState + diagJson + heapFn), 'as duas chamadas antigas sumiram do pedido');
const loopFn = source.slice(source.indexOf('void loop() {'), source.indexOf('\n}', source.indexOf('void loop() {')));
for (const [n, f] of [['diagLoop', cut('void diagLoop() {')], ['loop', loopFn]])
  assert(!/getHeapStats|getMaxFreeBlockSize|getHeapFragmentation|diagBlkSample|diagHeap/.test(f), n + ': o heap só é varrido no /api/state e antes de foto/GIF/pacote/OTA (interrupções desligadas)');
// quem chama diagBlkSample: só os pontos pontuais combinados
const calls = [...source.matchAll(/^\s+diagBlkSample\(\);/gm)].length;
assert.strictEqual(calls, 5, 'diagBlkSample: foto (álbum), GIF, validJpg, pacote e OTA');
for (const fn of [cut('JpgResult validJpg('), cut('bool loadPackFile('), cut('bool showAlbumItem(')]) assert(/diagBlkSample\(\);/.test(fn));
const gifBranch = cut('bool showAlbumItem(');
assert.strictEqual((gifBranch.match(/diagBlkSample\(\);/g) || []).length, 2, 'showAlbumItem: uma amostra no GIF e uma na foto');
const upd = cut('void setupUpdate() {');
assert(upd.slice(upd.indexOf('UPLOAD_FILE_START'), upd.indexOf('UPLOAD_FILE_WRITE')).includes('diagBlkSample();'), 'OTA: amostra no início do envio');
assert(!/diagBlkSample/.test(cut('static void albumMakeRoom(')) && !/diagBlkSample/.test(cut('void packEnsureLoaded(')), 'nada a cada volta do laço');
// contadores de falta de memória: onde a falta vira aviso na tela
assert(/diagNoMem\(0\);/.test(gifBranch) && /diagNoMem\(1\);/.test(gifBranch), 'foto e GIF sem memória contados');
assert(fs.readFileSync(path.join(__dirname, '../smalltv-pomodoro/ui.cpp'), 'utf8').includes('uiOffscreenFail++'), 'faixa recusada contada no ui.cpp');
// nada novo na flash nem na rede: sem endpoint novo, sem gravação
assert(!/server\.on\("\/api\/(diag|telem|heap)/.test(source), 'sem endpoint novo');
assert(!/EEPROM|LittleFS|saveSettings/.test(diagJson + heapFn + cut('void diagBlkSample() {') + cut('void diagNoMem(')), 'o diagnóstico não grava em lugar nenhum');

// ---------- 2. painel: histórico só no navegador e lista branca ----------
const sp = html.slice(html.indexOf('// minigráficos:'), html.indexOf('// data-neg:'));
assert(sp.includes('HN=600'), 'anel de ~600 amostras');
assert(!/localStorage|sessionStorage|indexedDB|post\(|fetch\(|sendBeacon/.test(sp.replace(/\/\/[^\n]*/g, '').replace(/navigator\.clipboard/g, '')), 'o histórico fica só na memória da página: nada de storage nem rede nos gráficos');
assert(html.includes('if(s.diag){diagShow(s);spkPush(s)}'), 'os gráficos são alimentados pelo load() do poll existente');
const a = html.indexOf('const DKEY='), b = html.indexOf('function diagCopy');
const diagText = new Function(html.slice(a, b) + '\nreturn diagText')();
const SEN = ['SEGREDO-CIDADE', 'SEGREDO-NOME', '192.168.77.5', '-23.5478', 'SEGREDO-FUSO', 'SEGREDO-ARQUIVO', 'SEGREDO-SENHA', 'SEGREDO-ROT', 'SEGREDO-PKG'];
const state = {
  phase: 'idle', ver: '1.25.0', heap: 31200, blk: 17600, showing: '/image/SEGREDO-ARQUIVO.jpg', name: 'SEGREDO-NOME', clock: '10:21',
  cfg: { city: 'SEGREDO-CIDADE', lat: -23.5478, lon: -46.6361, tz: 'SEGREDO-FUSO', lock: 1, pkg: 'SEGREDO-PKG', rot: 'SEGREDO-ROT', pass: 'SEGREDO-SENHA' },
  alexa: { ago: 5, ip: '192.168.77.5', last: 1, min: 0 }, wx: { tzn: 'SEGREDO-CIDADE', desc: 'SEGREDO-CIDADE' }, fs: { used: 1, total: 2 },
  diag: { rst: 4, exc: 0, epc: '40201234', rssi: -61, minheap: 24100, minblk: 14200, frag: 12, stack: 3180, ramfix: 38496, lps: 58000, up: 3600, draw: 140, pack: 38, stall: 90, stallAt: 1,
    nomem: { jpg: 1, gif: 0, strip: 2 }, boot: { scr: 1200, wifi: 800, wf: 1, time: 900, src: 1, ntp: 700 },
    ip: '192.168.77.5', ssid: 'SEGREDO-NOME', city: 'SEGREDO-CIDADE', campoNovo: 'SEGREDO-CIDADE' },
  crash: { n: 1, r: 2, x: 29, epc: '40201234', va: '00000000', up: 120, ver: '1.24.9', ip: '192.168.77.5', msg: 'SEGREDO-CIDADE' },
};
const txt = diagText(state, { heap: 26000, blk: 14000, rssi: -74, ip: '192.168.77.5', nome: 'SEGREDO-NOME' });
for (const s of SEN) assert(!txt.includes(s), 'a lista branca vazou: ' + s);
assert(!/\b\d{1,3}(\.\d{1,3}){3}\b/.test(txt), 'IP no diagnóstico copiado');
assert(!/cfg|city|(lat|lon)=|name|ssid|alexa|tz=|pkg|rot=|showing/i.test(txt), 'campo pessoal no diagnóstico copiado: ' + txt);
for (const want of ['SmallTV Pomodoro 1.25.0', 'heap=31200 blk=17600', 'minheap=24100', 'minblk=14200', 'stack=3180', 'ramfix=38496', 'lps=58000', 'pack=38', 'nomem jpg=1 gif=0 strip=2', 'boot scr=1200', 'crash n=1 r=2 x=29 epc=40201234 va=00000000 up=120 ver=1.24.9', 'page-min heap=26000 blk=14000 rssi=-74'])
  assert(txt.includes(want), 'faltou no diagnóstico: ' + want);
// campo que o firmware ganhar um dia não entra sozinho, e valor de texto livre é limpo mesmo numa chave da lista
assert.strictEqual(diagText({ ...state, diag: { ...state.diag, outro: 'x' } }, {}), diagText(state, {}), 'chave fora da lista branca entrou');
assert(/^diag( \w+=[\w.-]*)+$/.test(diagText({ ...state, diag: { ...state.diag, epc: 'Rua das Flores, 12 (casa)' } }, {}).split('\n')[2]), 'q() deve tirar espaços e pontuação de qualquer valor');
assert.doesNotThrow(() => diagText({ ver: '1.25.0' }, {}), 'firmware antigo (sem diag.minblk/nomem/boot) não pode quebrar o texto');
// barra de RAM: fixa + em uso + livre fecha a DRAM de 80 KB (81920 B)
assert(html.includes('const T=81920,u=T-f-s.heap;') && html.includes('[f,u,s.heap]') && /0x3FFE8000u/.test(source) && /heap total = 81920 - ramfix/.test(source), 'conta da barra de RAM: fixa + (81920 - fixa - livre) + livre');
// lps (voltas do laço por segundo): só em diag e no texto copiado; nada na tela, sem % nem gráfico
assert(!/lps/.test(html.replace(/const DKEY=[^\n]*/, '')), 'lps não pode aparecer na tela do painel (só no texto copiado)');
// textos novos em en/es
for (const k of ['pacote', 'mínimo', 'pilha livre', 'sem memória (foto/GIF/desenho)', 'Maior bloco', 'atual', 'pior', 'Memória (RAM)', 'fixa', 'livre', 'Copiar diagnóstico', 'Diagnóstico copiado (sem dados pessoais)', 'Não consegui copiar: selecione o texto abaixo',
  'Gráficos: histórico só desde que esta página abriu (até 10 min), guardado só neste navegador.'])
  assert(dict[k] && dict[k].length === 2 && dict[k].every(x => x), 'sem tradução en/es: ' + k);
assert(html.includes('<small>Gráficos: histórico só desde que esta página abriu (até 10 min), guardado só neste navegador.</small>') && html.includes('>Copiar diagnóstico</button>'), 'texto fixo da página precisa bater com a chave do dicionário');
console.log('telemetria: campos aditivos (' + NEW.join(', ') + '), uma varredura do heap por pedido, lista branca sem cfg/IP/nome, textos en/es');

// ---------- 3. C++: JSON real do diagnóstico ----------
const out = process.argv[2];
if (!out) process.exit(0);
const reserve = +sendState.match(/j\.reserve\((\d+)\)/)[1];
const i0 = sendState.indexOf('j = F("\\"},\\"alexa\\":{\\"ago\\":");'), i1 = sendState.indexOf('jsonEnd(j);');
if (i0 < 0 || i1 < 0) throw Error('não achei o último pedaço do sendState');
const tail = sendState.slice(i0, i1);
const block = source.slice(source.indexOf('static uint32_t diagMinHeap'), source.indexOf('static void sendState() {'));
const line = re => { const m = source.match(re); if (!m) throw Error(String(re)); return m[0]; };
fs.writeFileSync(out, `#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <algorithm>
#define F(s) (s)
#define PSTR(s) (s)
#define snprintf_P snprintf
#define FW_VERSION "1.25.0"
using std::min;
struct String {  // como o do Arduino: += de um byte sem sinal acrescenta o NÚMERO, de char acrescenta o caractere
  std::string s;
  String() {} String(const char *p) : s(p) {}
  String &operator=(const char *p) { s = p; return *this; }
  String &operator+=(const char *p) { s += p; return *this; }
  String &operator+=(const String &o) { s += o.s; return *this; }
  String &operator+=(char c) { s += c; return *this; }
  String &operator+=(unsigned char v) { s += std::to_string(v); return *this; }
  String &operator+=(short v) { s += std::to_string(v); return *this; }
  String &operator+=(unsigned short v) { s += std::to_string(v); return *this; }
  String &operator+=(int v) { s += std::to_string(v); return *this; }
  String &operator+=(unsigned v) { s += std::to_string(v); return *this; }
  String &operator+=(long v) { s += std::to_string(v); return *this; }
  String &operator+=(unsigned long v) { s += std::to_string(v); return *this; }
  String toString() const { return *this; }
};
struct rst_info { uint32_t reason, exccause, epc1; };
struct CrashRec { uint32_t magic; uint8_t n, reason, exc, pad; uint32_t epc, vaddr, up; char ver[8]; };
static CrashRec crashRec;
static uint32_t g_now = 100000, g_hFree, g_hMax; static uint8_t g_hFrag; static int g_stats = 0, g_max = 0, g_free = 0, g_rssi = -61;
static rst_info g_rst = {4, 0, 0};
static struct { void getHeapStats(uint32_t *f, uint32_t *m, uint8_t *fr) { g_stats++; if (f) *f = g_hFree; if (m) *m = g_hMax; if (fr) *fr = g_hFrag; }
  uint32_t getMaxFreeBlockSize() { g_max++; return g_hMax; } uint32_t getFreeHeap() { g_free++; return g_hFree; } uint32_t getFreeContStack() { return 3180; }
  const rst_info *getResetInfoPtr() { return &g_rst; } } ESP;
static struct { bool isConnected() { return true; } int RSSI() { return g_rssi; } } WiFi;
uint32_t millis() { return g_now; }
static char *_heap_start = (char *)(uintptr_t)0x3FFF1660u;  // o do .elf compilado: RAM fixa = 0x3FFF1660 - 0x3FFE8000 = 38496
uint16_t fullDrawMs = 140, packLoadMs = 38; uint8_t uiOffscreenFail = 2;
uint32_t bootScreenMs = 1200, bootWifiMs = 800, bootTimeMs = 900, bootNtpMs = 700; uint8_t bootTimeSrc = 1, bootWifiPath = 1;
${line(/uint8_t diagWhere = 0;/)}
${line(/static uint32_t diagStall = 0;/)}
${line(/static uint8_t diagStallAt = 0;/)}
// alexa e o resto do último pedaço
static uint32_t alexaSeen = 5, previewUntil = 0; static uint8_t alexaLast = 1, alexaMin = 25; static char lanName[12] = "cozinha-dav", netName[16] = "smalltv-abcdef"; static uint16_t pomoDrawMs = 120, phaseSeq = 1, shownSeq = 1;
static struct { String toString() const { return String("192.168.100.200"); } } alexaIp;
static bool previewActive() { return false; }
${block}
static void tail(String &j) {
${tail}
}
static bool jsonOk(const char *p) {  // aspas e chaves/colchetes balanceados, sem lixo no fim
  int depth = 0; bool str = false;
  for (; *p; p++) {
    if (str) { if (*p == '\\\\') p++; else if (*p == '"') str = false; continue; }
    if (*p == '"') str = true; else if (*p == '{' || *p == '[') depth++; else if (*p == '}' || *p == ']') { if (--depth < 0) return false; }
  }
  return !str && depth == 0;
}
static long num(const std::string &j, const char *key) {  // primeiro "key":N
  size_t p = j.find(std::string("\\"") + key + "\\":"); return p == std::string::npos ? -999999 : atol(j.c_str() + p + strlen(key) + 3);
}
#define CHECK(c) do { if (!(c)) { printf("FALHA linha %d: %s\\n", __LINE__, #c); return 1; } } while (0)
int main() {
  crashRec = {CRASH_MAGIC_PLACEHOLDER, 255, 2, 29, 0, 0x40201234, 0, 4294967295u, "1.24.13"};
  g_hFree = 31200; g_hMax = 17600; g_hFrag = 12;
  String j; tail(j);
  std::string doc = std::string("{\\"cfg\\":{\\"rot\\":\\"") + j.s;
  CHECK(jsonOk(doc.c_str()));
  // antigos iguais
  CHECK(num(doc, "rssi") == -61 && num(doc, "frag") == 12 && num(doc, "heap") == 31200 && num(doc, "blk") == 17600 && num(doc, "draw") == 140);
  CHECK(num(doc, "pdraw") == 120 && num(doc, "pseq") == 1 && num(doc, "pshown") == 1 && num(doc, "up") == 100 && num(doc, "stall") == 0 && num(doc, "rst") == 4);
  CHECK(doc.find("\\"ip\\":\\"192.168.100.200\\"") != std::string::npos && doc.find("\\"crash\\":{\\"n\\":255,") != std::string::npos && doc.find("\\"ver\\":\\"1.25.0\\"}") != std::string::npos);
  // novos
  CHECK(num(doc, "minblk") == 17600 && num(doc, "stack") == 3180 && num(doc, "pack") == 38 && num(doc, "ramfix") == 38496);
  CHECK(num(doc, "jpg") == 0 && num(doc, "gif") == 0 && num(doc, "strip") == 2);
  // uma varredura por pedido: getHeapStats uma vez; nem getMaxFreeBlockSize nem getHeapFragmentation (nem existe no ESP de mentira)
  CHECK(g_stats == 1 && g_max == 0);
  // minblk só desce: um pedido com bloco menor baixa, um com bloco maior não sobe
  g_hMax = 9000; String j2; tail(j2); CHECK(num(j2.s, "minblk") == 9000 && num(j2.s, "blk") == 9000 && g_stats == 2);
  g_hMax = 20000; String j3; tail(j3); CHECK(num(j3.s, "minblk") == 9000 && num(j3.s, "blk") == 20000);
  // amostra pontual (antes de foto/GIF/pacote/OTA): baixa o mínimo sem o pedido, e é o único jeito de pegar um vale entre dois pedidos
  g_hMax = 4000; diagBlkSample(); g_hMax = 20000; String j4; tail(j4); CHECK(num(j4.s, "minblk") == 4000 && g_max == 1);
  // diagLoop (a cada volta) não varre o heap
  int s0 = g_stats, m0 = g_max; for (int i = 0; i < 1000; i++) { g_now += 10; diagLoop(); } CHECK(g_stats == s0 && g_max == m0);
  // lps: voltas por segundo. Passos de 10 ms = 100 voltas/s; o pedido só lê o número, sem varrer nada
  { String jl; g_hMax = 20000; tail(jl); CHECK(num(jl.s, "lps") >= 99 && num(jl.s, "lps") <= 101); }
  // contadores: saturam em 255 e ignoram tipo inválido
  for (int i = 0; i < 300; i++) diagNoMem(0); diagNoMem(1); diagNoMem(1); diagNoMem(7);
  String j5; tail(j5); CHECK(num(j5.s, "jpg") == 255 && num(j5.s, "gif") == 2 && num(j5.s, "strip") == 2);
  // pior caso de tamanho do último pedaço (números de 10 dígitos onde couber) cabe no j.reserve(${reserve}) do sendState
  g_hFree = 99999; g_hMax = 99999; g_hFrag = 99; diagMinHeap = 4294967295u; g_rssi = -100; g_now = 4294967295u; diagStall = 4294967295u; diagStallAt = 2;
  bootScreenMs = bootWifiMs = bootTimeMs = bootNtpMs = 4294967295u; fullDrawMs = packLoadMs = 65535; pomoDrawMs = phaseSeq = shownSeq = 65535; alexaMin = 255; uiOffscreenFail = 255;
  crashRec.reason = crashRec.exc = 255; crashRec.epc = crashRec.vaddr = crashRec.up = 4294967295u; memcpy(crashRec.ver, "1.24.13", 8);
  String w; tail(w); CHECK(jsonOk((std::string("{\\"cfg\\":{\\"rot\\":\\"") + w.s).c_str()));
  printf("telemetria: JSON do diagnóstico ok; último pedaço do /api/state no pior caso = %zu bytes (j.reserve(${reserve}))\\n", w.s.size());
  CHECK((int)w.s.size() <= ${reserve});
  return 0;
}
`.replace('CRASH_MAGIC_PLACEHOLDER', '0x43524153'));
