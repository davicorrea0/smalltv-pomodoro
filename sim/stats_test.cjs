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
${fn('bool statsRead(')}
${fn('void statsCompute(')}
${fn('void statsWrite(')}
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
  puts("stats: histórico sobrevive a reinícios, anel e formatação ok, arquivo curto antigo recuperado");
}
`);
