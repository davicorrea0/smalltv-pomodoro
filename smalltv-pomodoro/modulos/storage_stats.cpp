// Sistema de arquivos (montagem e migração 4M3M→4M2M), histórico de focos e uso de espaço.
#include "../app.h"

static uint8_t statsPending = 0;  // focos terminados antes da hora ser confirmada (gravados no histórico depois)

// ---------- memória de arquivos (LittleFS) ----------
bool fsOk = false;

// O símbolo _FS_start vem do script do linker do layout escolhido (eagle.flash.4m2m.ld: 0x40400000).
bool fsLayout2() { return (uint32_t)&_FS_start == 0x40400000; }

// Monta o LittleFS SEM formatar à toa: se falhar, os arquivos ficam intactos. A exceção é a migração do
// layout 4M3M (LittleFS em 0x100000) para o 4M2M (0x200000): no primeiro início aqui, sem a marca na
// EEPROM, o que há em 0x200000 é um pedaço do sistema de arquivos antigo (ou da memória do firmware
// original), então formata uma vez, recria as pastas e marca `pend` para o painel oferecer a restauração
// do backup. Se a marca existe e a montagem falha, NÃO formata (pode ser outra coisa): o painel mostra o botão Formatar.
void fsMount() {
  LittleFSConfig fcfg;
  fcfg.setAutoFormat(false);
  LittleFS.setConfig(fcfg);
  fsOk = LittleFS.begin();
  if (!fsLayout2()) return;
  EEPROM.get(FS_OFF, fsMark);
  if (fsMark.magic == FS_MAGIC) return;
  message("SmallTV Pomodoro", tr("Reorganizando a memoria", "Reorganizing storage", "Reorganizando la memoria"), "...");
  if (fsOk) LittleFS.end();
  // Antes de formatar, guarda o histórico, o rodízio e os avisos, que valem mais que o espaço que ocupam: no primeiro
  // início o LittleFS antigo (4M3M, 0x100000-0x3FA000) ainda está intacto, porque a atualização só gravou abaixo de
  // 0x100000. Assim quem atualiza sem o backup do painel (ex.: direto da 1.23.1) não perde a sequência de dias.
  struct Keep { const char *path; std::unique_ptr<uint8_t[]> data; size_t len; } keep[] = {
      {"/stats.bin", nullptr, 0}, {"/rotation.txt", nullptr, 0}, {"/hooks.txt", nullptr, 0}};
  {
    FS old(FSImplPtr(new littlefs_impl::LittleFSImpl(0x100000, 0x3FA000 - 0x100000, 256, 8192, 2)));
    LittleFSConfig ocfg;
    ocfg.setAutoFormat(false);
    old.setConfig(ocfg);
    if (old.begin()) {
      for (Keep &k : keep) {
        File f = old.open(k.path, "r");
        if (!f || !f.size() || f.size() > 2048) continue;
        k.data.reset(new (std::nothrow) uint8_t[f.size()]);
        if (k.data) k.len = f.read(k.data.get(), f.size());
      }
      old.end();
    }
  }
  fsOk = LittleFS.format() && LittleFS.begin();
  if (!fsOk) return;  // sem marca: tenta de novo no próximo início
  LittleFS.mkdir("/image");
  LittleFS.mkdir("/gif");
  LittleFS.mkdir("/faces");
  for (Keep &k : keep) {
    if (!k.len) continue;
    File f = LittleFS.open(k.path, "w");
    if (f) f.write(k.data.get(), k.len);
  }
  fsMark = {FS_MAGIC, 1};
  EEPROM.put(FS_OFF, fsMark);
  EEPROM.commit();
}

StatsHead stats = {0, 0, 0, 0};

uint16_t statsStreak = 0, statsBest = 0;

static bool statsWritePending = false;

static uint16_t statsPendingDay = 0;

static uint8_t statsPendingCount = 0;

// dias desde 1970-01-01 para a data local (algoritmo "days from civil")
static uint16_t dayNumber(const struct tm &t) {
  int y = t.tm_year + 1900, m = t.tm_mon + 1;
  y -= m <= 2;
  int era = y / 400, yoe = y - era * 400, doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + t.tm_mday - 1;
  return era * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468;
}

// lê os 371 dias em ordem (o mais antigo primeiro); false sem arquivo
static bool statsRead(uint8_t *days) {
  File f = fsOk ? LittleFS.open("/stats.bin", "r") : File();
  if (!f || f.size() != sizeof(StatsHead) + STATS_DAYS) return false;
  uint8_t ring[STATS_DAYS];
  f.seek(sizeof(StatsHead));
  f.read(ring, STATS_DAYS);
  f.close();
  for (int i = 0; i < STATS_DAYS; i++) days[i] = ring[(stats.lastDay + 1 + i) % STATS_DAYS];
  return true;
}

void statsCompute() {
  uint8_t d[STATS_DAYS];
  statsStreak = 0;
  if (stats.magic == STATS_MAGIC && statsRead(d)) {
    int i = STATS_DAYS - 1;
    if (d[i] < cfg.dailyGoal) i--;  // hoje ainda não bateu: a sequência vem até ontem
    for (; i >= 0 && d[i] >= cfg.dailyGoal; i--) statsStreak++;
  }
  if (statsStreak > stats.best) stats.best = statsStreak;
  statsBest = stats.best;
}

// grava o dia "day" com "count" focos, zerando os dias pulados desde o último registro
static void statsWrite(uint16_t day, uint8_t count) {
  if (!fsOk) return;
  // LittleFS 2.5.1 pode corromper dados com dois arquivos abertos para escrita. Um upload permanece
  // aberto entre chamadas de handleClient(), então conserva só a atualização mais recente do histórico
  // e a grava depois que o upload terminar.
  if (fsWriterBusy()) {
    statsPendingDay = day;
    statsPendingCount = count;
    statsWritePending = true;
    return;
  }
  File f = LittleFS.open("/stats.bin", "r+");
  bool fresh = !f || f.size() != sizeof(StatsHead) + STATS_DAYS;  // sem arquivo (ou memória formatada)
  if (fresh) {
    if (f) f.close();
    f = LittleFS.open("/stats.bin", "w+");
  }
  if (!f) return;
  if (!fresh && stats.magic == STATS_MAGIC && day < stats.lastDay) {  // hora voltou para trás: não apaga nada
    f.close();
    return;
  }
  if (fresh || stats.magic != STATS_MAGIC || day - stats.lastDay >= STATS_DAYS) {
    if (stats.magic != STATS_MAGIC) stats = {STATS_MAGIC, day, 0, 0};
    // cabeçalho e os 371 dias em sequência desde o começo: o LittleFS não deixa pular para além do fim
    // do arquivo (o seek falha). Até a 1.18.2 este trecho pulava o cabeçalho num arquivo vazio, o arquivo
    // ficava com 371 bytes e o boot seguinte descartava o histórico (ver statsLoad).
    uint8_t zero[64] = {0};
    f.seek(0);
    f.write((const uint8_t *)&stats, sizeof stats);
    for (int i = 0; i < STATS_DAYS; i += sizeof zero) f.write(zero, min((int)sizeof zero, STATS_DAYS - i));
  } else
    for (uint16_t d = stats.lastDay + 1; d < day; d++) {  // dias com o relógio desligado: zero
      f.seek(sizeof(StatsHead) + d % STATS_DAYS);
      f.write((uint8_t)0);
    }
  f.seek(sizeof(StatsHead) + day % STATS_DAYS);
  f.write(count);
  stats.lastDay = day;
  f.seek(0);
  f.write((const uint8_t *)&stats, sizeof stats);
  f.close();
}

// histórico (o mais antigo primeiro) para o jardim, as conquistas, o cartão de compartilhar e o backup
void sendStats() {
  if (!readOk()) return;
  uint8_t d[STATS_DAYS];
  bool ok = stats.magic == STATS_MAGIC && statsRead(d);
  String j;
  j.reserve(704);
  jsonBegin();
  j = F("{\"day\":");
  j += stats.lastDay;
  j += F(",\"goal\":");
  j += cfg.dailyGoal;
  j += F(",\"streak\":");
  j += statsStreak;
  j += F(",\"best\":");
  j += statsBest;
  j += F(",\"total\":");
  j += stats.total;
  j += F(",\"days\":[");
  for (int i = 0; i < STATS_DAYS; i++) {
    if (i) j += ',';
    j += ok ? d[i] : 0;
    if (j.length() > 640) jsonFlush(j);
  }
  j += "]}";
  jsonEnd(j);
}

// restaura o histórico de um backup: "days" = contagens do mais antigo ao dia "day". Aceita tudo o que
// /api/stats exporta (STATS_DAYS dias, inclusive). Com mais dias (backup de outra versão), guarda os STATS_DAYS
// mais recentes: o anel não comporta mais, e recusar deixaria o painel sem restaurar nada.
void handleStatsImport() {
  if (!guard()) return;
  if (!fsOk || !server.hasArg("days") || !server.hasArg("day")) {
    server.send_P(400, PSTR("text/plain"), PSTR("Backup invalido"));
    return;
  }
  const String &d = server.arg("days");
  uint16_t day = server.arg("day").toInt();
  std::vector<uint8_t> v;
  v.reserve(min((size_t)STATS_DAYS, d.length() / 2 + 1));
  for (int i = 0; i < (int)d.length();) {
    int e = d.indexOf(',', i);
    if (e < 0) e = d.length();
    int n = d.substring(i, e).toInt();
    if (v.size() >= STATS_DAYS) v.erase(v.begin());
    v.push_back(n < 0 ? 0 : n > 255 ? 255 : n);
    i = e + 1;
  }
  if (v.empty()) {
    server.send_P(400, PSTR("text/plain"), PSTR("Backup invalido"));
    return;
  }
  stats = {STATS_MAGIC, day, (uint16_t)server.arg("best").toInt(), (uint32_t)server.arg("total").toInt()};
  File fh = LittleFS.open("/stats.bin", "w");
  bool saved = fh;  // close() zera o arquivo: o resultado precisa ser lido antes
  if (fh) {
    uint8_t ring[STATS_DAYS] = {0};
    for (size_t i = 0; i < v.size(); i++) ring[(day - (v.size() - 1) + i) % STATS_DAYS] = v[i];
    fh.write((const uint8_t *)&stats, sizeof stats);
    fh.write(ring, STATS_DAYS);
    fh.close();
  }
  statsCompute();
  server.send(saved ? 200 : 500, "text/plain", saved ? "ok" : "falhou");
}

void statsLoad() {
  File f = fsOk ? LittleFS.open("/stats.bin", "r") : File();
  size_t n = f ? f.size() : 0;
  if (n == sizeof(StatsHead) + STATS_DAYS) f.read((uint8_t *)&stats, sizeof stats);
  else if (n >= STATS_DAYS && n < sizeof(StatsHead) + STATS_DAYS) {
    // arquivo curto gravado até a 1.18.2 (ver statsWrite): mesmo cabeçalho e os dias a partir do byte 12,
    // só que faltando o fim. Recupera o que tem e regrava no tamanho certo.
    uint8_t ring[STATS_DAYS] = {0};
    f.read((uint8_t *)&stats, sizeof stats);
    f.read(ring, n - sizeof(StatsHead));
    f.close();
    File w = stats.magic == STATS_MAGIC ? LittleFS.open("/stats.bin", "w") : File();
    if (w) {
      w.write((const uint8_t *)&stats, sizeof stats);
      w.write(ring, STATS_DAYS);
      w.close();
    }
  }
  if (f) f.close();
  if (stats.magic != STATS_MAGIC) stats = {0, 0, 0, 0};
  statsCompute();
}

void statsFocus() {
  struct tm t;
  if (!localNow(t) || !timeTrusted()) {  // sem hora confirmada: guarda e grava quando confirmar (statsFlush)
    if (statsPending < 255) statsPending++;
    return;
  }
  stats.total++;
  statsWrite(dayNumber(t), cfg.todayCount > 255 ? 255 : cfg.todayCount);
  if (!statsWritePending) statsCompute();
}

void statsFlush(const struct tm &t) {
  if (!statsPending) return;
  stats.total += statsPending;
  statsPending = 0;
  statsWrite(dayNumber(t), cfg.todayCount > 255 ? 255 : cfg.todayCount);
  statsCompute();
}

void statsRollover(const struct tm &t) {
  statsWrite(dayNumber(t), cfg.todayCount > 255 ? 255 : cfg.todayCount);
  if (!statsWritePending) statsCompute();
}

uint32_t fsInfoAt = 0;  // quando o espaço livre foi medido para o painel (0 = medir de novo)

// ---------- armazenamento: tudo o que ocupa a memória de arquivos ----------
// O álbum mostra só fotos e GIFs, mas a memória guarda mais: mostradores baixados, o painel, o histórico
// e, em relógios que vieram do firmware original, os arquivos dele. /api/storage lista tudo, em partes
// (sem montar a lista inteira na RAM); /api/fsdelete apaga um arquivo ou uma pasta inteira, menos o que
// o relógio precisa para funcionar (fsKeep).
bool fsKeep(const String &p) {
  if (p == "/stats.bin" || p == "/hooks.txt" || p == "/rotation.txt" || p == "/www" || p.startsWith("/www/")) return true;
  if (p == "/image" || p == "/gif" || p == "/faces") return true;  // as pastas ficam; o que está dentro pode sair
  return cfg.theme == THEME_PACK && p.startsWith(String("/faces/") + cfg.facePkg + ".");  // mostrador em uso
}

void sendStorage() {
  if (!readOk()) return;
  static FSInfo fi;
  if (fsOk) LittleFS.info(fi);
  server.sendHeader("Cache-Control", "no-store");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");
  server.sendContent("{\"used\":" + String(fsOk ? fi.usedBytes : 0) + ",\"total\":" + String(fsOk ? fi.totalBytes : 0) +
                     ",\"block\":" + String(fsOk ? fi.blockSize : 0) + ",\"files\":[");
  std::vector<String> dirs = {"/"};
  bool first = true;
  int n = 0;
  for (size_t k = 0; fsOk && k < dirs.size() && n < 1000; k++) {  // pasta por pasta (em largura)
    Dir d = LittleFS.openDir(dirs[k]);
    String base = dirs[k] == "/" ? "" : dirs[k];
    String chunk;
    while (d.next() && n < 1000) {
      String p = base + "/" + d.fileName();
      bool dir = d.isDirectory();
      if (dir && dirs.size() < 64) dirs.push_back(p);
      chunk += first ? F("{\"p\":\"") : F(",{\"p\":\"");
      first = false;
      jsonEscapeAppend(chunk, p.c_str());
      chunk += F("\",\"s\":");
      chunk += dir ? 0 : d.fileSize();
      if (dir) chunk += F(",\"d\":1");
      if (fsKeep(p)) chunk += F(",\"k\":1");
      chunk += '}';
      n++;
      if (chunk.length() > 700) {
        server.sendContent(chunk);
        chunk = "";
      }
      yield();
    }
    if (chunk.length()) server.sendContent(chunk);
  }
  server.sendContent("]}");
  server.sendContent("");  // fim da resposta em partes
}

// apaga a pasta p e tudo dentro (no máximo 8 níveis); os nomes são lidos antes de apagar
bool fsRemoveAll(const String &p, int depth) {
  if (depth > 8) return false;
  std::vector<String> files, dirs;
  Dir d = LittleFS.openDir(p);
  while (d.next()) (d.isDirectory() ? dirs : files).push_back(p + "/" + d.fileName());
  bool ok = true;
  for (const String &f : files) ok = LittleFS.remove(f) && ok;
  for (const String &s : dirs) ok = fsRemoveAll(s, depth + 1) && ok;
  if (LittleFS.exists(p)) ok = LittleFS.rmdir(p) && ok;  // o LittleFS do núcleo pode já ter tirado a pasta vazia
  return ok;
}

bool fsWriterBusy() {
  return uploadFile || packUpload || panelUpload;
}

void statsFlush() {
  if (!statsWritePending || fsWriterBusy()) return;
  uint16_t day = statsPendingDay;
  uint8_t count = statsPendingCount;
  statsWritePending = false;
  statsWrite(day, count);
  if (!statsWritePending) statsCompute();
}
