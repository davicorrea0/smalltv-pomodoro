// O desenho do relógio (makeState, a cada 100 ms) não pode alocar para montar o texto do IP: o
// WiFi.localIP().toString() faz malloc/free (StreamString::reserve) a cada quadro. O texto é refeito só
// quando o endereço muda (ipString). Confere no código real de main.cpp e a conta do formato.
const source = require('./fonte.cjs');
const a = source.indexOf('static const char *ipString() {'), b = source.indexOf('\n}', a);
if (a < 0 || b < 0) throw Error('não achei ipString() em main.cpp');
const f = source.slice(a, b);
if (!/ip != ipLast/.test(f) || !/snprintf(_P)?\(ipText/.test(f)) throw Error('ipString deve refazer o texto só quando o IP muda');
if (/toString\(\)/.test(f)) throw Error('ipString não pode usar toString()');
const m = source.slice(source.indexOf('static UiState makeState() {'), source.indexOf('return s;', source.indexOf('static UiState makeState() {')));
if (/localIP\(\)\.toString\(\)/.test(m) || !/s\.ip = ipString\(\)/.test(m)) throw Error('makeState deve usar ipString(), sem toString() a cada desenho');
// mesma conta do snprintf: IPAddress é um uint32 com o primeiro número no byte baixo
const fmt = ip => `${ip & 255}.${(ip >>> 8) & 255}.${(ip >>> 16) & 255}.${ip >>> 24}`;
const num = (a, b2, c, d) => (a | b2 << 8 | c << 16 | d << 24) >>> 0;
for (const [ip, want] of [[num(192, 168, 1, 10), '192.168.1.10'], [0, '0.0.0.0'], [num(255, 255, 255, 255), '255.255.255.255'], [num(10, 0, 0, 1), '10.0.0.1']]) {
  if (fmt(ip) !== want) throw Error('formato do IP: ' + want);
}
if ('255.255.255.255'.length + 1 > 16) throw Error('ipText[16] não cabe');
console.log('ip_cache: o texto do IP é refeito só quando o endereço muda; makeState não aloca mais por desenho');
