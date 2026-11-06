// Servidor web: Wi-Fi, portal de configuração, proteções, envios e rotas /api.
#include "../app.h"
#include <bearssl/bearssl_hash.h>
#include "../web.h"

// respostas curtas repetidas nas rotas: um só corpo no binário em vez de três String temporárias em cada uso
static void sendOk() { server.send_P(200, PSTR("text/plain"), PSTR("ok")); }
static void sendCode(int code) { server.send_P(code, PSTR("text/plain"), PSTR("")); }

ESP8266WebServer server(80);

// Respostas JSON grandes usam HTTP em partes: o ESP8266 não precisa manter o documento inteiro em uma
// String contígua no heap. O pedaço vazio no fim fecha a resposta chunked (mesmo padrão de /api/storage).
void jsonBegin() {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, PSTR("application/json"), "");
}

void jsonFlush(String &chunk) {
  if (!chunk.length()) return;
  server.sendContent(chunk);
  chunk = "";  // conserva a capacidade para o próximo pedaço, sem fragmentar o heap
  yield();
}

void jsonEnd(String &chunk) {
  jsonFlush(chunk);
  server.sendContent("");
}

static uint16_t wifiDowngrades = 0;  // rebaixamentos do Wi-Fi bloqueados desde que ligou (ver wifiGuardLoop)

// Formulários HTML puros (configuração e recuperação) levam um código aleatório da sessão: outra página
// pode enviar o formulário, mas não consegue ler o código.
String formToken;

void newFormToken() {
  char token[33];
  snprintf_P(token, sizeof token, PSTR("%08x%08x%08x%08x"), ESP.random(), ESP.random(), ESP.random(), ESP.random());
  formToken = token;
}

String formTokenField() { return String(F("<input type=hidden name=csrf value=\"")) + formToken + "\">"; }

extern "C" char _heap_start[];  // fim do BSS (linker): daqui até o fim da DRAM (0x3FFFC000) é o heap

uint8_t diagWhere = 0;  // o que o laço está fazendo de demorado: 1 clima, 2 aviso (diag.stallAt); 0 = outro

static uint32_t diagStall = 0;  // maior intervalo entre dois laços desde que ligou (ms)

static uint8_t diagStallAt = 0;

// ---------- web ----------
void jsonEscapeAppend(String &o, const char *s) {
  for (; *s; s++) {
    char c = *s;
    if (c == '"' || c == '\\') o += '\\';
    if ((uint8_t)c >= 0x20) o += c;
  }
}

static bool safeText(const String &s) { return s.indexOf('"') < 0 && s.indexOf('\\') < 0; }

// ---------- diagnóstico (painel: Ajustes -> Sistema) ----------
// Motivo do último reinício (e, se foi travamento, a causa e o endereço), sinal do Wi-Fi, menor memória livre
// desde que ligou e tempo ligado: se o relógio reiniciar sozinho, dá para ver o porquê em vez de adivinhar.
static uint32_t diagMinHeap = 0xFFFFFFFF, diagLastMs = 0;

static uint16_t diagWraps = 0;  // millis() volta a zero a cada ~49,7 dias

// Voltas do laço por segundo (diag.lps): só um número para o "Copiar diagnóstico", sem % nem gráfico. O loop() não tem
// ocioso, então isto mede o quanto ele desenha e atende, não "carga de CPU".
static uint32_t diagLoops = 0, diagLps = 0, diagLpsAt = 0;

void diagLoop() {
  uint32_t now = millis(), h = ESP.getFreeHeap();
  diagLoops++;
  if (now - diagLpsAt >= 1000) {
    diagLps = diagLoops * 1000UL / (now - diagLpsAt);
    diagLoops = 0;
    diagLpsAt = now;
  }
  if (now < diagLastMs) diagWraps++;
  else if (diagLastMs && now - diagLastMs > diagStall) {
    diagStall = now - diagLastMs;
    diagStallAt = diagWhere;
  }
  diagWhere = 0;
  diagLastMs = now;
  if (h < diagMinHeap) diagMinHeap = h;
}

// Menor "maior bloco livre" visto desde que ligou. ESP.getHeapStats()/getMaxFreeBlockSize() percorrem o heap com as
// interrupções desligadas (atrapalham o Wi-Fi), então só rodam no /api/state (uma passada por pedido) e em amostras
// pontuais antes de gastar blocos grandes (foto, GIF, pacote, OTA). NUNCA no diagLoop nem a cada volta do laço.
static uint32_t diagMinBlk = 0xFFFFFFFF;

void diagBlkSample() {
  uint32_t m = ESP.getMaxFreeBlockSize();
  if (m < diagMinBlk) diagMinBlk = m;
}

// Faltas de memória que viraram aviso na tela: foto, GIF (a faixa do uiOffscreen é contada no ui.cpp). Satura em 255.
static uint8_t diagNoMemN[2];

void diagNoMem(uint8_t what) {
  if (what < 2 && diagNoMemN[what] < 255) diagNoMemN[what]++;
}

// Uma única varredura do heap por pedido: alimenta heap, blk, frag e o minblk.
static void diagHeap(uint32_t &heapFree, uint32_t &blk, uint8_t &frag) {
  ESP.getHeapStats(&heapFree, &blk, &frag);
  if (blk < diagMinBlk) diagMinBlk = blk;
}

// RAM fixa (DATA + RODATA + BSS): constante do build, do início da DRAM (0x3FFE8000) até o início do heap. O painel
// calcula o resto da DRAM de 80 KB (81920 B): heap total = 81920 - ramfix. A pilha do laço fica fora (pilha do sistema).
static uint32_t ramFixed() { return (uint32_t)(uintptr_t)_heap_start - 0x3FFE8000u; }

static void diagJson(String &j, uint8_t frag) {
  const rst_info *r = ESP.getResetInfoPtr();
  char epc[12];
  snprintf(epc, sizeof epc, "%08x", r ? (unsigned)r->epc1 : 0u);
  j += F(",\"diag\":{\"rst\":");  // 0 ligado, 1 watchdog de hardware, 2 travamento, 3 watchdog, 4 reinício pedido, 6 botão/externo
  j += r ? (int)r->reason : -1;
  j += F(",\"exc\":");
  j += r ? (int)r->exccause : -1;
  j += F(",\"epc\":\"");
  j += epc;
  j += F("\",\"rssi\":");
  j += WiFi.isConnected() ? WiFi.RSSI() : 0;
  j += F(",\"minheap\":");
  j += diagMinHeap;
  j += F(",\"frag\":");
  j += frag;
  j += F(",\"up\":");
  j += (uint32_t)((((uint64_t)diagWraps << 32) | millis()) / 1000);
  j += F(",\"draw\":");  // ms do último desenho que trocou a tela inteira (0 = ainda nenhum)
  j += fullDrawMs;
  j += F(",\"stall\":");  // maior intervalo entre dois laços (ms) e onde foi: 1 clima, 2 aviso, 0 outro
  j += diagStall;
  j += F(",\"stallAt\":");
  j += diagStallAt;
  j += F(",\"minblk\":");  // menor maior bloco livre desde que ligou (bytes)
  j += diagMinBlk;
  j += F(",\"stack\":");  // menor folga da pilha do laço desde que ligou (bytes)
  j += ESP.getFreeContStack();
  j += F(",\"ramfix\":");  // RAM fixa do build em bytes (o painel desenha a barra fixa / em uso / livre)
  j += ramFixed();
  j += F(",\"lps\":");  // voltas do laço por segundo (só no texto copiado do painel)
  j += diagLps;
  j += F(",\"pack\":");  // ms da última carga de pacote de mostrador (0 = nenhuma)
  j += packLoadMs;
  j += F(",\"nomem\":{\"jpg\":");  // faltas de memória: foto, GIF e faixa de desenho recusada
  j += diagNoMemN[0];
  j += F(",\"gif\":");
  j += diagNoMemN[1];
  j += F(",\"strip\":");
  j += uiOffscreenFail;
  j += F("},\"boot\":{\"scr\":");  // ms desde que ligou até cada etapa do último início (0 = ainda não)
  j += bootScreenMs;
  j += F(",\"wifi\":");
  j += bootWifiMs;
  j += F(",\"wf\":");
  j += bootWifiPath;
  j += F(",\"time\":");
  j += bootTimeMs;
  j += F(",\"src\":");
  j += bootTimeSrc;
  j += F(",\"ntp\":");
  j += bootNtpMs;
  j += F("}}");
}

// "crash" só existe depois de uma queda: n = quantas, r = motivo (como diag.rst), x/epc/va = causa e endereços
static void crashJson(String &j) {
  if (!crashRec.magic) return;
  char b[112];
  snprintf_P(b, sizeof b, PSTR(",\"crash\":{\"n\":%u,\"r\":%u,\"x\":%u,\"epc\":\"%08x\",\"va\":\"%08x\",\"up\":%u,\"ver\":\"%.7s\"}"),
           crashRec.n, crashRec.reason, crashRec.exc, (unsigned)crashRec.epc, (unsigned)crashRec.vaddr, (unsigned)crashRec.up, crashRec.ver);
  j += b;
}

static void sendState() {
  if (!readOk()) return;
  struct tm t;
  String clock = localNow(t) ? hhmm(t) : "--:--";
  char color[8];
  snprintf_P(color, sizeof(color), PSTR("#%02x%02x%02x"), cfg.colR, cfg.colG, cfg.colB);
  // LittleFS.info() percorre o sistema de arquivos inteiro (~100 ms com 1,5 MB de fotos) e o painel
  // pede o estado a cada segundo: isso travava os GIFs. Guarda o resultado; recalcula quando os
  // arquivos mudam (fsInfoAt = 0) ou depois de 1 minuto.
  static FSInfo fi = {};
  if (fsOk && (!fsInfoAt || millis() - fsInfoAt > 60000)) {
    LittleFS.info(fi);
    fsInfoAt = millis() | 1;
  }
  String j;
  j.reserve(704);
  server.sendHeader("Cache-Control", F("no-store"));  // cidade, rede e ajustes: o navegador não guarda
  jsonBegin();
  j += F("{\"phase\":\"");
  j += PHASE_KEY[phase];
  j += F("\",\"running\":");
  j += running ? "true" : "false";
  j += F(",\"remaining\":");
  j += remainingMs;
  j += F(",\"total\":");
  j += totalMs;
  j += F(",\"cycle\":");
  j += cycleDone;
  j += F(",\"today\":");
  j += cfg.todayCount;
  j += F(",\"streak\":");
  j += statsStreak;
  j += F(",\"best\":");
  j += statsBest;
  j += F(",\"clock\":\"");
  j += clock;
  j += F("\",\"showing\":\"");
  jsonEscapeAppend(j, curFile.c_str());
  j += F("\",\"wx\":{\"ok\":");
  j += wx.ok ? 1 : 0;
  j += F(",\"t\":");
  appendFixed(j, wx.t, 1);
  j += F(",\"min\":");
  appendFixed(j, wx.tmin, 1);
  j += F(",\"max\":");
  appendFixed(j, wx.tmax, 1);
  j += F(",\"h\":");
  j += wx.hum;
  j += F(",\"rin\":");  // minutos até a chuva (0 = não vai chover na próxima hora)
  j += wx.rainIn;
  j += F(",\"tzn\":\"");  // fuso da cidade (Open-Meteo), o deslocamento dele e o do relógio agora
  j += wx.tzName;
  j += F("\",\"off\":");
  j += wx.utcOff;
  j += F(",\"loff\":");
  j += localOffset();
  j += F(",\"rise\":");
  j += sunRise;
  j += F(",\"set\":");
  j += sunSet;
  j += F(",\"desc\":\"");
  j += weatherText(wx.code);
  j += F("\"},\"fs\":{\"ok\":");
  j += fsOk ? 1 : 0;
  j += F(",\"l2\":");  // 1 = layout 4M2M (o painel só oferece a migração a quem ainda está no 4M3M)
  j += fsLayout2() ? 1 : 0;
  j += F(",\"mig\":");  // 1 = a memória foi reorganizada e o backup ainda não foi restaurado
  j += fsMark.magic == FS_MAGIC && fsMark.pend ? 1 : 0;
  j += F(",\"used\":");
  j += (uint32_t)fi.usedBytes;
  j += F(",\"total\":");
  j += (uint32_t)fi.totalBytes;
  j += '}';
  jsonFlush(j);
  j = F(",\"cfg\":{\"work\":");
  j += cfg.work;
  j += F(",\"short\":");
  j += cfg.shortBreak;
  j += F(",\"long\":");
  j += cfg.longBreak;
  j += F(",\"every\":");
  j += cfg.longEvery;
  j += F(",\"bri\":");
  j += cfg.brightness;
  j += F(",\"auto\":");
  j += cfg.autoStart;
  j += F(",\"h12\":");
  j += cfg.hour12;
  j += F(",\"tz\":\"");
  jsonEscapeAppend(j, cfg.tz);
  j += F("\",\"lock\":");
  j += cfg.otaPass[0] ? 1 : 0;
  j += F(",\"theme\":");
  j += cfg.theme;
  j += F(",\"mode\":");
  j += cfg.mode;
  j += F(",\"delay\":");
  j += cfg.albumDelay;
  j += F(",\"color\":\"");
  j += color;
  j += F("\",\"non\":");
  j += cfg.nightOn;
  j += F(",\"nstart\":");
  j += cfg.nightStart;
  j += F(",\"nend\":");
  j += cfg.nightEnd;
  j += F(",\"nbri\":");
  j += cfg.nightBri;
  j += F(",\"won\":");
  j += cfg.weatherOn;
  j += F(",\"fahr\":");
  j += cfg.fahr;
  j += F(",\"rain\":");
  j += cfg.rainAlert;
  j += F(",\"beta\":");
  j += cfg.beta;
  j += F(",\"lat\":");
  appendFixed(j, cfg.lat, 4);
  j += F(",\"lon\":");
  appendFixed(j, cfg.lon, 4);
  j += F(",\"city\":\"");
  jsonEscapeAppend(j, cfg.city);
  j += F("\",\"bgr\":");
  j += cfg.bgr;
  j += F(",\"inv\":");
  j += cfg.invert;
  j += F(",\"flip\":");
  j += cfg.flip;
  j += F(",\"vcom\":");
  j += cfg.vcom;
  j += F(",\"wdown\":");  // rebaixamentos do Wi-Fi bloqueados desde que ligou
  j += wifiDowngrades;
  j += F(",\"tok\":");  // hora confirmada por uma segunda fonte (1) e saltos de hora desfeitos desde que ligou
  j += timeGuard.confirmed ? 1 : 0;
  j += F(",\"tund\":");
  j += timeGuard.undone;
  j += F(",\"pkg\":\"");
  j += cfg.theme == THEME_PACK ? cfg.facePkg : "";
  j += F("\",\"goal\":");
  j += cfg.dailyGoal;
  j += F(",\"act\":");
  j += cfg.activity;
  j += F(",\"lang\":");
  j += cfg.lang;
  j += F(",\"nsun\":");
  j += cfg.nightSun;
  j += F(",\"warm\":");
  j += cfg.warm;
  j += F(",\"lcdg\":");
  j += cfg.lcdGreen;
  j += F(",\"alexa\":");
  j += cfg.alexa;
  j += F(",\"rotmin\":");
  j += cfg.rotMin;
  j += F(",\"rot\":\"");
  jsonFlush(j);
  if (rotList.length()) {
    server.sendContent(rotList);
    yield();
  }
  j = F("\"},\"alexa\":{\"ago\":");  // segundos desde o último pedido de um Echo (-1 = nenhum)
  j += alexaSeen ? (long)((millis() - alexaSeen) / 1000) : -1L;
  j += F(",\"ip\":\"");
  j += alexaSeen ? alexaIp.toString() : String();
  j += F("\",\"last\":");
  j += alexaLast;
  j += F(",\"min\":");
  j += alexaMin;
  j += F("},\"pv\":");
  j += previewActive() ? (previewUntil - millis()) / 1000 + 1 : 0;
  j += F(",\"name\":\"");
  j += lanName[0] ? lanName : netName;
  uint32_t heapFree, blk;
  uint8_t frag;
  diagHeap(heapFree, blk, frag);
  j += F("\",\"heap\":");
  j += heapFree;
  j += F(",\"blk\":");
  j += blk;
  j += F(",\"pdraw\":");  // ms que a tela levou para desenhar o Pomodoro (0 = ainda não mediu)
  j += pomoDrawMs;
  j += F(",\"pseq\":");  // fase atual e a última que a tela já mostrou com os números (iguais = na tela)
  j += phaseSeq;
  j += F(",\"pshown\":");
  j += shownSeq;
  diagJson(j, frag);
  crashJson(j);
  j += F(",\"ver\":\"" FW_VERSION "\"}");
  jsonEnd(j);
}

// "-23.5478" -> -23.5478. Lê só o que o painel manda (latitude/longitude), sem a leitura de decimais da newlib.
static float parseDec(const String &s) {
  const char *p = s.c_str();
  bool neg = *p == '-';
  if (*p == '-' || *p == '+') p++;
  float v = 0, k = 1;
  for (; *p >= '0' && *p <= '9'; p++) v = v * 10 + (*p - '0');
  if (*p == '.')
    for (p++; *p >= '0' && *p <= '9'; p++) v += (*p - '0') * (k /= 10);
  return neg ? -v : v;
}

static uint8_t argRange(const char *name, uint8_t cur, int lo, int hi) {
  if (!server.hasArg(name) || server.arg(name).length() == 0) return cur;
  return constrain(server.arg(name).toInt(), lo, hi);
}

// Caminho de mídia válido: só dentro de /image ou /gif, sem "..".
static bool mediaPath(const String &p) {
  return (p.startsWith(F("/image/")) || p.startsWith("/gif/")) && p.indexOf("..") < 0;
}

File uploadFile;

static String uploadPath, uploadError;

static UploadGate photoGate;

static void handleUpload() {
  HTTPUpload &up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    if (uploadFile) uploadFile.close();
    photoGate.begin(false, 0);
    uploadError = "";
    uploadPath = "";
    if (!panelOk() || !uploadAuthed()) {
      uploadError = F("Proibido");
      return;
    }
    if (!fsOk) {
      uploadError = F("Sistema de arquivos indisponivel");
      return;
    }
    // nome seguro: letras, números, - _ . e no máximo 28 caracteres (limite do LittleFS é 31)
    String name;
    for (char c : up.filename)
      if (isalnum(c) || c == '-' || c == '_' || c == '.') name += c;
    while (name.indexOf("..") >= 0) name.replace("..", ".");  // mediaPath recusa "..": o arquivo não poderia ser apagado
    if (name.length() > 28) name = name.substring(name.length() - 28);
    if (!isJpg(name) && !isGif(name)) {
      uploadError = F("Use JPG ou GIF");
      return;
    }
    FSInfo fi;
    LittleFS.info(fi);
    if (fi.totalBytes - fi.usedBytes < 16384) {
      uploadError = F("Memoria cheia");
      return;
    }
    if (!LittleFS.exists("/image")) LittleFS.mkdir("/image");
    uploadPath = String(F("/image/")) + name;
    uploadFile = LittleFS.open(uploadPath, "w");
    if (!uploadFile) uploadError = F("Falha ao criar arquivo");
    else photoGate.begin(true, 3 * 1024 * 1024);
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (!photoGate.active) return;
    if (!photoGate.take(up.currentSize)) uploadError = F("Arquivo grande demais");
    else if (uploadFile.write(up.buf, up.currentSize) != up.currentSize) uploadError = F("Memoria cheia");
    if (uploadError.length()) {
      photoGate.stop();
      uploadFile.close();
      LittleFS.remove(uploadPath);
    }
  } else if (up.status == UPLOAD_FILE_END || up.status == UPLOAD_FILE_ABORTED) {
    photoGate.stop();
    if (uploadFile) uploadFile.close();
    if (up.status == UPLOAD_FILE_ABORTED && uploadPath.length()) LittleFS.remove(uploadPath);
    if (up.status == UPLOAD_FILE_END && !uploadError.length() && isGif(uploadPath) && !validGif(uploadPath)) {
      LittleFS.remove(uploadPath);
      uploadError = tr("GIF fora do padrao", "Unsupported GIF", "GIF fuera de norma");
    } else if (up.status == UPLOAD_FILE_END && !uploadError.length() && isJpg(uploadPath)) {
      JpgResult r = validJpg(uploadPath);
      if (r == JPG_NOMEM) {  // falta de RAM não prova nada sobre o arquivo: a foto fica e o álbum a confere ao desenhar
        uploadError = tr("Foto salva, mas sem memoria para conferir agora", "Photo saved, but no memory to check it now", "Foto guardada, pero sin memoria para comprobarla ahora");
      } else if (r != JPG_OK) {
        LittleFS.remove(uploadPath);
        uploadError = tr("JPG fora do padrao", "Unsupported JPG", "JPG fuera de norma");
      }
    }
    rebuildAlbum();
  }
}

static void sendFiles() {
  if (!readOk()) return;
  String j;
  j.reserve(704);
  jsonBegin();
  j = F("{\"files\":[");
  bool first = true;
  for (const String &n : album) {
    File f = LittleFS.open(n, "r");
    if (!first) j += ',';
    first = false;
    j += F("{\"n\":\"");
    jsonEscapeAppend(j, n.c_str());
    j += F("\",\"s\":");
    j += (uint32_t)(f ? f.size() : 0);
    j += '}';
    if (f) f.close();
    if (j.length() > 640) jsonFlush(j);
  }
  j += "]}";
  jsonEnd(j);
}

// ---------- proteção do painel ----------
// Qualquer site aberto no navegador pode mandar pedidos para o IP do relógio (CSRF: apagar o Wi-Fi,
// formatar, gravar um firmware) ou, apontando um nome próprio para esse IP, até ler as respostas (DNS
// rebinding). Por isso:
//  - toda ação exige o cabeçalho X-SmallTV, que só o painel manda: um site de fora não consegue (o
//    navegador pediria uma permissão CORS que o relógio nunca dá);
//  - o pedido precisa chegar pelo IP do relógio (cabeçalho Host só com números), não por um nome.
// Ficam de fora a API da Alexa (o Echo não manda o cabeçalho; só liga e desliga o Pomodoro), o portal de
// Wi-Fi e o modo de recuperação (exigem estar perto do relógio).
bool hostOk() {
  const String &h = server.hostHeader();
  // IPv4 de verdade (4 partes até 255, porta opcional) ou o nome exato deste relógio em .local
  return safeIpv4Host(h.c_str(), h.length()) || safeOwnHost(h.c_str(), h.length(), netName) ||
         (lanName[0] && safeOwnHost(h.c_str(), h.length(), lanName));
}

bool panelOk() { return hostOk() && server.header("X-SmallTV") == "1"; }

bool deny() {
  server.send_P(403, PSTR("text/plain"), PSTR("Proibido"));
  return false;
}

// Senha do painel (a mesma do /update, usuário admin): com ela definida, todo o painel e a API pedem
// login HTTP (o navegador pergunta uma vez e lembra). Erros usam espera progressiva por IP sem bloquear
// o loop do relógio. A API da Alexa e o modo de recuperação não pedem senha.
// O login Basic é conferido aqui: o authenticate() do servidor traz junto o modo Digest e o MD5 (~2,5 KB).
static bool passOk() {
  return !cfg.otaPass[0] || server.header("Authorization") == "Basic " + base64::encode(String("admin:") + cfg.otaPass, false);
}

void askLogin() {
  server.sendHeader(F("WWW-Authenticate"), F("Basic realm=\"SmallTV\""));
  sendCode(401);
}

static AuthThrottle authThrottle;

static uint32_t requestIp() {
  IPAddress ip = server.client().remoteIP();
  return ((uint32_t)ip[0] << 24) | ((uint32_t)ip[1] << 16) | ((uint32_t)ip[2] << 8) | ip[3];
}

static bool authBlocked(uint32_t ip, bool reply) {
  uint32_t wait = 0;
  if (!cfg.otaPass[0] || !authThrottle.blocked(ip, millis(), &wait)) return false;
  if (reply) {
    server.sendHeader("Retry-After", String((wait + 999) / 1000));
    server.send_P(429, PSTR("text/plain"), PSTR("Tente novamente em alguns segundos"));
  }
  return true;
}

static bool authCheck(bool reply) {
  uint32_t ip = requestIp();
  if (authBlocked(ip, reply)) return false;
  if (passOk()) { authThrottle.success(ip); return true; }
  if (server.hasHeader("Authorization")) authThrottle.fail(ip, millis());
  if (reply) askLogin();
  return false;
}

bool authed() { return authCheck(true); }

bool uploadAuthed() { return authCheck(false); }

bool guard() { return (panelOk() || deny()) && authed(); }   // ações (POST)

bool readOk() { return (hostOk() || deny()) && authed(); }   // leituras

// ---------- painel na memória de arquivos ----------
// O painel (~19 KB comprimido) não ocupa a flash do programa: fica em /www/<hash>.gz, onde <hash> são os
// 24 primeiros hex do SHA-256 do arquivo, calculado aqui ao receber. O firmware só serve o arquivo com o
// PANEL_HASH desta versão (web.h); sem ele, manda o carregador, que baixa o painel do site (com SRI) e
// envia para /api/panel. Um arquivo qualquer enviado ali só ganha outro nome e nunca é servido; aceitar
// outros hashes deixa o painel instalar o da próxima versão antes de gravar o firmware novo.
File panelUpload;

static UploadGate panelGate;

static br_sha256_context panelSha;

static String panelResult;

static void handlePanelUpload() {
  HTTPUpload &up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    if (panelUpload) panelUpload.close();
    panelGate.begin(false, 0);
    panelResult = "";
    if (!panelOk() || !uploadAuthed() || !fsOk) return;
    if (!LittleFS.exists("/www")) LittleFS.mkdir("/www");
    panelUpload = LittleFS.open(F("/www/_novo"), "w");
    if (!panelUpload) return;
    br_sha256_init(&panelSha);
    panelGate.begin(true, 65536);
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (!panelGate.active) return;
    if (!panelGate.take(up.currentSize) || panelUpload.write(up.buf, up.currentSize) != up.currentSize) {
      panelGate.stop();
      panelUpload.close();
      LittleFS.remove(F("/www/_novo"));
      return;
    }
    br_sha256_update(&panelSha, up.buf, up.currentSize);
  } else if (up.status == UPLOAD_FILE_END || up.status == UPLOAD_FILE_ABORTED) {
    bool ok = panelGate.active && up.status == UPLOAD_FILE_END && panelGate.bytes > 18;
    panelGate.stop();
    if (panelUpload) panelUpload.close();
    if (!ok) {
      LittleFS.remove(F("/www/_novo"));
      return;
    }
    uint8_t d[32];
    br_sha256_out(&panelSha, d);
    char hex[25];
    for (int i = 0; i < 12; i++) snprintf(hex + i * 2, 3, "%02x", d[i]);
    String name = String("/www/") + hex + ".gz";
    LittleFS.remove(name);
    LittleFS.rename(F("/www/_novo"), name);
    // guarda só o desta versão e o recém-chegado (o da próxima, durante uma atualização)
    Dir dir = LittleFS.openDir("/www");
    std::vector<String> old;
    while (dir.next())
      if (dir.fileName() != String(hex) + ".gz" && !dir.fileName().equals(F(PANEL_HASH ".gz"))) old.push_back(dir.fileName());
    for (const String &n : old) LittleFS.remove("/www/" + n);
    panelResult = hex;
  }
}

// ETag = PANEL_HASH: o navegador revalida sempre (no-cache) e, com o painel desta versão em cache, recebe 304 sem
// corpo em vez dos ~30 KB. Depois de um OTA o hash é outro, então o If-None-Match antigo não bate e vem o painel
// novo. O 304 só sai depois do login (readOk) e só com /www/<hash>.gz presente: o carregador não leva ETag.
static void sendPanel() {
  if (!readOk()) return;  // com senha, o navegador pede o login já ao abrir o painel
  server.sendHeader("Cache-Control", "no-cache");  // depois de atualizar o firmware, o navegador busca o painel novo
  File f = fsOk ? LittleFS.open("/www/" PANEL_HASH ".gz", "r") : File();
  if (f) {
    server.sendHeader(F("ETag"), F("\"" PANEL_HASH "\""));
    if (!strcmp_P(server.header(F("If-None-Match")).c_str(), PSTR("\"" PANEL_HASH "\""))) server.send(304);
    else server.streamFile(f, "text/html");  // .gz: o servidor manda "Content-Encoding: gzip"
    f.close();
    return;
  }
  server.sendHeader("Content-Encoding", "gzip");
  server.send_P(200, "text/html", (PGM_P)LOADER_HTML_GZ, sizeof(LOADER_HTML_GZ));
}

void setupWeb() {
  server.collectHeaders("X-SmallTV", "Content-Length", "Authorization", "Origin", "X-Firmware-Sig");
  // painel: o desta versão na memória de arquivos ou, sem ele, o carregador que o instala
  server.on("/", HTTP_GET, sendPanel);
  server.on(F("/api/panel"), HTTP_POST, [] {
    if (!guard()) return;
    if (!panelResult.length()) server.send_P(400, PSTR("text/plain"), PSTR("Painel invalido"));
    else server.send(200, "text/plain", panelResult.equals(F(PANEL_HASH)) ? "ok" : String(F("guardado ")) + panelResult);
  }, handlePanelUpload);
  server.on(F("/api/state"), HTTP_GET, sendState);
  server.on(F("/api/stats"), HTTP_GET, sendStats);
  server.on(F("/api/crash"), HTTP_POST, [] {  // "Limpar" do painel
    if (!guard()) return;
    crashRec.magic = 0;
    EEPROM.put(CRASH_OFF, crashRec);
    EEPROM.commit();
    sendOk();
  });
  // avisos de fim de fase: GET lê, POST (w, b) grava, POST test=w|b chama o endereço e devolve o código
  server.on(F("/api/hooks"), HTTP_GET, [] {
    if (!guard()) return;  // os links levam o código da conta do serviço
    server.sendHeader("Cache-Control", F("no-store"));
    String j;
    j.reserve((hookWork.length() + hookBreak.length()) * 2 + 20);
    j = F("{\"w\":\"");
    jsonEscapeAppend(j, hookWork.c_str());
    j += F("\",\"b\":\"");
    jsonEscapeAppend(j, hookBreak.c_str());
    j += F("\"}");
    server.send(200, PSTR("application/json"), j);
  });
  server.on(F("/api/hooks"), HTTP_POST, [] {
    if (!guard()) return;
    if (server.hasArg("test")) {
      diagWhere = 2;
      int code = hookFire(server.arg("test") == "b" ? hookBreak : hookWork);
      for (uint32_t t0 = millis(); code == HTTP_WAIT && millis() - t0 < DnsCache::WAIT + 500; delay(5))  // teste manual: espera o nome
        code = hookFire(server.arg("test") == "b" ? hookBreak : hookWork);
      server.send(200, "text/plain", String(code));
      return;
    }
    String work = hookClean(server.arg("w")), pause = hookClean(server.arg("b"));
    String rawWork = server.arg("w"), rawPause = server.arg("b");
    rawWork.trim(); rawPause.trim();
    if ((rawWork.length() && !work.length()) || (rawPause.length() && !pause.length())) {
      server.send_P(400, PSTR("text/plain"), PSTR("Use um endereco http:// valido (sem espacos ou controles)"));
      return;
    }
    hookWork = work;
    hookBreak = pause;
    File f = fsOk ? LittleFS.open("/hooks.txt", "w") : File();
    bool saved = f;  // close() zera o arquivo: o resultado precisa ser lido antes
    if (f) {
      f.print(hookWork + "\n" + hookBreak + "\n");
      f.close();
    }
    server.send_P(saved ? 200 : 500, PSTR("text/plain"), saved ? PSTR("ok") : PSTR("falhou"));
  });
  server.on(F("/api/statsimport"), HTTP_POST, handleStatsImport);
  // lista dos mostradores para a galeria do painel (vem de faces_list.h)
  server.on(F("/api/faces"), HTTP_GET, [] {
    if (!readOk()) return;
    String j;
    j.reserve(704);
    jsonBegin();
    j = '[';
    for (uint8_t t = 0; t < THEME_COUNT; t++) {
      if (t) j += ',';
      j += F("{\"n\":\"");
      char nb[40], db[72];  // nome e descrição estão na flash
      strncpy_P(nb, uiFaceName(t), sizeof nb - 1), nb[sizeof nb - 1] = 0;
      strncpy_P(db, uiFaceDesc(t), sizeof db - 1), db[sizeof db - 1] = 0;
      jsonEscapeAppend(j, nb);
      j += F("\",\"d\":\"");
      jsonEscapeAppend(j, db);
      j += F("\"}");
      if (j.length() > 640) jsonFlush(j);
    }
    // pacotes instalados: nome/descrição/autor vêm do <id>.txt gravado na instalação
    if (fsOk) {
      Dir d = LittleFS.openDir("/faces");
      while (d.next()) {
        String fn = d.fileName();
        if (!fn.endsWith(".json") || fn.startsWith("_")) continue;
        String id = fn.substring(0, fn.length() - 5), name = id, desc, author;
        File t = LittleFS.open("/faces/" + id + ".txt", "r");
        if (t) {
          name = t.readStringUntil('\n');
          desc = t.readStringUntil('\n');
          author = t.readStringUntil('\n');
          t.close();
        }
        j += F(",{\"n\":\"");
        jsonEscapeAppend(j, name.c_str());
        j += F("\",\"d\":\"");
        jsonEscapeAppend(j, desc.c_str());
        j += F("\",\"a\":\"");
        jsonEscapeAppend(j, author.c_str());
        j += F("\",\"pkg\":\"");
        j += id;
        j += F("\"}");
        if (j.length() > 640) jsonFlush(j);
      }
    }
    j += "]";
    jsonEnd(j);
  });
  // prévia PNG de um mostrador para a galeria do painel. As dos embutidos ficam no site (geradas pelo
  // simulador, sem ocupar a flash do relógio); as dos pacotes instalados, no LittleFS.
  server.on("/face", HTTP_GET, [] {
    if (!readOk()) return;
    if (server.hasArg("pkg")) {  // prévia de pacote instalado (/faces/<id>.png, se veio junto)
      const String &id = server.arg("pkg");
      File f = fsOk && idOk(id) ? LittleFS.open("/faces/" + id + ".png", "r") : File();
      if (!f) {
        sendCode(404);
        return;
      }
      server.sendHeader("Cache-Control", F("max-age=300"));
      server.streamFile(f, F("image/png"));
      f.close();
      return;
    }
    int t = server.arg("t").toInt();
    if (t < 0 || t >= THEME_COUNT) {
      sendCode(404);
      return;
    }
    server.sendHeader("Location", String(F("https://davicorrea0.github.io/smalltv-pomodoro/img/faces/face-")) + t + ".png");
    sendCode(302);
  });
  // anti-fantasma: v=0..63 aplica e mostra a estampa (3 min); save=1 guarda; stop=1 volta ao salvo
  server.on(F("/api/vcom"), HTTP_POST, [] {
    if (!guard()) return;
    if (server.hasArg("v")) {
      int v = server.arg("v").toInt();
      vcomTest = v < 0 ? 0 : v > 0x3F ? 0x3F : v;
      applyVcom(vcomTest);
      if (!vcomTestUntil) vcomDraw = 2;
      else if (vcomDraw < 1) vcomDraw = 1;
      vcomTestUntil = millis() + 180000;
      if (!vcomTestUntil) vcomTestUntil = 1;
    }
    if (server.arg("save") == "1") {
      cfg.vcom = vcomTest;
      cfg.vcomChk = cfg.vcom ^ 0xDA;
      saveSettings();
    }
    if (server.arg("save") == "1" || server.arg("stop") == "1") vcomTestUntil = millis();  // o loop encerra
    server.send(200, PSTR("application/json"), String(F("{\"vcom\":")) + cfg.vcom + "}");
  });
  // cor quente à noite: v=0..100 mostra a aparência noturna por 15 s (sem salvar); v=0 ou stop=1 encerra
  server.on(F("/api/warm"), HTTP_POST, [] {
    if (!guard()) return;
    int v = server.arg("v").toInt();
    warmPreview = v < 0 ? 0 : v > 100 ? 100 : v;
    warmPreviewUntil = server.arg("stop") == "1" || !warmPreview ? 0 : (millis() + 15000) | 1;
    bool conj = warmPreviewUntil && server.arg("c") == "1";  // tabela conjugada, só para comparar na tela
    if (conj != warmConj) warmSent = -1;
    warmConj = conj;
    updateBrightness(true);
    server.send(200, PSTR("application/json"), String(F("{\"warm\":")) + cfg.warm + "}");
  });
  server.on(F("/api/cmd"), HTTP_POST, [] {
    if (!guard()) return;
    if (server.arg("c") == "preview") {
      int t = server.arg("t").toInt();
      bool ok = t >= 0 && t < THEME_COUNT;
      if (server.hasArg("pkg")) {  // prévia de pacote: carrega no motor até a prévia acabar
        ok = loadPack(server.arg("pkg"));
        t = THEME_PACK;
      }
      if (ok) {
        previewTheme = t;
        previewUntil = millis() + 15000;
        viewUntil = millis() + 15000;  // o álbum não interrompe a prévia
      }
    }
    command(server.arg("c"));
    sendState();
  });
  server.on(F("/api/settings"), HTTP_POST, [] {
    if (!guard()) return;
    cfg.work = argRange("work", cfg.work, 1, 99);
    cfg.shortBreak = argRange("short", cfg.shortBreak, 1, 99);
    cfg.longBreak = argRange("long", cfg.longBreak, 1, 99);
    cfg.longEvery = argRange("every", cfg.longEvery, 1, 8);
    cfg.brightness = argRange("bri", cfg.brightness, 5, 100);  // mínimo 5: com 0 a tela apagaria
    cfg.autoStart = argRange("auto", cfg.autoStart, 0, 1);
    cfg.hour12 = argRange("h12", cfg.hour12, 0, 1);
    if (server.hasArg("theme") && server.arg("theme").length()) {  // embutido: libera o motor de pacotes
      cfg.theme = argRange("theme", cfg.theme, 0, THEME_COUNT - 1);
      facePackUnload();
    }
    if (server.hasArg("pkg") && loadPack(server.arg("pkg"))) {
      cfg.theme = THEME_PACK;
      strlcpy(cfg.facePkg, server.arg("pkg").c_str(), sizeof cfg.facePkg);
    }
    uint8_t goal = argRange("goal", cfg.dailyGoal, 1, 30);
    if (goal != cfg.dailyGoal) {  // a sequência é contada com a meta atual
      cfg.dailyGoal = goal;
      statsCompute();
    }
    cfg.activity = argRange("act", cfg.activity, 0, ACT_COUNT - 1);
    uint8_t lang = argRange("lang", cfg.lang, 0, 2);
    if (lang != cfg.lang) uiSetLang(cfg.lang = lang);
    uint8_t bgr = argRange("bgr", cfg.bgr, 0, 1), inv = argRange("inv", cfg.invert, 0, 1),
            flip = argRange("flip", cfg.flip, 0, 1);
    bool panelChanged = bgr != cfg.bgr || inv != cfg.invert || flip != cfg.flip;
    cfg.bgr = bgr;
    cfg.invert = inv;
    cfg.flip = flip;
    cfg.mode = argRange("mode", cfg.mode, 0, 2);
    cfg.albumDelay = argRange("delay", cfg.albumDelay, 3, 255);
    cfg.nightOn = argRange("non", cfg.nightOn, 0, 1);
    cfg.nightStart = argRange("nstart", cfg.nightStart, 0, 23);
    cfg.nightEnd = argRange("nend", cfg.nightEnd, 0, 23);
    cfg.nightBri = argRange("nbri", cfg.nightBri, 1, 100);
    cfg.nightSun = argRange("nsun", cfg.nightSun, 0, 1);
    cfg.warm = argRange("warm", cfg.warm, 0, 100);
    cfg.lcdGreen = argRange("lcdg", cfg.lcdGreen, 0, 1);
    cfg.rotMin = argRange("rotmin", cfg.rotMin, 0, 240);
    cfg.alexa = argRange("alexa", cfg.alexa, 0, 1);
    alexaBegin();  // ligada agora: começa a responder à descoberta sem reiniciar
    if (server.hasArg("rot")) rotSet(server.arg("rot"));
    uint8_t won = argRange("won", cfg.weatherOn, 0, 1);
    uint8_t fahr = argRange("fahr", cfg.fahr, 0, 1), rain = argRange("rain", cfg.rainAlert, 0, 1);
    cfg.beta = argRange("beta", cfg.beta, 0, 1);
    if (fahr != cfg.fahr || rain != cfg.rainAlert) uiInvalidate();  // números e aviso mudam na tela
    cfg.fahr = fahr;
    cfg.rainAlert = rain;
    const String &color = server.arg("color");
    if (color.length() == 7 && color[0] == '#') {
      long v = strtol(color.c_str() + 1, nullptr, 16);
      cfg.colR = v >> 16;
      cfg.colG = v >> 8;
      cfg.colB = v;
    }
    // aspas ou barra quebrariam o JSON de /api/state e travariam o painel
    String tz = server.arg("tz");
    tz.trim();
    if (tz.length() > 0 && safeText(tz)) {
      strlcpy(cfg.tz, tz.c_str(), sizeof(cfg.tz));
      applyTimezone();
    }
    bool newPlace = false;
    // fora de ±90/±180 não é lugar; também impede estouro na conversão decimal do estado
    float lat = parseDec(server.arg("lat")), lon = parseDec(server.arg("lon"));
    if (server.hasArg("lat") && server.hasArg("lon") && safeText(server.arg("city")) && fabsf(lat) <= 90 && fabsf(lon) <= 180) {
      cfg.lat = lat;
      cfg.lon = lon;
      strlcpy(cfg.city, server.arg("city").c_str(), sizeof(cfg.city));
      newPlace = true;
    }
    if (newPlace || won != cfg.weatherOn) {
      cfg.weatherOn = won;
      wx.ok = false;
      wx.version++;
      wxBack.next = millis();  // busca o clima já
    }
    saveSettings();
    updateBrightness(true);
    if (panelChanged) applyPanel();
    // Brilho, horário noturno e opções que não aparecem na tela: sem apagar e redesenhar tudo, que piscaria
    // a cada movimento do controle (junto com a rampa do brilho)
    static const char *const QUIET[] = {"bri", "non", "nstart", "nend", "nbri", "nsun", "warm", "lcdg", "beta", "alexa", "rotmin"};
    bool quiet = true;
    for (int i = 0; quiet && i < server.args(); i++) {
      bool q = false;
      for (const char *k : QUIET) q |= server.argName(i) == k;
      quiet = q || server.argName(i) == "plain";
    }
    if (!quiet) {
      uiInvalidate();  // redesenha com a cor/tema novos
      viewUntil = 0;   // reavalia relógio/álbum com o novo modo
    }
    sendState();
  });
  server.on(F("/api/files"), HTTP_GET, sendFiles);
  // instala um pacote de mostrador: valida no motor, usa o "id" do JSON como nome do arquivo
  server.on(F("/api/faceinstall"), HTTP_POST, [] {
    const bool ready = packFaceReady;
    packFaceReady = false;
    if (!guard()) return;
    String msg;
    bool ok = ready && !packUploadError.length() && loadPackFile("/faces/_novo.json", msg);
    String id = ok ? facePackId() : "", name = ok ? facePackName() : "";
    if (ok && id[0] == '_') {  // "_novo" e afins são os temporários do envio
      ok = false;
      msg = F("id invalido");
    }
    if (ok) {
      // sem remove antes: o rename do LittleFS (lfs_rename) troca o destino num commit só; apagar antes abria uma janela
      // em que a queda de energia deixava o mostrador sem nenhuma das duas versões
      LittleFS.rename("/faces/_novo.json", "/faces/" + id + ".json");
      LittleFS.remove("/faces/" + id + ".png");
      if (LittleFS.exists("/faces/_novo.png")) LittleFS.rename("/faces/_novo.png", "/faces/" + id + ".png");
      File t = LittleFS.open("/faces/" + id + ".txt", "w");  // metadados para a lista do painel
      if (t) {
        t.printf_P(PSTR("%s\n%s\n%s\n"), facePackName(), facePackDesc(), facePackAuthor());
        t.close();
      }
    } else {
      LittleFS.remove("/faces/_novo.json");
      LittleFS.remove("/faces/_novo.png");
    }
    restoreActivePack();  // o teste carregou o pacote novo no motor; volta ao que estava ativo
    if (!ok) {
      server.send(400, "text/plain", packUploadError.length() ? packUploadError : String(F("Pacote invalido: ")) + msg);
      return;
    }
    String reply;
    reply.reserve(id.length() + name.length() * 2 + 32);
    reply = F("{\"ok\":1,\"id\":\"");
    reply += id;
    reply += F("\",\"name\":\"");
    jsonEscapeAppend(reply, name.c_str());
    reply += F("\"}");
    server.send(200, PSTR("application/json"), reply);
  }, handlePackUpload);
  server.on(F("/api/facedelete"), HTTP_POST, [] {
    if (!guard()) return;
    const String &id = server.arg("pkg");
    if (!fsOk || !idOk(id) || !LittleFS.remove("/faces/" + id + ".json")) {
      server.send_P(400, PSTR("text/plain"), PSTR("Pacote nao encontrado"));
      return;
    }
    LittleFS.remove("/faces/" + id + ".png");
    LittleFS.remove("/faces/" + id + ".txt");
    if (cfg.theme == THEME_PACK && id == cfg.facePkg) {  // era o ativo: volta ao primeiro embutido
      cfg.theme = THEME_MATRIX;
      cfg.facePkg[0] = 0;
      saveSettings();
      facePackUnload();
      uiInvalidate();
    }
    sendOk();
  });
  server.on(F("/api/upload"), HTTP_POST, [] {
    if (!guard()) return;
    if (uploadError.length()) server.send(400, "text/plain", uploadError);
    else sendOk();
  }, handleUpload);
  server.on(F("/api/delete"), HTTP_POST, [] {
    if (!guard()) return;
    const String &f = server.arg("f");
    if (curFile == f) {
      stopAlbum();
      viewUntil = 0;
    }
    if (!fsOk || !mediaPath(f) || !LittleFS.remove(f)) {
      server.send_P(400, PSTR("text/plain"), PSTR("Nao foi possivel apagar"));
      return;
    }
    rebuildAlbum();
    sendOk();
  });
  server.on(F("/api/storage"), HTTP_GET, sendStorage);
  // apaga qualquer arquivo ou pasta da memória, menos o que o relógio precisa (fsKeep)
  server.on(F("/api/fsdelete"), HTTP_POST, [] {
    if (!guard()) return;
    String f = server.arg("f");
    if (!fsOk || !f.startsWith("/") || f.length() < 2 || f.length() > 96 || f.indexOf("..") >= 0 || f.indexOf("//") >= 0 ||
        f.endsWith("/") || fsKeep(f) || !LittleFS.exists(f)) {
      server.send_P(400, PSTR("text/plain"), PSTR("Nao foi possivel apagar"));
      return;
    }
    if (curFile == f || curFile.startsWith(f + "/")) {  // o arquivo na tela, ou a pasta dele
      stopAlbum();
      viewUntil = 0;
    }
    File h = LittleFS.open(f, "r");
    bool dir = h && h.isDirectory();
    if (h) h.close();
    bool ok = dir ? fsRemoveAll(f, 0) : LittleFS.remove(f);
    fsInfoAt = 0;  // o uso da memória mudou
    rebuildAlbum();
    server.send_P(ok ? 200 : 500, PSTR("text/plain"), ok ? PSTR("ok") : PSTR("Nao foi possivel apagar"));
  });
  server.on(F("/api/show"), HTTP_POST, [] {
    if (!guard()) return;
    const String &f = server.arg("f");
    if (phase != IDLE) {
      server.send_P(409, PSTR("text/plain"), PSTR("Pomodoro em andamento"));
      return;
    }
    if (!fsOk || !mediaPath(f) || !showAlbumItem(f)) {
      server.send_P(400, PSTR("text/plain"), PSTR("Arquivo invalido"));
      return;
    }
    viewUntil = millis() + cfg.albumDelay * 1000UL;
    sendOk();
  });
  server.on(F("/api/migdone"), HTTP_POST, [] {  // o painel restaurou (ou dispensou) o backup da migração
    if (!guard()) return;
    if (fsMark.magic == FS_MAGIC && fsMark.pend) {
      fsMark.pend = 0;
      EEPROM.put(FS_OFF, fsMark);
      EEPROM.commit();
    }
    sendOk();
  });
  server.on(F("/api/format"), HTTP_POST, [] {
    if (!guard()) return;
    stopAlbum();
    LittleFS.end();
    fsOk = LittleFS.format() && LittleFS.begin();
    if (fsOk) LittleFS.mkdir("/image");
    rebuildAlbum();
    server.send_P(fsOk ? 200 : 500, PSTR("text/plain"), fsOk ? PSTR("ok") : PSTR("falhou"));
  });
  // Arquivos de mídia para as miniaturas do painel
  server.on("/fs", HTTP_GET, [] {
    if (!readOk()) return;
    const String &f = server.arg("f");
    File file;
    if (fsOk && mediaPath(f)) file = LittleFS.open(f, "r");
    if (!file) {
      server.send_P(404, PSTR("text/plain"), PSTR("nao encontrado"));
      return;
    }
    server.sendHeader("Cache-Control", F("max-age=86400"));
    server.streamFile(file, isGif(f) ? F("image/gif") : F("image/jpeg"));
    file.close();
  });
  // Troca a senha do /update. Se já houver senha, exige a atual.
  server.on(F("/api/otapass"), HTTP_POST, [] {
    if (!guard()) return;
    if (cfg.otaPass[0] && server.arg("old") != cfg.otaPass) {
      server.send_P(403, PSTR("text/plain"), PSTR("Senha atual incorreta"));
      return;
    }
    const String &p = server.arg("new");
    if (p.length() >= sizeof(cfg.otaPass)) {
      server.send_P(400, PSTR("text/plain"), PSTR("Senha muito longa (max 23)"));
      return;
    }
    strlcpy(cfg.otaPass, p.c_str(), sizeof(cfg.otaPass));
    saveSettings();
    server.send_P(200, PSTR("text/plain"), p.length() ? PSTR("Senha definida (usuario: admin)") : PSTR("Senha removida"));
  });
  server.on(F("/api/wifireset"), HTTP_POST, [] {
    if (!guard()) return;
    sendOk();
    delay(300);
    WiFi.persistent(true);
    WiFi.disconnect(true);  // com persistent, apaga a rede salva
    safeRestart();
  });
  setupUpdate();
  server.on(F("/description.xml"), HTTP_GET, sendHueDescription);  // Alexa (ponte Hue)
  server.onNotFound(alexaApi);                                   // /api/<usuário>/lights...
  server.begin();
}

// ---------- Wi-Fi: rede salva ou portal de configuração ----------
// Portal próprio no lugar do WiFiManager (~45 KB): o binário precisa caber ao lado do firmware atual.
DNSServer dns;

bool portalMode = false;

// ---------- proteção contra rebaixamento do Wi-Fi (CVE-2020-12638) ----------
// Um beacon falso com o bit de privacidade desligado, junto com um aviso de troca de canal, faz o SDK
// do ESP8266 passar a conversar SEM criptografia com o ponto de acesso de quem está por perto. A
// Espressif não corrigiu isso no NONOS SDK do núcleo Arduino; o contorno conhecido é observar a troca
// do modo de autenticação e reconectar quando ele enfraquece. reconnect() mantém a rede salva
// (disconnect() apagaria a senha) e a nova conexão só aceita WPA ou melhor.
static WiFiEventHandler wifiAuthHandler;

static volatile bool wifiDowngrade = false;

static int authRank(uint8_t m) { return m == AUTH_OPEN ? 0 : m == AUTH_WEP ? 1 : 2; }  // WPA, WPA2 e misto: 2

// Rede salva por outro firmware (o original) pode não exigir WPA: passa a exigir, como faz o begin() do núcleo
// devolve true se precisou mudar a rede salva
static bool wifiHarden() {
  struct station_config conf;
  if (!wifi_station_get_config_default(&conf) || !conf.password[0]) return false;  // rede aberta salva: nada a exigir
  bool ok = conf.threshold.authmode >= AUTH_WPA_PSK;  // NONOS 2.2: só se conecta a redes com este modo ou melhor
#if (NONOSDK >= (0x30000))
  ok = ok && conf.open_and_wep_mode_disable;
  conf.open_and_wep_mode_disable = 1;
#endif
  if (ok) return false;
  conf.threshold.authmode = AUTH_WPA_PSK;
  wifi_station_set_config(&conf);
  return true;
}

void wifiGuardBegin() {
  wifiAuthHandler = WiFi.onStationModeAuthModeChanged([](const WiFiEventStationModeAuthModeChanged &e) {
    if (authRank(e.newMode) < authRank(e.oldMode)) wifiDowngrade = true;  // trata no loop, fora do SDK
  });
}

// Conexão com canal e BSSID fixos (ver connectStored): se cair e não voltar logo (o roteador mudou de canal ou, numa
// rede mesh, o ponto de acesso é outro), volta à rede salva sem fixar nada.
static bool wifiPinned = false;

static uint32_t wifiLostAt = 0;

static void wifiUnpin() {
  String ssid = WiFi.SSID(), psk = WiFi.psk();
  WiFi.persistent(false);
  WiFi.begin(ssid.c_str(), psk.c_str());
  wifiPinned = false;
}

void wifiGuardLoop() {
  if (wifiPinned) {
    if (WiFi.status() == WL_CONNECTED) wifiLostAt = 0;
    else if (!wifiLostAt) wifiLostAt = millis() | 1;
    else if (millis() - wifiLostAt > 15000) wifiUnpin();
  }
  if (!wifiDowngrade) return;
  wifiDowngrade = false;
  wifiDowngrades++;
  WiFi.reconnect();
}

static uint32_t ssidHash(const String &s) {  // FNV-1a
  uint32_t h = 2166136261u;
  for (char c : s) h = (h ^ (uint8_t)c) * 16777619u;
  return h;
}

// Espera conectar animando a barra da tela de início (ver bootAnim)
static bool waitWifi(uint32_t ms) {
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < ms) {
    bootAnim(0);
    delay(30);
  }
  return WiFi.status() == WL_CONNECTED;
}

// Rede salva no SDK. Com o canal e o BSSID do último roteador (EEPROM, ver wifiHintSave) o SDK não varre os 13
// canais: conecta direto. Só na RAM (persistent desligado): o BSSID fixo não vai para a flash, que segue com a rede
// de sempre. Se não conectar em poucos segundos, volta à varredura completa.
// O persistent fica desligado (padrão do núcleo 3.1.2) desde o boot e só é ligado logo antes de uma gravação de
// propósito (/save, /api/wifireset, /rec/wifi, /rec/wipe). Ligado, WiFi.mode() e softAP() gravam o modo e o AP na flash
// do SDK: no ciclo "rede sumiu → portal → reinicia em 5 min" o modo padrão era regravado a cada volta (~288/dia).
bool connectStored(uint32_t timeoutMs) {
  uint32_t t0 = millis();
  WiFi.mode(WIFI_STA);
  WiFi.hostname(netName);
  bool hardened = wifiHarden();
  if (!hardened && WiFi.status() == WL_CONNECTED) {  // o SDK conectou sozinho enquanto a tela iniciava
    bootWifiPath = 2;
    return true;
  }
  WifiHint h;
  EEPROM.get(WIFIH_OFF, h);
  String ssid = WiFi.SSID(), psk = WiFi.psk();
  if (!ssid.length() || h.magic != WIFI_MAGIC || h.ssid != ssidHash(ssid) || h.chan < 1 || h.chan > 13) {
    WiFi.begin();
    return waitWifi(timeoutMs);
  }
  WiFi.persistent(false);
  WiFi.begin(ssid.c_str(), psk.c_str(), h.chan, h.bssid);
  bool ok = waitWifi(min<uint32_t>(4000, timeoutMs));
  if (ok) {
    bootWifiPath = 1;
    wifiPinned = true;
  } else {
    bootWifiPath = 3;
    WiFi.begin(ssid.c_str(), psk.c_str());
    uint32_t used = millis() - t0;
    ok = waitWifi(used < timeoutMs ? timeoutMs - used : 0);
  }
  return ok;
}

// Guarda canal e BSSID do roteador atual; só grava a flash se mudou (outra rede, outro canal, outro roteador)
void wifiHintSave() {
  String ssid = WiFi.SSID();
  int32_t ch = WiFi.channel();
  if (!ssid.length() || ch < 1 || ch > 13) return;
  WifiHint h = {WIFI_MAGIC, ssidHash(ssid), {}, (uint8_t)ch, 0}, old;
  memcpy(h.bssid, WiFi.BSSID(), 6);
  EEPROM.get(WIFIH_OFF, old);
  if (!memcmp(&old, &h, sizeof h)) return;
  EEPROM.put(WIFIH_OFF, h);
  EEPROM.commit();
}

static void htmlEscapeAppend(String &o, const String &s) {
  for (char c : s) {
    if (c == '<') o += "&lt;";
    else if (c == '>') o += "&gt;";
    else if (c == '&') o += "&amp;";
    else if (c == '"') o += "&quot;";
    else o += c;
  }
}

// Idioma das páginas do portal e da recuperação: com o idioma dos ajustes em inglês (o padrão de fábrica) vale o do
// navegador (Accept-Language), que serve a quem acabou de instalar e nunca escolheu; português e espanhol nos ajustes são
// respeitados. Não há como saber se o inglês foi escolhido ou é o padrão (um campo novo na EEPROM não vale para duas
// páginas): quem escolheu inglês com o navegador em outra língua vê a página na língua do navegador. A tela do relógio
// não muda.
uint8_t pageLang() {
  if (cfg.lang != 1) return cfg.lang;
  uint8_t l = langFromAccept(server.header("Accept-Language").c_str());
  return l == 255 ? cfg.lang : l;
}

// Texto longo no idioma da página (o tr() corta em 47 bytes)
static void addTr(String &o, PGM_P pt, PGM_P en, PGM_P es) { o += FPSTR(uiLang == 1 ? en : uiLang == 2 ? es : pt); }

#define ADD_TR(o, pt, en, es) addTr(o, PSTR(pt), PSTR(en), PSTR(es))

static const char PORTAL_HEAD[] PROGMEM =
    "<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>SmallTV Wi-Fi</title><style>body{font:16px system-ui;background:#111;color:#eee;max-width:380px;margin:auto;padding:20px}"
    "input,button{width:100%;box-sizing:border-box;padding:12px;margin:6px 0 14px;border-radius:10px;border:1px solid #333;background:#1c1c1c;color:#eee;font:inherit}"
    "button{background:#ff5a47;border:0;font-weight:700}p{color:#aaa}a{color:#ff5a47}"
    ".n{display:flex;align-items:center;padding:10px 12px;margin:6px 0;border:1px solid #333;border-radius:10px;background:#1c1c1c;word-break:break-all}"
    ".n input{width:auto;margin:0 10px 0 0}</style><h2>SmallTV Wi-Fi</h2>";

// Redes da lista: sem repetidas, as mais fortes primeiro
static const int PORTAL_LIST_MAX = 12;

void startPortal() {
  newFormToken();
  portalMode = true;
  // A estação para de procurar a rede salva (pode ser a do firmware original, ou lixo): enquanto procura em outros
  // canais, o ponto de acesso muda de canal junto (o celular cai) e a varredura falha. A rede continua gravada
  // (disconnect com eraseCredentials = false; o disconnect(false) do núcleo apagaria). Os dois ajustes ficam só na
  // RAM do SDK: o /save os refaz antes de gravar a rede nova.
  WiFi.setAutoReconnect(false);
  WiFi.mode(WIFI_AP_STA);
  WiFi.disconnect(false, false);
  WiFi.softAP(AP_NAME, cfg.apPass);
  WiFi.scanNetworks(true);  // assíncrona: o servidor não espera os ~3 s da varredura; o GET / lê o resultado
  dns.start(53, "*", WiFi.softAPIP());
  setupQrShown = -1;
  setupQrScreen(false);
  server.collectHeaders("Accept-Language");
  server.on("/", HTTP_GET, [] {
    PageLang lang;
    int n = WiFi.scanComplete();  // WIFI_SCAN_RUNNING (-1), WIFI_SCAN_FAILED (-2) ou o número de redes
    uint8_t pick[PORTAL_LIST_MAX];
    int k = n > 0 ? scanPick(n, [](int i, char *d) { strlcpy(d, WiFi.SSID(i).c_str(), 33); },
                             [](int i) { return (int)WiFi.RSSI(i); }, pick, PORTAL_LIST_MAX)
                  : 0;
    server.sendHeader(F("Cache-Control"), F("no-store"));
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);  // HTTP em partes: a página não é montada inteira no heap
    server.send(200, "text/html", "");
    String c;
    c.reserve(800);
    c = FPSTR(PORTAL_HEAD);
    c += F("<p>");
    if (n == WIFI_SCAN_RUNNING) ADD_TR(c, "Procurando redes&hellip;", "Looking for networks&hellip;", "Buscando redes&hellip;");
    else if (k) ADD_TR(c, "Escolha a sua rede:", "Choose your network:", "Elige tu red:");
    else ADD_TR(c, "Nenhuma rede encontrada. O rel&oacute;gio s&oacute; enxerga redes de 2,4 GHz: aproxime-o do roteador e toque em Atualizar lista.",
                "No networks found. The clock only sees 2.4 GHz networks: move it closer to the router and tap Refresh list.",
                "No se encontr&oacute; ninguna red. El reloj solo ve redes de 2,4 GHz: ac&eacute;rcalo al router y toca Actualizar lista.");
    c += F("</p><form method=post action=/save>");
    for (int j = 0; j < k; j++) {
      String ssid = WiFi.SSID(pick[j]);
      c += F("<label class=n><input type=radio name=s value=\"");
      htmlEscapeAppend(c, ssid);
      c += F("\">");
      htmlEscapeAppend(c, ssid);
      c += F("</label>");
      if (c.length() > 480) {
        server.sendContent(c);
        c = "";
      }
    }
    ADD_TR(c, "Nome da rede (se n&atilde;o estiver na lista)", "Network name (if not listed)", "Nombre de la red (si no aparece)");
    c += F("<input name=h autocapitalize=none autocorrect=off spellcheck=false>");
    c += formTokenField();
    ADD_TR(c, "Senha", "Password", "Contrase&ntilde;a");
    c += F("<input name=p type=password><button>");
    ADD_TR(c, "Conectar", "Connect", "Conectar");
    c += F("</button></form><p><a href=/scan>");
    ADD_TR(c, "Atualizar lista", "Refresh list", "Actualizar lista");
    c += F("</a></p>");
    // varredura em andamento: tenta de novo em 3 s, sem apagar o que a pessoa já digitou
    if (n == WIFI_SCAN_RUNNING)
      c += F("<script>setTimeout(function(){var f=document.forms[0];if(!f.h.value&&!f.p.value)location.reload()},3000)</script>");
    server.sendContent(c);
    server.sendContent("");
  });
  server.on("/scan", HTTP_GET, [] {  // "Atualizar lista": recomeça a varredura (já em andamento: nada muda)
    WiFi.scanNetworks(true);
    server.sendHeader("Location", "/");
    sendCode(302);
  });
  server.on("/save", HTTP_POST, [] {
    if (!formOk()) { deny(); return; }
    PageLang lang;
    String ssid = server.arg("h");  // nome digitado (rede oculta ou fora da lista) vale mais que a rede marcada
    if (!ssid.length()) ssid = server.arg("s");
    String p = F("<meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
                 "<body style='font:16px system-ui;background:#111;color:#eee;padding:20px'>");
    if (!ssid.length() || ssid.length() > 32) {
      ADD_TR(p, "Escolha uma rede da lista ou digite o nome dela.", "Pick a network from the list or type its name.",
             "Elige una red de la lista o escribe su nombre.");
      p += F("<p><a href=/ style=color:#ff5a47>");
      ADD_TR(p, "Voltar", "Back", "Volver");
      p += F("</a>");
      server.send(400, "text/html", p);
      return;
    }
    ADD_TR(p, "Conectando... O rel&oacute;gio vai reiniciar e mostrar o endere&ccedil;o do painel na tela.",
           "Connecting... The clock will restart and show the panel address on screen.",
           "Conectando... El reloj se reiniciar&aacute; y mostrar&aacute; la direcci&oacute;n del panel en la pantalla.");
    server.send(200, "text/html", p);
    delay(500);
    WiFi.setAutoReconnect(true);  // desfaz a parada do startPortal antes de gravar a rede (se o SDK guardar a política junto, guarda a ligada)
    WiFi.persistent(true);
    WiFi.begin(ssid.c_str(), server.arg("p").c_str());
    delay(1000);
    safeRestart();
  });
  // qualquer outro endereço cai no formulário (portal cativo do celular)
  server.onNotFound([] {
    server.sendHeader("Location", F("http://192.168.4.1/"));
    sendCode(302);
  });
  server.begin();
}
