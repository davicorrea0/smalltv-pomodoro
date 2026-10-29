// Compila as funções reais do histórico (/stats.bin) de main.cpp com um LittleFS em memória que imita o
// do núcleo ESP8266 (seek além do fim falha, "w"/"w+" truncam, "r"/"r+" exigem o arquivo) e confere que o
// histórico sobrevive a reinícios, que o anel dá a volta e que o arquivo curto das versões antigas é
// recuperado. Uso: node sim/stats_test.cjs saida.cpp [main.cpp]; depois g++ -std=c++17 saida.cpp.
const fs = require('fs'), path = require('path');
const source = fs.readFileSync(process.argv[3] || path.join(__dirname, '../smalltv-pomodoro/main.cpp'), 'utf8');
function fn(sig) { const a = source.indexOf(sig), b = source.indexOf('\n}', a); if (a < 0 || b < 0) throw Error(sig); return source.slice(a, b + 2); }
const out = process.argv[2];
if (!out) throw Error('output.cpp required');
fs.writeFileSync(out, `#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cassert>
#include <map>
#include <string>
#include <vector>
#include <algorithm>
#include <type_traits>
#define F(s) (s)
#define PSTR(s) (s)
using std::min;
static std::map<std::string, std::vector<uint8_t>> disk;
struct File {
  std::vector<uint8_t> *d = nullptr; size_t pos = 0;
  operator bool() const { return d; }
  size_t size() const { return d->size(); }
  bool seek(size_t p) { if (p > d->size()) return false; pos = p; return true; }  // como o LittleFS do núcleo
  size_t write(const uint8_t *b, size_t n) { if (pos + n > d->size()) d->resize(pos + n); memcpy(d->data() + pos, b, n); pos += n; return n; }
  size_t write(uint8_t b) { return write(&b, 1); }
  size_t read(uint8_t *b, size_t n) { n = min(n, d->size() - pos); memcpy(b, d->data() + pos, n); pos += n; return n; }
  void close() { d = nullptr; }
};
struct { File open(const char *n, const char *m) {
  File f; std::string s = m;
  if (s[0] == 'w') disk[n].clear();
  else if (!disk.count(n)) return f;
  f.d = &disk[n]; return f; } } LittleFS;
static bool fsOk = true;
struct { uint8_t dailyGoal = 2; } cfg;
${source.match(/struct StatsHead [^\n]*\n/)[0]}${source.match(/const uint32_t STATS_MAGIC[^\n]*\n/)[0]}${source.match(/const uint16_t STATS_DAYS[^\n]*\n/)[0]}StatsHead stats = {0, 0, 0, 0};
uint16_t statsStreak = 0, statsBest = 0;
static bool writerBusy = false;
bool statsWritePending = false;
uint16_t statsPendingDay = 0;
uint8_t statsPendingCount = 0;
bool fsWriterBusy() { return writerBusy; }
// String e servidor mínimos para sendStats e handleStatsImport (backup do painel)
struct String : std::string {
  using std::string::string;
  String(const std::string &s) : std::string(s) {}
  size_t length() const { return size(); }
  int indexOf(char c, int from = 0) const { size_t i = find(c, from); return i == npos ? -1 : (int)i; }
  String substring(int a, int b) const { return substr(a, b - a); }
  long toInt() const { return atol(c_str()); }
  String &operator+=(char c) { push_back(c); return *this; }
  String &operator+=(const char *c) { append(c); return *this; }
  template <class T, class = typename std::enable_if<std::is_integral<T>::value && !std::is_same<T, char>::value>::type>
  String &operator+=(T v) { append(std::to_string(v)); return *this; }
};
static std::map<std::string, String> args;
static int sentCode = 0; static std::string sentBody;
struct { bool hasArg(const char *n) { return args.count(n); } const String &arg(const char *n) { return args[n]; }
  void send(int c, const char *, const std::string &b) { sentCode = c; sentBody = b; }
  void send_P(int c, const char *, const char *b) { sentCode = c; sentBody = b; } } server;
bool guard() { return true; }
bool readOk() { return true; }
${fn('bool statsRead(')}
${fn('void statsCompute(')}
${fn('void statsWrite(')}
${fn('void statsFlush() {')}
${fn('void sendStats() {')}
${fn('void handleStatsImport() {')}
${fn('void statsLoad(')}
static void boot() { stats = {0, 0, 0, 0}; statsLoad(); }
static int dayCount(uint16_t day) { uint8_t d[STATS_DAYS]; if (!statsRead(d)) return -1; return d[STATS_DAYS - 1 - (stats.lastDay - day)]; }
int main() {
  const uint16_t D = 20749;
  // dia a dia, com um reinício (atualização) entre cada dia
  boot();
  for (int i = 0; i < 10; i++) { statsWrite(D + i, 3); stats.total += 3; statsWrite(D + i, 3); boot(); }
  assert(disk["/stats.bin"].size() == sizeof(StatsHead) + STATS_DAYS);
  assert(stats.magic == STATS_MAGIC && stats.lastDay == D + 9);
  for (int i = 0; i < 10; i++) assert(dayCount(D + i) == 3);
  assert(statsStreak == 10);
  // dias desligado viram zero; o anel dá a volta sem perder o recente
  statsWrite(D + 12, 1); boot();
  assert(dayCount(D + 10) == 0 && dayCount(D + 11) == 0 && dayCount(D + 12) == 1 && dayCount(D + 9) == 3);
  for (int i = 13; i < 400; i++) statsWrite(D + i, i % 5);
  boot();
  assert(dayCount(D + 399) == 399 % 5 && dayCount(D + 300) == 300 % 5);
  // hora no passado (NTP forjado): nada é apagado nem gravado (1.19.1+; antes zerava o histórico)
  statsWrite(D + 100, 9); boot();
  assert(dayCount(D + 399) == 399 % 5 && stats.lastDay == D + 399);
  // memória formatada com o relógio ligado: volta a gravar (antes, o "r+" falhava até reiniciar)
  disk.clear(); statsWrite(D + 400, 2); boot();
  assert(dayCount(D + 400) == 2);
  // arquivo curto das versões antigas (371 bytes): recuperado no boot e regravado com 383
  disk.clear();
  std::vector<uint8_t> old(STATS_DAYS, 0);
  StatsHead h = {STATS_MAGIC, D, 4, 7};
  memcpy(old.data(), &h, sizeof h);
  old[sizeof(StatsHead) + D % STATS_DAYS] = 5;
  disk["/stats.bin"] = old;
  boot();
  assert(disk["/stats.bin"].size() == sizeof(StatsHead) + STATS_DAYS);
  assert(stats.magic == STATS_MAGIC && stats.total == 7 && dayCount(D) == 5);
  // um upload mantém outro arquivo aberto para escrita: o histórico espera e persiste ao fechar
  std::vector<uint8_t> before = disk["/stats.bin"];
  writerBusy = true;
  stats.total++;
  statsWrite(D + 1, 6);
  assert(statsWritePending && statsPendingDay == D + 1 && statsPendingCount == 6);
  assert(disk["/stats.bin"] == before);
  writerBusy = false;
  statsFlush();
  assert(!statsWritePending && disk["/stats.bin"] != before);
  boot();
  assert(stats.total == 8 && dayCount(D + 1) == 6);
  // backup do painel: exporta como /api/stats (371 dias) e restaura pelo handler real
  {
    disk.clear(); boot(); cfg.dailyGoal = 2;
    for (int i = 0; i < 400; i++) { statsWrite(D + i, (i * 7) % 6); stats.total += (i * 7) % 6; }
    uint8_t want[STATS_DAYS]; assert(statsRead(want));
    sendStats(); assert(sentCode == 200);
    std::string js = sentBody;
    size_t a = js.find("\\"days\\":[") + 8, b = js.find(']', a);
    std::string days = js.substr(a, b - a);
    assert(std::count(days.begin(), days.end(), ',') == STATS_DAYS - 1);   // exatamente STATS_DAYS dias
    uint32_t total = stats.total; uint16_t last = stats.lastDay, best = stats.best;
    auto restore = [&](const std::string &dd, uint16_t day) {
      args.clear(); args["days"] = dd; args["day"] = std::to_string(day); args["best"] = std::to_string(best); args["total"] = std::to_string(total);
      disk.clear(); stats = {0, 0, 0, 0}; handleStatsImport(); };
    restore(days, last);
    assert(sentCode == 200 && sentBody == "ok");
    boot();
    assert(stats.lastDay == last && stats.total == total && stats.best >= best);
    uint8_t got[STATS_DAYS]; assert(statsRead(got));
    for (int i = 0; i < STATS_DAYS; i++) assert(got[i] == want[i]);
    // mais dias do que o anel guarda: ficam os STATS_DAYS mais recentes (o mais antigo é descartado)
    restore("9," + days, last); assert(sentCode == 200); boot();
    assert(statsRead(got)); for (int i = 0; i < STATS_DAYS; i++) assert(got[i] == want[i]);
    restore(days + ",9", last + 1); assert(sentCode == 200); boot();
    assert(statsRead(got)); for (int i = 0; i < STATS_DAYS - 1; i++) assert(got[i] == want[i + 1]); assert(got[STATS_DAYS - 1] == 9);
    // poucos dias (backup de versão antiga) e valores fora da faixa
    restore("3,-4,999", last); assert(sentCode == 200); boot();
    assert(statsRead(got) && got[STATS_DAYS - 1] == 255 && got[STATS_DAYS - 2] == 0 && got[STATS_DAYS - 3] == 3);
    // pedido sem os campos ou vazio é recusado e não toca no histórico
    std::vector<uint8_t> keep = disk["/stats.bin"];
    args.clear(); args["day"] = "1"; handleStatsImport(); assert(sentCode == 400 && disk["/stats.bin"] == keep);
    restore("", last); assert(sentCode == 400);
  }
  puts("stats: backup de 371 dias exportado por /api/stats volta inteiro pelo /api/statsimport");
  puts("stats: histórico sobrevive a reinícios, anel e formatação; escrita espera o upload terminar");
}
`);
