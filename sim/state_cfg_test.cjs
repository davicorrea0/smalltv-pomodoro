// Extrai e compila o Settings, o FNV-1a e o sendState reais. O servidor, String e ESP são falsos;
// a montagem do JSON e a decisão cfg=0 são exatamente as do firmware.
const fs = require('fs'), assert = require('assert');
const source = require('./fonte.cjs');
const cut = sig => {
  const a = source.indexOf(sig);
  if (a < 0) throw Error('não achei ' + sig);
  const b = source.indexOf('\n}', a);
  if (b < 0) throw Error('sem fim: ' + sig);
  return source.slice(a, b + 2);
};
const sa = source.indexOf('struct Settings {'), sb = source.indexOf('\n};', sa);
assert(sa >= 0 && sb > sa, 'struct Settings');
const settings = source.slice(sa, sb + 3);
const fnv = source.slice(source.indexOf('static void fnvMix('), source.indexOf('static void sendState() {'));
const sendState = cut('static void sendState() {');
assert(fnv.includes('ota + sizeof cfg.otaPass') && fnv.includes('ap + sizeof cfg.apPass'), 'as duas senhas precisam ser puladas pelo hash');
assert(fnv.includes('const uint8_t otaSet = cfg.otaPass[0] != 0;'), 'só a presença da senha OTA entra no hash');
assert(!/fnvMix\(h,\s*(?:cfg\.)?(?:otaPass|apPass)/.test(fnv), 'senha passada diretamente ao FNV');

// O painel usa cfg=0 só no poll; backup, espera de pseq e demais chamadas continuam completas.
const html = fs.readFileSync(require('path').join(__dirname, '../web/index.html'), 'utf8');
const line = text => { const i = html.indexOf(text); assert(i >= 0, text); return html.slice(i, html.indexOf('\n', i)); };
const poll = line('function poll()');
const load = line('function load(s)');
assert(poll.includes("fetch(st&&st.cfg?'/api/state?cfg=0':'/api/state')"), 'poll usa cfg=0 só depois do primeiro estado completo');
assert(load.includes('st&&s.cv!=null&&s.cv===st.cv') && load.includes('s.cfg=st.cfg'), 'load reaproveita cfg quando cv é igual');
assert(load.includes("else return fetch('/api/state').then(r=>r.json()).then(load)"), 'load busca o estado completo quando cv difere ou não existe');
assert(!line('async function backup()').includes('cfg=0'), 'backup continua completo');
assert(line('if(s.pseq!=null)').includes("fetch('/api/state')"), 'refetch de pseq continua completo');

const out = process.argv[2];
if (!out) {
  console.log('state cfg: contrato do painel e exclusão das senhas conferidos');
  process.exit(0);
}

fs.writeFileSync(out, `#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <algorithm>
#define F(s) (s)
#define PSTR(s) (s)
#define snprintf_P snprintf
#define FW_VERSION "1.26.1"
#define HTTP_GET 1
#define THEME_PACK 200
struct String {
  std::string s;
  String() {} String(const char *p) : s(p ? p : "") {} String(const std::string &p) : s(p) {}
  String &operator=(const char *p) { s = p ? p : ""; return *this; }
  String &operator+=(const char *p) { s += p ? p : ""; return *this; }
  String &operator+=(const String &v) { s += v.s; return *this; }
  String &operator+=(char v) { s += v; return *this; }
  String &operator+=(unsigned char v) { s += std::to_string(v); return *this; }
  String &operator+=(short v) { s += std::to_string(v); return *this; }
  String &operator+=(unsigned short v) { s += std::to_string(v); return *this; }
  String &operator+=(int v) { s += std::to_string(v); return *this; }
  String &operator+=(unsigned v) { s += std::to_string(v); return *this; }
  String &operator+=(long v) { s += std::to_string(v); return *this; }
  String &operator+=(unsigned long v) { s += std::to_string(v); return *this; }
  bool operator!=(const char *p) const { return s != p; }
  size_t length() const { return s.size(); }
  const char *c_str() const { return s.c_str(); }
  void reserve(size_t n) { s.reserve(n); }
  String toString() const { return *this; }
};
${settings}
static Settings cfg;
struct TimeGuard { bool confirmed = false; uint16_t undone = 0; };
static TimeGuard timeGuard;
static uint16_t wifiDowngrades = 0;
static String rotList;
struct FSInfo { uint32_t usedBytes = 0, totalBytes = 0; };
static bool fsOk = true; static uint32_t fsInfoAt = 0, g_now = 123456; static std::string output; static bool cfg0 = false, worstValues = false; static int requestMethod = HTTP_GET;
static struct { void info(FSInfo &f) { f.usedBytes = 456789; f.totalBytes = 2097152; } } LittleFS;
static struct Server {
  int method() const { return requestMethod; }
  String arg(const char *) const { return cfg0 ? String("0") : String(); }
  void sendHeader(const char *, const char *) {}
  void sendContent(const String &v) { output += v.s; }
} server;
static uint32_t millis() { return g_now; }
static void yield() {}
static bool readOk() { return true; }
static bool localNow(struct tm &t) { memset(&t, 0, sizeof t); return true; }
static String hhmm(const struct tm &) { return String("12:34"); }
static void jsonEscapeAppend(String &j, const char *p) { for (; p && *p; ++p) { if (*p == '"' || *p == '\\\\') j += '\\\\'; j += *p; } }
static void appendFixed(String &j, float v, int n) { char b[40]; snprintf(b, sizeof b, "%.*f", n, v); j += b; }
static void jsonBegin() { output.clear(); }
static void jsonFlush(String &j) { output += j.s; j.s.clear(); }
static void jsonEnd(String &j) { output += j.s; j.s.clear(); }
enum { IDLE, WORK, SHORT_BREAK, LONG_BREAK }; static int phase = IDLE; static const char *PHASE_KEY[] = {"idle", "work", "short", "long"};
static bool running = false; static uint32_t remainingMs = 1500000, totalMs = 1500000; static uint8_t cycleDone = 1; static uint16_t statsStreak = 3, statsBest = 8;
static String curFile("/image/cafe.jpg");
static struct { bool ok = true; float t = 21.5f, tmin = 17.2f, tmax = 25.8f; int hum = 63; uint8_t rainIn = 0; char tzName[40] = "America/Sao_Paulo"; int32_t utcOff = -10800; int code = 1; } wx;
static struct { uint8_t fails = 0; } wxBack;
static int32_t localOffset() { return -10800; } static int sunRise = 390, sunSet = 1125; static const char *weatherText(int) { return "Parcialmente nublado"; }
static bool fsLayout2() { return true; } struct FsMark { uint32_t magic; uint8_t pend; }; static FsMark fsMark = {0, 0}; static const uint32_t FS_MAGIC = 0x46533241;
static uint32_t alexaSeen = 0; static uint8_t alexaLast = 0, alexaMin = 25; struct FakeIp { String toString() const { return String("192.168.1.20"); } }; static FakeIp alexaIp;
static uint32_t previewUntil = 0; static bool previewActive() { return false; } static char lanName[32] = "escritorio", netName[32] = "smalltv";
static uint16_t pomoDrawMs = 120, phaseSeq = 2, shownSeq = 2;
static void diagHeap(uint32_t &heap, uint32_t &blk, uint8_t &frag) { heap = 30000; blk = 17000; frag = 12; }
static void diagJson(String &j, uint8_t) {
  j += worstValues
    ? ",\\\"diag\\\":{\\\"rst\\\":255,\\\"exc\\\":255,\\\"epc\\\":\\\"ffffffff\\\",\\\"rssi\\\":-100,\\\"minheap\\\":4294967295,\\\"frag\\\":100,\\\"up\\\":4294967,\\\"draw\\\":65535,\\\"stall\\\":4294967295,\\\"stallAt\\\":255,\\\"minblk\\\":4294967295,\\\"stack\\\":4294967295,\\\"ramfix\\\":4294967295,\\\"lps\\\":4294967295,\\\"pack\\\":65535,\\\"tw\\\":4294967295,\\\"twmax\\\":255,\\\"nomem\\\":{\\\"jpg\\\":255,\\\"gif\\\":255,\\\"strip\\\":255},\\\"boot\\\":{\\\"scr\\\":4294967295,\\\"wifi\\\":4294967295,\\\"wf\\\":255,\\\"time\\\":4294967295,\\\"src\\\":255,\\\"ntp\\\":4294967295}}"
    : ",\\\"diag\\\":{\\\"rst\\\":4,\\\"exc\\\":0,\\\"epc\\\":\\\"00000000\\\",\\\"rssi\\\":-61,\\\"minheap\\\":28000,\\\"frag\\\":12,\\\"up\\\":123,\\\"draw\\\":140,\\\"stall\\\":90,\\\"stallAt\\\":0,\\\"minblk\\\":16000,\\\"stack\\\":3200,\\\"ramfix\\\":38496,\\\"lps\\\":1000,\\\"pack\\\":38,\\\"tw\\\":2,\\\"twmax\\\":1,\\\"nomem\\\":{\\\"jpg\\\":0,\\\"gif\\\":0,\\\"strip\\\":0},\\\"boot\\\":{\\\"scr\\\":1200,\\\"wifi\\\":800,\\\"wf\\\":1,\\\"time\\\":900,\\\"src\\\":1,\\\"ntp\\\":700}}";
}
static void crashJson(String &j) { if (worstValues) j += ",\\\"crash\\\":{\\\"n\\\":255,\\\"r\\\":255,\\\"x\\\":255,\\\"epc\\\":\\\"ffffffff\\\",\\\"va\\\":\\\"ffffffff\\\",\\\"up\\\":4294967295,\\\"ver\\\":\\\"1.26.1\\\"}"; }
${fnv}
${sendState}
static std::string response(bool omit, int method = HTTP_GET) { cfg0 = omit; requestMethod = method; sendState(); return output; }
static bool has(const std::string &j, const char *key) { return j.find(std::string("\\\"") + key + "\\\":") != std::string::npos; }
static uint32_t number(const std::string &j, const char *key) { size_t p = j.find(std::string("\\\"") + key + "\\\":"); return p == std::string::npos ? 0 : (uint32_t)strtoul(j.c_str() + p + strlen(key) + 3, nullptr, 10); }
static bool jsonShape(const std::string &j) { int d = 0; bool str = false; for (size_t i = 0; i < j.size(); ++i) { char c = j[i]; if (str) { if (c == '\\\\') ++i; else if (c == '"') str = false; } else if (c == '"') str = true; else if (c == '{' || c == '[') ++d; else if (c == '}' || c == ']') { if (--d < 0) return false; } } return !str && d == 0 && !j.empty() && j.front() == '{' && j.back() == '}'; }
static void text(char *p, size_t n, char c) { memset(p, c, n - 1); p[n - 1] = 0; }
#define CHECK(x) do { if (!(x)) { printf("FALHA linha %d: %s\\n", __LINE__, #x); return 1; } } while (0)
int main() {
  memset(&cfg, 0, sizeof cfg); cfg.magic = 0x504f4d34; cfg.work = 25; cfg.shortBreak = 5; cfg.longBreak = 15; cfg.longEvery = 4; cfg.brightness = 70; cfg.autoStart = 1;
  strcpy(cfg.tz, "<-03>3"); strcpy(cfg.otaPass, "senha-ota-a"); cfg.todayCount = 4; cfg.theme = 1; cfg.albumDelay = 10; cfg.colR = 255; cfg.colG = 90; cfg.colB = 71;
  cfg.lat = -25.390f; cfg.lon = -51.460f; strcpy(cfg.city, "Guarapuava"); cfg.dailyGoal = 8; strcpy(cfg.facePkg, "bento-casa"); strcpy(cfg.apPass, "abc12345"); cfg.vcom = 30; cfg.rainAlert = 1; cfg.lcdGreen = 1;
  rotList = "t:0,t:1,p:bento-casa"; wifiDowngrades = 2; timeGuard.confirmed = true; timeGuard.undone = 1;
  const std::string full = response(false), lean = response(true), post = response(true, 2);
  CHECK(jsonShape(full) && jsonShape(lean)); CHECK(has(full, "cfg") && has(full, "rot")); CHECK(!has(lean, "cfg") && !has(lean, "rot"));
  CHECK(has(full, "cv") && has(lean, "cv") && number(full, "cv") == number(lean, "cv")); CHECK(has(post, "cfg"));
  const uint32_t base = cfgVersion();
  auto changes = [&](auto f) { Settings old = cfg; String oldRot = rotList; uint16_t oldWifi = wifiDowngrades; TimeGuard oldTime = timeGuard; f(); uint32_t v = cfgVersion(); cfg = old; rotList = oldRot; wifiDowngrades = oldWifi; timeGuard = oldTime; return v != base; };
  CHECK(changes([&]{ cfg.theme++; })); CHECK(changes([&]{ strcpy(cfg.facePkg, "outro"); })); CHECK(changes([&]{ cfg.vcom++; })); CHECK(changes([&]{ cfg.brightness++; }));
  CHECK(changes([&]{ rotList += ",t:2"; })); CHECK(changes([&]{ wifiDowngrades++; })); CHECK(changes([&]{ timeGuard.undone++; })); CHECK(changes([&]{ timeGuard.confirmed = false; }));
  Settings saved = cfg; memset(cfg.otaPass + 1, 'X', sizeof cfg.otaPass - 1); CHECK(cfgVersion() == base); cfg = saved;
  memset(cfg.apPass, 'Y', sizeof cfg.apPass); CHECK(cfgVersion() == base); cfg = saved;
  worstValues = true; text(cfg.tz, sizeof cfg.tz, 'T'); text(cfg.city, sizeof cfg.city, 'C'); text(cfg.facePkg, sizeof cfg.facePkg, 'P'); cfg.theme = THEME_PACK; cfg.todayCount = 65535; cfg.lat = -89.9999f; cfg.lon = 179.9999f;
  rotList = std::string(300, 'R'); wifiDowngrades = 65535; timeGuard.undone = 65535; g_now = 4294967295u; remainingMs = totalMs = 4294967295u; statsStreak = statsBest = 65535; fsInfoAt = 0;
  const std::string worstFull = response(false), worstLean = response(true);
  CHECK(jsonShape(worstFull) && jsonShape(worstLean) && has(worstFull, "cfg") && !has(worstLean, "cfg"));
  printf("state cfg: realista completo=%zu bytes, cfg=0=%zu bytes; pior caso completo=%zu bytes, cfg=0=%zu bytes\\n", full.size(), lean.size(), worstFull.size(), worstLean.size());
  return 0;
}
`);
