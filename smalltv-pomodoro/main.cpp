// SmallTV Ultra (GeekMagic, ESP8266) - firmware alternativo
// Relógio (3 temas) + clima + álbum de fotos/GIFs + brilho noturno + Pomodoro
//
// Hardware (fonte: devices.esphome.io/devices/geekmagic-ultra):
//   Tela ST7789 240x240, SPI hardware CLK=GPIO14 MOSI=GPIO13, sem CS (SPI modo 3)
//   DC=GPIO0  RST=GPIO2  Backlight=GPIO5 (PWM invertido)
// Flash: layout 4M3M, igual ao firmware original (LittleFS em 0x100000-0x3FA000, constantes
// encontradas no binário V9.0.50 no offset 0x14d00). Assim as fotos e GIFs já gravados são lidos.

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <base64.h>
#include <DNSServer.h>
#include <EEPROM.h>
#include <LittleFS.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <ArduinoJson.h>
#include <WiFiUdp.h>
#include <eboot_command.h>
#include <bearssl/bearssl_hash.h>  // SHA-256 do painel recebido (/api/panel)
#include <bearssl/bearssl_rsa.h>  // conferência da assinatura das atualizações (RSA-2048)
#include <StackThunk.h>
#include <libb64/cdecode.h>
#include "ota_key.h"  // chave PÚBLICA das atualizações (gerada por scripts/assinar.cjs)
#include "src/uzlib/uzlib.h"  // o mesmo descompactador do gerenciador de boot (licença zlib)
extern "C" uint32_t _FS_start;
#include "src/JPEGDEC/JPEGDEC.h"  // cópia da JPEGDEC 1.8.4 com as tabelas de cor geradas só durante a decodificação
#include "src/AnimatedGIF/AnimatedGIF.h"
#include "gifcheck.h"
#include "security.h"
#include <new>
#include <vector>
#include <time.h>
#include <sys/time.h>
#include <coredecls.h>  // settimeofday_cb: avisa quando o SNTP muda a hora
#include "ui.h"
#include "web.h"
#include "facepack.h"

void blit(int16_t x, int16_t y, uint16_t *px, int16_t w, int16_t h);  // ui.cpp: recorta para a área suja; fora da tela, cai na faixa

#define FW_VERSION "1.23.1"
#define HOSTNAME   "smalltv"
#define AP_NAME    "SmallTV-Pomodoro"

#define PIN_DC  0
#define PIN_RST 2
#define PIN_BL  5


Adafruit_ST7789 tft(-1, PIN_DC, PIN_RST);

// Linhas de pixels para a tela. A Adafruit GFX 1.13 manda cada linha pelo SPI.writeBytes, que lê a
// memória de 4 em 4 bytes: com a linha num endereço que não é múltiplo de 4 (texto da fonte vetorial,
// recortes, faixas de largura ímpar) o ESP8266 dá exceção de alinhamento e reinicia (issue #4).
// Toda linha passa por aqui: desalinhada, vai por uma cópia alinhada.
static void tftPixels(uint16_t *px, int n) {
  static uint32_t al[120];  // 240 pixels
  if (!((uintptr_t)px & 3)) {
    tft.writePixels(px, n);
    return;
  }
  while (n > 0) {
    int k = n < 240 ? n : 240;
    memcpy(al, px, k * 2);
    tft.writePixels((uint16_t *)al, k);
    px += k;
    n -= k;
  }
}

// drawRGBBitmap com o recorte da tela feito antes, para cada linha passar por tftPixels
static void tftBitmap(int16_t x, int16_t y, uint16_t *px, int16_t w, int16_t h) {
  int x0 = max<int>(x, 0), y0 = max<int>(y, 0), x1 = min<int>(x + w, tft.width()), y1 = min<int>(y + h, tft.height());
  if (x1 <= x0 || y1 <= y0) return;
  tft.startWrite();
  tft.setAddrWindow(x0, y0, x1 - x0, y1 - y0);
  for (int r = y0; r < y1; r++) tftPixels(px + (r - y) * w + (x0 - x), x1 - x0);
  tft.endWrite();
}

ESP8266WebServer server(80);
bool panelOk();  // proteção do painel (mais abaixo, antes de setupWeb)
bool readOk();
uint16_t wifiDowngrades = 0;  // rebaixamentos do Wi-Fi bloqueados desde que ligou (ver wifiGuardLoop)
bool passOk();
bool uploadAuthed();
// Formulários HTML puros (configuração e recuperação) levam um código aleatório da sessão: outra página
// pode enviar o formulário, mas não consegue ler o código.
String formToken;
void newFormToken() {
  char token[33];
  snprintf(token, sizeof token, "%08x%08x%08x%08x", ESP.random(), ESP.random(), ESP.random(), ESP.random());
  formToken = token;
}
String formTokenField() { return "<input type=hidden name=csrf value=\"" + formToken + "\">"; }
bool formOk() { return formToken.length() && server.arg("csrf") == formToken; }

// ---------- cores ----------
constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) { return uiRgb(r, g, b); }

// ---------- configurações (EEPROM) ----------
struct Settings {
  uint32_t magic;
  uint8_t work, shortBreak, longBreak, longEvery;
  uint8_t brightness;   // 5-100
  uint8_t autoStart;    // inicia a próxima fase sozinho
  uint8_t hour12;
  char tz[48];          // string POSIX TZ
  char otaPass[24];     // senha do /update (vazia = sem senha)
  uint16_t todayCount;  // focos do dia, sobrevive a reinícios
  int16_t todayYday;
  uint8_t theme;        // 0 digital, 1 analógico, 2 clima
  uint8_t mode;         // 0 só relógio, 1 álbum, 2 alternar relógio/álbum
  uint8_t albumDelay;   // segundos por item
  uint8_t nightOn, nightStart, nightEnd, nightBri;
  uint8_t weatherOn;
  uint8_t colR, colG, colB;  // cor do relógio
  float lat, lon;
  char city[32];
  // v4: ajustes do painel (variam de unidade para unidade) e meta diária
  uint8_t bgr;          // 0 = RGB (padrão do Ultra, igual ao smalltv-mod), 1 = BGR
  uint8_t invert;       // 1 = INVON (padrão do painel IPS)
  uint8_t flip;         // 1 = girar 180°
  uint8_t dailyGoal;    // meta de focos por dia
  uint8_t activity;     // atividade do foco (UiActivity); campo novo no fim: unidades antigas leem lixo e caem no padrão
  char facePkg[24];     // pacote de mostrador ativo (/faces/<id>.json) quando theme == THEME_PACK; idem: lixo vira ""
  uint8_t lang;         // idioma da tela e do painel: 0 português, 1 inglês, 2 espanhol; idem: lixo vira 0
  uint8_t nightSun;     // brilho noturno do pôr ao nascer do sol (Open-Meteo) em vez de nightStart/nightEnd
  uint8_t rotMin;       // rodízio de mostradores: minutos entre trocas (0 = desligado); lista em /rotation.txt
  uint8_t alexa;        // 1 = aparece para a Alexa como a lâmpada "Pomodoro"
  char apPass[9];       // senha das redes SmallTV-Pomodoro e SmallTV-Recuperacao (sorteada, mostrada na tela)
  uint8_t vcom, vcomChk;  // tensão comum do painel (registrador VCOMS) e marca de calibrado (vcom ^ 0xDA)
  uint8_t fahr;         // 1 = temperaturas em °F; lixo vira 0
  uint8_t rainAlert;    // 1 = avisa na tela quando vai chover na próxima hora; lixo vira 1
  uint8_t beta;         // 1 = o painel oferece também as versões de teste (firmware/beta.json); lixo vira 0
  uint8_t spi80;        // reservado: era a opção de 80 MHz da 1.23.0 (o ST7789 não aguenta e a imagem vira lixo); sempre 0
};
const uint32_t SETTINGS_MAGIC = 0x504F4D34;     // "POM4"
const uint32_t SETTINGS_MAGIC_V3 = 0x504F4D33;  // "POM3": mesmos campos, sem os do v4
Settings cfg;

void defaultSettings() {
  cfg = Settings{SETTINGS_MAGIC, 25, 5, 15, 4, 80, 0, 0, "<-03>3", "", 0, -1,
                 THEME_MATRIX, 0, 10, 0, 22, 7, 15, 1, 242, 242, 242, 0, 0, "", 0, 1, 0, 8, ACT_READ, "", 0, 0, 0, 1, "", UI_VCOM_DEFAULT, 0, 0, 1, 0, 0};
}

// ---------- recuperação: 3 inicializações curtas seguidas ----------
// O aparelho não tem botão: ligar e desligar 3 vezes seguidas (cada vez por menos de 30 s), ou um
// firmware que trava logo ao ligar, abre o modo de recuperação (rede própria, atualização sem senha,
// ajustes de fábrica). O contador fica na EEPROM, depois das configurações: sobrevive a tirar da
// tomada, ao contrário da memória RTC. 30 s ligado zeram a contagem.
const int BOOT_OFF = 400;
static_assert(sizeof(Settings) <= BOOT_OFF, "as configuracoes invadiriam o contador de inicializacoes");
struct BootCount { uint32_t magic; uint8_t n; };
const uint32_t BOOT_MAGIC = 0x424F4F54;  // "BOOT"
uint8_t bootCount = 0;
bool recoveryMode = false;

// Última queda (exceção ou watchdog), para o painel dizer por que o relógio reiniciou. Fica na EEPROM
// depois do contador e só é gravada quando o reinício foi anormal: início normal não gasta flash.
// "up" (segundos ligado antes da queda) vem da memória RTC, que o callback abaixo preenche ao travar.
struct CrashRec { uint32_t magic; uint8_t n, reason, exc, pad; uint32_t epc, vaddr, up; char ver[8]; };
const uint32_t CRASH_MAGIC = 0x43524153;  // "CRAS"
const int CRASH_OFF = BOOT_OFF + sizeof(BootCount);
const int EE_SIZE = CRASH_OFF + sizeof(CrashRec);
static_assert(sizeof(CrashRec) == 28, "registro da queda mudou de tamanho");
static_assert(sizeof(FW_VERSION) <= sizeof(CrashRec::ver), "a versao nao cabe no registro da queda");
const uint32_t CRASH_RTC_TAG = 0x55505453;  // "UPTS"
CrashRec crashRec;  // magic != CRASH_MAGIC: nenhuma queda guardada

// roda no travamento (o core chama no lugar do aviso extra): guarda há quanto tempo estava ligado
extern "C" void custom_crash_callback(struct rst_info *, uint32_t, uint32_t) {
  uint32_t v[2] = {CRASH_RTC_TAG, millis() / 1000};
  ESP.rtcUserMemoryWrite(0, v, sizeof v);
}

void crashLoad() {
  EEPROM.get(CRASH_OFF, crashRec);
  if (crashRec.magic != CRASH_MAGIC) crashRec.magic = 0;
  const rst_info *r = ESP.getResetInfoPtr();
  if (!r || !(r->reason == REASON_WDT_RST || r->reason == REASON_EXCEPTION_RST || r->reason == REASON_SOFT_WDT_RST)) return;
  uint32_t v[2] = {0, 0};
  ESP.rtcUserMemoryRead(0, v, sizeof v);
  uint8_t n = crashRec.magic ? crashRec.n : 0;
  crashRec = {CRASH_MAGIC, (uint8_t)(n < 255 ? n + 1 : n), (uint8_t)r->reason, (uint8_t)r->exccause, 0,
              r->epc1, r->excvaddr, v[0] == CRASH_RTC_TAG ? v[1] : 0, {}};
  strlcpy(crashRec.ver, FW_VERSION, sizeof crashRec.ver);
  EEPROM.put(CRASH_OFF, crashRec);
  EEPROM.commit();
}

void setBootCount(uint8_t n) {
  BootCount b = {BOOT_MAGIC, n};
  EEPROM.put(BOOT_OFF, b);
  EEPROM.commit();
  bootCount = n;
}

// reinício pedido (Wi-Fi, atualização...): não conta como inicialização curta
void safeRestart() {
  if (bootCount) setBootCount(0);
  ESP.restart();
}

void loadSettings() {
  EEPROM.begin(EE_SIZE);
  EEPROM.get(0, cfg);
  if (cfg.magic == SETTINGS_MAGIC_V3) {
    // v3 -> v4: mantém tudo, completa os campos novos e remapeia os temas
    // (v3: 0 digital, 1 analógico, 2 clima; v4: 0 matriz, 1 tomate, 2 analógico, 3 clima)
    const uint8_t themeMap[3] = {THEME_MATRIX, THEME_ANALOG, THEME_WEATHER};
    cfg.theme = cfg.theme < 3 ? themeMap[cfg.theme] : THEME_MATRIX;
    cfg.bgr = 0;
    cfg.invert = 1;
    cfg.flip = 0;
    cfg.dailyGoal = 8;
    cfg.activity = ACT_READ;
    cfg.magic = SETTINGS_MAGIC;
  } else if (cfg.magic != SETTINGS_MAGIC) {
    defaultSettings();
  }
  cfg.facePkg[sizeof(cfg.facePkg) - 1] = 0;
  for (char *p = cfg.facePkg; *p; p++)  // id inválido (memória antiga): sem pacote
    if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '-' || *p == '_')) {
      cfg.facePkg[0] = 0;
      break;
    }
  if (cfg.theme >= THEME_COUNT && !(cfg.theme == THEME_PACK && cfg.facePkg[0])) cfg.theme = THEME_MATRIX;
  if (!cfg.dailyGoal) cfg.dailyGoal = 8;
  if (cfg.activity >= ACT_COUNT) cfg.activity = ACT_READ;
  if (cfg.lang > 2) cfg.lang = 0;
  if (cfg.nightSun > 1) cfg.nightSun = 0;
  if (cfg.rotMin > 240) cfg.rotMin = 0;
  if (cfg.alexa > 1) cfg.alexa = 1;  // memória de unidades antigas (0xFF): ligado
  if (cfg.fahr > 1) cfg.fahr = 0;
  if (cfg.rainAlert > 1) cfg.rainAlert = 1;
  if (cfg.beta > 1) cfg.beta = 0;
  cfg.spi80 = 0;  // quem ligou na 1.23.0 volta aos 40 MHz
  if (!(fabsf(cfg.lat) <= 90 && fabsf(cfg.lon) <= 180)) cfg.lat = cfg.lon = 0;  // lixo ou NaN estouraria o String(float)
  // senha das redes do relógio: 8 letras/dígitos sem os que se confundem, sorteada pelo gerador do chip
  // na primeira vez (ou se a memória tiver lixo) e mostrada na tela quando a rede está no ar
  static const char AP_CHARS[] = "abcdefghjkmnpqrstuvwxyz23456789";
  // VCOM: só fica o valor que a pessoa calibrou (com a marca); lixo de memória antiga, a 1.16.0
  // (marca 0x5A) e quem nunca calibrou seguem o padrão atual
  if (cfg.vcom > 0x3F || cfg.vcomChk != (cfg.vcom ^ 0xDA)) {
    cfg.vcom = UI_VCOM_DEFAULT;
    cfg.vcomChk = 0;
  }
  bool apOk = strnlen(cfg.apPass, sizeof cfg.apPass) == 8;
  for (int i = 0; apOk && i < 8; i++) apOk = strchr(AP_CHARS, cfg.apPass[i]) != nullptr;
  if (!apOk) {
    for (int i = 0; i < 8; i++) cfg.apPass[i] = AP_CHARS[ESP.random() % (sizeof AP_CHARS - 1)];
    cfg.apPass[8] = 0;
    EEPROM.put(0, cfg);  // grava já (saveSettings vem mais abaixo)
    EEPROM.commit();
  }
  cfg.tz[sizeof(cfg.tz) - 1] = 0;
  cfg.otaPass[sizeof(cfg.otaPass) - 1] = 0;
  cfg.city[sizeof(cfg.city) - 1] = 0;
  if (cfg.albumDelay < 3) cfg.albumDelay = 3;
}

void saveSettings() {
  EEPROM.put(0, cfg);
  EEPROM.commit();
}

uint16_t clockColor() { return rgb(cfg.colR, cfg.colG, cfg.colB); }

// ---------- tempo ----------
bool timeValid() { return time(nullptr) > 1700000000; }
TimeGuard timeGuard;      // hora confirmada por uma segunda fonte (security.h); ver timeGuardBegin
uint8_t statsPending = 0;  // focos terminados antes da hora ser confirmada (gravados no histórico depois)
// só grava o histórico com a hora confirmada: um NTP forjado não apaga nem embaralha os dias
bool timeTrusted() { return timeGuard.ok(time(nullptr), millis()); }

bool localNow(struct tm &t) {
  if (!timeValid()) return false;
  time_t now = time(nullptr);
  localtime_r(&now, &t);
  return true;
}

void applyTimezone() {
  configTime(cfg.tz, "pool.ntp.org", "time.google.com", "a.st1.ntp.br");
}

// ---------- brilho (com horário noturno) ----------
// PWM a 20 kHz (fora da faixa audível e de piscar) com 1023 degraus; pino invertido (0 = aceso no máximo).
// Curva gama 2.2 (perceptual) sobre o 0-100 do cfg; 1 % já sai com ~1 % de duty, para não apagar.
const uint16_t BL_RANGE = 1023;
const uint32_t BL_HZ = 20000;
const uint16_t BL_MIN = 10;      // duty mínimo para qualquer brilho > 0
const uint16_t BL_FADE_MS = 400;  // rampa entre dois níveis (dia/noite, slider)
int curBri = -1;
uint16_t blFrom = 0, blTo = 0, blNow = 0;
uint32_t blT0 = 0;
bool blBoot = true;  // primeira escrita (boot): sem rampa

uint16_t briDuty(int b) {
  if (b <= 0) return 0;
  return BL_MIN + (uint16_t)((BL_RANGE - BL_MIN) * powf(b / 100.0f, 2.2f) + 0.5f);
}

int16_t sunRise = -1, sunSet = -1;  // minutos do dia (hora local), da última busca do clima

// Chamada a cada volta do loop: anda da posição atual até o alvo em BL_FADE_MS e só mexe no PWM quando muda
void briFade() {
  uint32_t el = millis() - blT0;
  uint16_t lvl = el >= BL_FADE_MS ? blTo : blFrom + (int32_t)(blTo - blFrom) * (int32_t)el / (int32_t)BL_FADE_MS;
  if (lvl == blNow) return;
  blNow = lvl;
  analogWrite(PIN_BL, BL_RANGE - lvl);
}

void updateBrightness(bool force = false) {
  int b = cfg.brightness;
  struct tm t;
  if (cfg.nightOn && localNow(t)) {
    int h = t.tm_hour, m = h * 60 + t.tm_min, s = cfg.nightStart, e = cfg.nightEnd;
    bool night = false;
    if (cfg.nightSun && sunRise >= 0 && sunSet > sunRise) night = m < sunRise || m >= sunSet;  // do pôr ao nascer
    else if (s != e) night = s < e ? (h >= s && h < e) : (h >= s || h < e);  // horário fixo (ou sem o sol ainda)
    if (night) b = cfg.nightBri;
  }
  if (b == curBri && !force) return;
  curBri = b;
  blFrom = blNow;
  blTo = briDuty(b);
  blT0 = millis();
  if (blBoot) {  // boot: acende direto no nível pedido
    blBoot = false;
    blFrom = blTo;
    blNow = blTo;
    analogWrite(PIN_BL, BL_RANGE - blTo);
    return;
  }
  briFade();
}

// ---------- Pomodoro ----------
enum Phase : uint8_t { IDLE, WORK, SHORT_BREAK, LONG_BREAK };
const char *PHASE_KEY[] = {"idle", "work", "short", "long"};
const char *PHASE_LABEL[] = {"", "FOCO", "PAUSA", "PAUSA LONGA"};

Phase phase = IDLE;
bool running = false;
uint32_t totalMs = 0, remainingMs = 0, lastTick = 0;
uint8_t cycleDone = 0;      // focos concluídos no ciclo atual
uint32_t alertUntil = 0;

uint8_t phaseMinutes(Phase p) {
  return p == WORK ? cfg.work : p == SHORT_BREAK ? cfg.shortBreak : cfg.longBreak;
}

// Cada fase que começa ganha um número (phaseSeq); quando a tela termina de desenhá-la, com os números,
// shownSeq passa a ser esse número. O painel espera os dois baterem para mostrar a contagem: nunca começa
// antes do relógio (ver "pseq"/"pshown" em /api/state).
uint16_t phaseSeq = 0, shownSeq = 0;

void startPhase(Phase p, bool run) {
  phase = p;
  totalMs = remainingMs = (uint32_t)phaseMinutes(p) * 60000UL;
  running = run;
  lastTick = millis();
  phaseSeq++;
}

void statsFocus();                      // um foco terminou (histórico de focos, mais abaixo)
void statsRollover(const struct tm &t);  // o dia mudou
void statsFlush(const struct tm &t);     // grava os focos que terminaram antes da hora ser confirmada
extern uint16_t statsStreak, statsBest;

int8_t hookPending = -1;  // aviso de fim de fase a enviar no loop: 0 foco, 1 pausa (avisos na Alexa)
String hookWork, hookBreak;  // endereços dos avisos (mais abaixo, em "avisos de fim de fase")
int hookFire(const String &url);
String hookClean(String u);

void advance(bool completed) {
  if (completed) hookPending = phase == WORK ? 0 : 1;
  if (phase == WORK) {
    if (completed) {
      cycleDone++;
      cfg.todayCount++;
      saveSettings();
      statsFocus();
    }
    bool longNext = completed && cfg.longEvery && cycleDone % cfg.longEvery == 0;
    startPhase(longNext ? LONG_BREAK : SHORT_BREAK, cfg.autoStart);
  } else {
    if (phase == LONG_BREAK) cycleDone = 0;
    startPhase(WORK, cfg.autoStart);
  }
}

void tickPomodoro() {
  if (!running) return;
  uint32_t now = millis();
  uint32_t dt = now - lastTick;
  lastTick = now;
  if (dt >= remainingMs) {
    remainingMs = 0;
    alertUntil = now + 4000;
    advance(true);
  } else {
    remainingMs -= dt;
  }
}

bool colorTestRequested = false;  // tratado no loop (precisa da tela)

// Prévia de mostrador pedida pelo painel: mostra outro tema por alguns segundos sem salvar.
uint8_t previewTheme = 0;
uint32_t previewUntil = 0;
bool previewActive() { return previewUntil && (int32_t)(millis() - previewUntil) < 0; }

void command(const String &c) {
  if (c == "start") {
    if (phase == IDLE) startPhase(WORK, true);
    else { running = true; lastTick = millis(); }
  } else if (c == "pause") {
    running = false;
  } else if (c == "toggle") {
    command(running ? "pause" : "start");
  } else if (c == "skip") {
    if (phase != IDLE) advance(false);
  } else if (c == "reset") {
    phase = IDLE;
    running = false;
    cycleDone = 0;
  } else if (c == "focus") {
    startPhase(WORK, true);
  } else if (c == "short") {
    startPhase(SHORT_BREAK, true);
  } else if (c == "long") {
    startPhase(LONG_BREAK, true);
  } else if (c == "colortest") {
    colorTestRequested = true;
  }
}

// ---------- Alexa ----------
// O relógio aparece para a Alexa como lâmpadas reguláveis de uma ponte Philips Hue, na rede local: sem
// conta, skill nem nuvem. É o mesmo protocolo do Espalexa (respostas iguais às dele, que os Echo aceitam),
// reduzido ao que o Pomodoro usa e sem cor: a biblioteca inteira custaria ~26 KB de flash.
//   descoberta: o Echo manda M-SEARCH por SSDP (UDP 239.255.255.250:1900) e lê /description.xml;
//   controle:   GET /api/<usuário>/lights e PUT /api/<usuário>/lights/<id>/state {"on":..,"bri":..}.
// Lâmpadas (o nome sai no idioma do relógio na hora da descoberta):
//   Pomodoro    ligar: começa o foco (ou continua um pausado); desligar: pausa, e de novo encerra;
//               "em 50%": foco de 50 minutos
//   Pausa       ligar: pausa curta ("em 10%": de 10 minutos); desligar: volta ao foco
//   Pausa longa ligar: pausa longa ("em 30%": de 30 minutos); desligar: volta ao foco
// A lâmpada acesa mostra a fase atual no app da Alexa.
// Nome na rede, sem decorar o IP:
//  - lanName: http://smalltv.local; com outro relógio já usando, smalltv-2.local, -3... (conferido ao conectar)
//  - netName: "smalltv-" + 4 hex do chip, sempre válido como reserva e usado no DHCP.
// Só o registro A, com o leitor de pacotes de security.h (testado): o ESP8266mDNS custaria ~21 KB.
char netName[16], lanName[12];
WiFiUDP hueUdp;
WiFiUDP mdnsUdp;
IPAddress mdnsIp;

void mdnsSend(const char *name, uint16_t id, bool legacy, bool direct) {
  uint8_t out[80], ip[4] = {mdnsIp[0], mdnsIp[1], mdnsIp[2], mdnsIp[3]};
  size_t m = mdnsAnswer(out, sizeof out, name, ip, id, legacy);
  if (!m) return;
  if (direct) mdnsUdp.beginPacket(mdnsUdp.remoteIP(), mdnsUdp.remotePort());
  else mdnsUdp.beginPacketMulticast(IPAddress(224, 0, 0, 251), 5353, mdnsIp);
  mdnsUdp.write(out, m);
  mdnsUdp.endPacket();
}

void mdnsAnnounce() {
  if (lanName[0]) mdnsSend(lanName, 0, false, false);
  mdnsSend(netName, 0, false, false);
}

bool mdnsJoin() {
  mdnsIp = WiFi.localIP();
  mdnsUdp.stop();
  return mdnsUdp.beginMulticast(mdnsIp, IPAddress(224, 0, 0, 251), 5353);
}

// Alguém já responde por esse nome? Pergunta 3 vezes, 250 ms cada (RFC 6762, 8.1).
bool mdnsTaken(const char *name) {
  uint8_t q[48], b[256];
  size_t m = mdnsProbeQuery(q, sizeof q, name);
  for (int t = 0; t < 3 && m; t++) {
    mdnsUdp.beginPacketMulticast(IPAddress(224, 0, 0, 251), 5353, mdnsIp);
    mdnsUdp.write(q, m);
    mdnsUdp.endPacket();
    for (uint32_t t0 = millis(); millis() - t0 < 250; delay(10)) {
      if (mdnsUdp.parsePacket() <= 0) continue;
      int n = mdnsUdp.read(b, sizeof b);
      if (n > 0 && mdnsHasAnswer(b, n, name)) return true;
    }
  }
  return false;
}

// Ao conectar: escolhe o nome curto livre e se anuncia.
void mdnsStart() {
  lanName[0] = 0;
  if (!mdnsJoin()) return;
  for (int i = 1; i <= 9; i++) {
    char name[sizeof lanName];
    if (i == 1) strlcpy(name, HOSTNAME, sizeof name);
    else snprintf(name, sizeof name, HOSTNAME "-%d", i);
    if (!mdnsTaken(name)) {
      strlcpy(lanName, name, sizeof lanName);
      break;
    }
  }
  mdnsAnnounce();
}

void mdnsLoop() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (WiFi.localIP() != mdnsIp) {  // o IP mudou: entra de novo no grupo e se anuncia com o mesmo nome
    if (mdnsJoin()) mdnsAnnounce();
  }
  int len = mdnsUdp.parsePacket();
  if (len <= 0) return;
  uint8_t b[256];  // a pergunta vem no começo; o resto (respostas conhecidas) pode ficar de fora
  int n = mdnsUdp.read(b, sizeof b);
  if (n <= 0) return;
  bool legacy = mdnsUdp.remotePort() != 5353, qu = false;
  uint16_t id = (uint16_t)(b[0] << 8 | b[1]);
  if (lanName[0] && mdnsAsksFor(b, n, lanName, &qu)) mdnsSend(lanName, id, legacy, legacy || qu);
  else if (mdnsAsksFor(b, n, netName, &qu)) mdnsSend(netName, id, legacy, legacy || qu);
}
bool alexaUp = false;     // ouvindo a descoberta
uint8_t alexaBri = 255;   // último brilho pedido (1-255)
uint32_t alexaSeen = 0;   // millis() do último pedido de um Echo (0 = nenhum desde que ligou)
IPAddress alexaIp;        // de onde veio
// último comando para o painel: 0 nenhum, 1 começou/continuou o foco, 2 pausou, 3 foco de N min,
// 4 encerrou, 5 pausa curta, 6 pausa longa, 7 voltou ao foco, 8 pausa de N min
uint8_t alexaLast = 0;
uint8_t alexaMin = 0;     // N dos comandos 3 e 8
char hueMac[13];          // MAC em minúsculas, sem ":" (identificador da "ponte")
const int HUE_LIGHTS = 3;

// chave da lâmpada como o Espalexa: 24 bits de baixo do MAC << 7 | índice (int, pode ser negativa)
long hueKey(int i) { return (long)(int)((strtoul(hueMac + 6, nullptr, 16) << 7) | i); }

bool hueOn(int i) {
  return i == 0 ? phase == WORK && running : i == 1 ? phase == SHORT_BREAK : phase == LONG_BREAK;
}

void hueLight(String &j, int i) {
  uint8_t m[6];
  WiFi.macAddress(m);
  char id[27];
  snprintf(id, sizeof id, "%02X:%02X:%02X:%02X:%02X:%02X:00:11-%02X", m[0], m[1], m[2], m[3], m[4], m[5], i + 1);
  j += F("{\"state\":{\"on\":");
  j += hueOn(i) ? F("true") : F("false");
  j += F(",\"bri\":");
  j += i ? 254 : alexaBri - 1;
  j += F(",\"alert\":\"none\",\"mode\":\"homeautomation\",\"reachable\":true},\"type\":\"Dimmable light\",\"name\":\"");
  j += i == 0 ? tr("Pomodoro", "Pomodoro", "Pomodoro") : i == 1 ? tr("Pausa", "Break", "Pausa") : tr("Pausa longa", "Long break", "Pausa larga");
  j += F("\",\"modelid\":\"LWB010\",\"manufacturername\":\"Philips\",\"productname\":\"E1\",\"uniqueid\":\"");
  j += id;
  j += F("\",\"swversion\":\"espalexa-2.7.0\"}");
}

// comando de uma lâmpada: ligar, desligar ou brilho (minutos)
void hueCommand(int i, const String &body) {
  bool off = body.indexOf("false") > 0, bri = body.indexOf("bri") > 0;
  int min = 0;
  if (bri) {
    int b = body.substring(body.indexOf("bri") + 5).toInt();
    b = b >= 255 ? 255 : b + 1;
    if (i == 0) alexaBri = b;
    min = b * 100 / 255;
    min = min < 1 ? 1 : min > 99 ? 99 : min;
  }
  if (i == 0) {
    if (off) {
      alexaLast = phase != IDLE && running ? 2 : 4;  // rodando: pausa; já pausado (ou na pausa): encerra
      command(alexaLast == 2 ? "pause" : "reset");
    } else if (bri) {
      startPhase(WORK, true);
      totalMs = remainingMs = min * 60000UL;
      alexaLast = 3;
    } else {
      alexaLast = 1;
      command(phase == WORK ? "start" : "focus");
    }
  } else if (off) {  // desligar uma pausa: volta ao foco
    if (phase == (i == 1 ? SHORT_BREAK : LONG_BREAK)) {
      command("focus");
      alexaLast = 7;
    }
  } else {
    startPhase(i == 1 ? SHORT_BREAK : LONG_BREAK, true);
    if (bri) totalMs = remainingMs = min * 60000UL;
    alexaLast = bri ? 8 : i == 1 ? 5 : 6;
  }
  alexaMin = min;
  previewUntil = 0;  // a tela vai direto para o timer
}

void alexaApi() {
  const String &req = server.uri(), &body = server.arg("plain");
  if (!cfg.alexa || req.indexOf("api") < 0) {
    server.send(404, "text/plain", "");
    return;
  }
  if (server.hasHeader("Origin")) {  // o Echo não manda Origin; navegador manda: é outro site tentando mexer no timer
    server.send(403, "text/plain", "");
    return;
  }
  alexaSeen = millis() | 1;  // um Echo falou com o relógio (o painel mostra)
  alexaIp = server.client().remoteIP();
  if (body.indexOf("devicetype") > 0) {  // pede um usuário da API do Hue: qualquer um serve
    server.send(200, "application/json", F("[{\"success\":{\"username\":\"2WLEDHardQrI3WHYTHoMcXHgEspsM8ZZRpSKtBQr\"}}]"));
    return;
  }
  int pos = req.indexOf("lights");
  long key = pos > 0 ? req.substring(pos + 7).toInt() : 0;
  int i = key & 127;  // índice da lâmpada na chave
  if (req.indexOf("state") > 0 && body.length()) {
    server.send(200, "application/json", F("[{\"success\":{\"/lights/1/state/\": true}}]"));
    if (i < HUE_LIGHTS) hueCommand(i, body);
    return;
  }
  String j;
  j.reserve(512);
  if (pos > 0 && key == 0) {  // todas as lâmpadas
    j = '{';
    for (int k = 0; k < HUE_LIGHTS; k++) {
      j += k ? ",\"" : "\"";
      j += hueKey(k);
      j += "\":";
      hueLight(j, k);
    }
    j += '}';
  } else if (pos > 0 && i < HUE_LIGHTS) hueLight(j, i);
  else j = "{}";
  server.send(200, "application/json", j);
}

void alexaBegin() {
  if (alexaUp || !cfg.alexa) return;
  String mac = WiFi.macAddress();
  mac.replace(":", "");
  mac.toLowerCase();
  strlcpy(hueMac, mac.c_str(), sizeof hueMac);
  alexaUp = hueUdp.beginMulticast(WiFi.localIP(), IPAddress(239, 255, 255, 250), 1900);
}

// responde às buscas por SSDP do Echo apontando para /description.xml
void alexaLoop() {
  if (!alexaUp) return;
  int n = hueUdp.parsePacket();
  if (n < 1) return;
  char p[256];
  int len = hueUdp.read(p, sizeof p - 1);
  p[len < 0 ? 0 : len] = 0;
  hueUdp.flush();
  if (!cfg.alexa || !strstr(p, "M-SEARCH") || !strstr(p, "ssdp:disc") ||
      !(strstr(p, "upnp:rootd") || strstr(p, "ssdp:all") || strstr(p, "asic:1")))
    return;
  // no máximo 4 respostas por segundo: o Echo pergunta poucas vezes, e um pedido pequeno vira uma resposta
  // ~3x maior (dava para usar o relógio para multiplicar tráfego na rede)
  static RateLimit ssdpLimit;
  if (!ssdpLimit.take(millis(), 4)) return;
  String ip = WiFi.localIP().toString(), r;
  r.reserve(400);
  r = F("HTTP/1.1 200 OK\r\nEXT:\r\nCACHE-CONTROL: max-age=100\r\nLOCATION: http://");
  r += ip;
  r += F(":80/description.xml\r\nSERVER: FreeRTOS/6.0.5, UPnP/1.0, IpBridge/1.17.0\r\nhue-bridgeid: ");
  r += hueMac;
  r += F("\r\nST: urn:schemas-upnp-org:device:basic:1\r\nUSN: uuid:2f402f80-da50-11e1-9b23-");
  r += hueMac;
  r += F("::upnp:rootdevice\r\n\r\n");
  hueUdp.beginPacket(hueUdp.remoteIP(), hueUdp.remotePort());
  hueUdp.write(r.c_str(), r.length());
  hueUdp.endPacket();
}

void sendHueDescription() {
  if (!cfg.alexa) {  // com a Alexa desligada não se anuncia (nem expõe o MAC)
    server.send(404, "text/plain", "");
    return;
  }
  String ip = WiFi.localIP().toString(), x;
  x.reserve(900);
  x = F("<?xml version=\"1.0\" ?><root xmlns=\"urn:schemas-upnp-org:device-1-0\"><specVersion><major>1</major><minor>0</minor></specVersion><URLBase>http://");
  x += ip;
  x += F(":80/</URLBase><device><deviceType>urn:schemas-upnp-org:device:Basic:1</deviceType><friendlyName>Espalexa (");
  x += ip;
  x += F(":80)</friendlyName><manufacturer>Royal Philips Electronics</manufacturer><manufacturerURL>http://www.philips.com</manufacturerURL>"
         "<modelDescription>Philips hue Personal Wireless Lighting</modelDescription><modelName>Philips hue bridge 2012</modelName>"
         "<modelNumber>929000226503</modelNumber><modelURL>http://www.meethue.com</modelURL><serialNumber>");
  x += hueMac;
  x += F("</serialNumber><UDN>uuid:2f402f80-da50-11e1-9b23-");
  x += hueMac;
  x += F("</UDN><presentationURL>index.html</presentationURL></device></root>");
  server.send(200, "text/xml", x);
}

// ---------- clima (Open-Meteo, sem chave de API) ----------
struct Weather {
  bool ok = false;
  float t = 0, tmin = 0, tmax = 0;
  int hum = 0, code = 0;
  bool day = true;
  uint16_t version = 0;
  uint8_t nd = 0;  // previsão: hoje e os próximos dias
  UiDay d[UI_DAYS];
  uint8_t rainIn = 0;  // ver UiWeather::rainIn
  char tzName[40] = "";  // fuso da cidade segundo o Open-Meteo (IANA, ex.: America/Sao_Paulo)
  int32_t utcOff = 0;     // deslocamento desse fuso agora, em segundos (o painel compara com o do relógio)
} wx;
uint32_t wxNext = 0;
uint8_t wxFails = 0;  // buscas de clima seguidas que falharam

// GET em HTTP/1.0 (resposta sem "chunked", fecha no fim) no lugar do HTTPClient (~4 KB).
// Devolve o código HTTP (negativo se não conectar) e deixa o cliente no começo do corpo.
static bool httpLine(WiFiClient &c, String &line, uint32_t start, uint16_t timeoutMs) {
  line = "";
  while (millis() - start < timeoutMs) {
    if (c.available()) {
      int ch = c.read();
      if (ch == '\n') return true;
      if (ch < 0) continue;
      if (line.length() >= 512) return false;
      line += (char)ch;
    } else if (!c.connected()) return false;
    else delay(1);
  }
  return false;
}

// Acrescenta um decimal já limitado pelo firmware sem criar outro String no heap.
static void appendFixed(String &out, float value, uint8_t digits) {
  bool neg = value < 0;
  if (neg) value = -value;
  uint32_t scale = 1;
  for (uint8_t i = 0; i < digits; i++) scale *= 10;
  uint32_t scaled = lroundf(value * scale);
  if (neg && scaled) out += '-';
  out += scaled / scale;
  if (!digits) return;
  out += '.';
  uint32_t frac = scaled % scale;
  for (uint32_t div = scale / 10; div; div /= 10) out += (char)('0' + frac / div % 10);
}

int httpGet(WiFiClient &c, const String &url, uint16_t timeoutMs) {
  if (!safeHttpUrl(url.c_str(), url.length())) return -1;
  int end = 7;
  while (end < (int)url.length() && url[end] != '/' && url[end] != '?') end++;
  String host = url.substring(7, end), path = url.substring(end);
  if (!path.length() || path[0] == '?') path = "/" + path;
  uint16_t port = 80;
  int colon = host.indexOf(':');
  if (colon >= 0) {
    port = host.substring(colon + 1).toInt();
    host.remove(colon);
  }
  c.setTimeout(timeoutMs);
  if (!host.length() || !port || !c.connect(host.c_str(), port)) return -1;
  // Envia em partes: concatenar tudo criava um String temporário grande em toda consulta.
  c.print(F("GET "));
  c.print(path);
  c.print(F(" HTTP/1.0\r\nHost: "));
  c.print(host);
  c.print(F("\r\nUser-Agent: SmallTV\r\nConnection: close\r\n\r\n"));
  uint32_t start = millis();
  String line;
  line.reserve(128);  // httpLine reutiliza a mesma capacidade para status e cabeçalhos
  if (!httpLine(c, line, start, timeoutMs)) return -2;
  int sp = line.indexOf(' ');
  int code = sp > 0 ? line.substring(sp + 1).toInt() : 0;
  if (code <= 0) return -2;
  size_t total = line.length();
  for (int headers = 0; headers < 48; headers++) {
    if (!httpLine(c, line, start, timeoutMs)) return -2;
    total += line.length();
    if (total > 8192) return -2;
    if (line.length() <= 1) return code;
  }
  return -2;
}

bool fetchWeather() {
  WiFiClient client;
  String url;
  url.reserve(384);
  url = F("http://api.open-meteo.com/v1/forecast?latitude=");
  appendFixed(url, cfg.lat, 4);
  url += F("&longitude=");
  appendFixed(url, cfg.lon, 4);
  url += F("&current=temperature_2m,relative_humidity_2m,weather_code,is_day"
           "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,sunrise,sunset"
           "&minutely_15=precipitation,precipitation_probability&forecast_minutely_15=4"
           "&timezone=auto&forecast_days=");
  url += UI_DAYS;
  if (httpGet(client, url, 5000) != 200) return false;
  JsonDocument filter;
  filter["minutely_15"]["precipitation"] = true;  // 4 quartos de hora a partir do atual: a próxima hora
  filter["minutely_15"]["precipitation_probability"] = true;
  filter["current"]["temperature_2m"] = true;
  filter["current"]["relative_humidity_2m"] = true;
  filter["current"]["weather_code"] = true;
  filter["current"]["is_day"] = true;
  filter["current"]["time"] = true;  // hora local do serviço: a segunda fonte que confirma a do SNTP
  filter["utc_offset_seconds"] = true;
  filter["timezone"] = true;  // fuso da cidade: o painel confere se bate com o do relógio
  filter["daily"]["weather_code"] = true;
  filter["daily"]["precipitation_probability_max"] = true;
  filter["daily"]["temperature_2m_max"] = true;
  filter["daily"]["temperature_2m_min"] = true;
  filter["daily"]["sunrise"] = true;
  filter["daily"]["sunset"] = true;
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, client, DeserializationOption::Filter(filter));
  client.stop();
  if (err || doc["current"]["temperature_2m"].isNull()) return false;
  wx.t = doc["current"]["temperature_2m"];
  wx.hum = doc["current"]["relative_humidity_2m"];
  wx.code = doc["current"]["weather_code"];
  wx.day = doc["current"]["is_day"] | 1;
  wx.tmax = doc["daily"]["temperature_2m_max"][0];
  wx.tmin = doc["daily"]["temperature_2m_min"][0];
  // a resposta vem em HTTP puro: limita antes de converter os decimais de /api/state
  for (float *v : {&wx.t, &wx.tmax, &wx.tmin}) *v = isfinite(*v) ? constrain(*v, -99.0f, 99.0f) : 0;
  // vai chover: o primeiro quarto de hora com chuva de verdade (≥ 0,1 mm e chance ≥ 40%), se agora
  // ainda não chove. O atual vira "em instantes" (5); os seguintes, 15/30/45 min.
  bool rainingNow = (wx.code >= 51 && wx.code <= 67) || (wx.code >= 80 && wx.code <= 82) || wx.code >= 95;
  JsonArrayConst qp = doc["minutely_15"]["precipitation"], qc = doc["minutely_15"]["precipitation_probability"];
  wx.rainIn = 0;
  for (int i = 0; !rainingNow && i < 4 && i < (int)qp.size(); i++)
    if (qp[i].as<float>() >= 0.1f && (qc[i].isNull() || qc[i].as<int>() >= 40)) {
      wx.rainIn = i ? i * 15 : 5;
      break;
    }
  JsonArrayConst dc = doc["daily"]["weather_code"], dmax = doc["daily"]["temperature_2m_max"],
                 dmin = doc["daily"]["temperature_2m_min"], drain = doc["daily"]["precipitation_probability_max"];
  wx.nd = 0;
  for (int i = 0; i < UI_DAYS && i < (int)dc.size() && i < (int)dmax.size() && i < (int)dmin.size(); i++) {
    auto deg = [](float v) { return (int8_t)constrain(lroundf(v), -99, 99); };
    wx.d[i] = {dc[i].as<uint8_t>(), deg(dmin[i].as<float>()), deg(dmax[i].as<float>()),
               drain[i].isNull() ? (uint8_t)255 : (uint8_t)constrain(drain[i].as<int>(), 0, 100)};
    wx.nd = i + 1;
  }
  // "2026-10-21T05:31" (hora local, timezone=auto) -> minutos do dia
  auto minutes = [](const char *iso) {
    const char *p = iso ? strchr(iso, 'T') : nullptr;
    return p && strlen(p) >= 6 ? (p[1] - '0') * 600 + (p[2] - '0') * 60 + (p[4] - '0') * 10 + (p[5] - '0') : -1;
  };
  sunRise = minutes(doc["daily"]["sunrise"][0] | (const char *)nullptr);
  sunSet = minutes(doc["daily"]["sunset"][0] | (const char *)nullptr);
  int64_t local = isoToEpoch(doc["current"]["time"] | (const char *)nullptr);
  if (local > 0) {  // confere a hora (veio por TCP): confirma a atual ou a que o SNTP trouxe e foi desfeita
    timeval tv;
    gettimeofday(&tv, nullptr);
    int64_t want = timeGuard.onWeather(local - doc["utc_offset_seconds"].as<int32_t>(), tv.tv_sec, millis());
    if (want != tv.tv_sec) {
      timeval w = {(time_t)want, 0};
      settimeofday(&w, nullptr);
    }
  }
  strlcpy(wx.tzName, doc["timezone"] | "", sizeof wx.tzName);
  for (char *c = wx.tzName; *c; c++)  // nome IANA ("America/Sao_Paulo"): só isso vai para o JSON do estado
    if (!isalnum((unsigned char)*c) && !strchr("/_+-", *c)) *c = 0;
  wx.utcOff = doc["utc_offset_seconds"] | 0;
  wx.ok = true;
  wx.version++;
  return true;
}

void weatherLoop() {
  if (!cfg.weatherOn || !cfg.city[0] || WiFi.status() != WL_CONNECTED) return;
  if ((int32_t)(millis() - wxNext) < 0) return;
  // a cada 15 min; se falhar, tenta de novo em 2 min
  bool ok = fetchWeather();
  wxFails = ok ? 0 : wxFails < 255 ? wxFails + 1 : 255;
  wxNext = millis() + (ok ? 15UL : 2UL) * 60000UL;
}

// O clima confirma a hora enquanto estiver configurado e respondendo; sem ele, valem duas sincronizações.
bool weatherConfirms() { return cfg.weatherOn && cfg.city[0] && wxFails < 3; }

void timeGuardBegin() {
  settimeofday_cb([](bool fromSntp) {
    if (!fromSntp) return;  // a própria correção abaixo (e a do clima) também chamam este aviso
    timeval tv;
    gettimeofday(&tv, nullptr);
    int64_t want = timeGuard.onSntp(tv.tv_sec, millis(), !weatherConfirms());
    if (want != tv.tv_sec) {  // salto não confirmado: volta à hora que vinha andando
      timeval w = {(time_t)want, 0};
      settimeofday(&w, nullptr);
    }
    if (!timeGuard.confirmed && weatherConfirms()) wxNext = millis();  // confirma já pelo clima
  });
}

// enquanto a hora não estiver confirmada, o SNTP pergunta a cada minuto (o padrão é a cada hora)
extern "C" uint32_t sntp_update_delay_MS_rfc_not_less_than_15000() { return timeGuard.confirmed ? 3600000 : 60000; }

const char *weatherText(int code);  // de ui.cpp (o mesmo texto que aparece na tela)

// ---------- tela (desenho em ui.cpp) ----------
// Ajuste anti-fantasma: a tensão comum (VCOM) ideal muda de painel para painel. Fora do ponto, sobra
// corrente contínua no cristal líquido e uma imagem parada por horas fica marcada ("fantasma"). O painel
// mostra uma estampa que tremula quando o VCOM está errado; a pessoa ajusta até ela parar e salva.
void applyVcom(uint8_t v) { tft.sendCommand(0xBB, &v, 1); }
uint32_t vcomTestUntil = 0;  // teste na tela até este millis()
uint8_t vcomTest = UI_VCOM_DEFAULT;     // valor em teste (aplicado, ainda não salvo)
int8_t vcomDraw = 0;         // 2 = desenhar a estampa toda, 1 = só o valor
// Ordem de cor, inversão e rotação variam entre unidades do SmallTV: tudo configurável no painel.
// Rolagem vertical por hardware (VSCRDEF 0x33 / VSCSAD 0x37), usada por uiScroll* em ui.cpp.
// A RAM do ST7789 tem 320 linhas e o vidro 240 mostra as 240 primeiras que o painel LÊ (linha lida n
// mostra a linha de RAM n + VSCSAD). Onde ficam na RAM as linhas que a biblioteca escreve:
//   normal (setRotation(2), MADCTL sem MY): endereço de linha = y, y = 0..239 -> RAM 0..239; a margem
//     (80 linhas sem vidro) é o endereço 240..319.
//   girado (setRotation(0), MADCTL MX|MY): endereço = y + 80, e o MY espelha a RAM (RAM = 319 - endereço),
//     então y = 0..239 -> RAM 239..0 e a margem é o endereço 0..79 (RAM 319..240).
// O MY só espelha o acesso de escrita; o VSCSAD continua em linhas lidas. Por isso, para o conteúdo subir
// na vista da pessoa, o girado precisa de VSCSAD = rows - offset (uiScrollSad em ui.h). O chip rola a
// área toda de uma vez: aqui TFA = BFA = 0 e VSA = rows (as faixas fixas não cabem, ver ui.h).
// Em qualquer dos dois, y = 0..239 do desenho continua sendo o mesmo endereço durante a rolagem.
static int16_t scrollDef = 0;  // VSA enviado por último (0 = ainda não)
static void tftScroll(int16_t rows, int16_t offset) {
  int16_t r = rows ? rows : 320;
  if (r != scrollDef) {
    uint8_t d[6] = {0, 0, (uint8_t)(r >> 8), (uint8_t)r, (uint8_t)((320 - r) >> 8), (uint8_t)(320 - r)};
    tft.sendCommand(0x33, d, 6);
    scrollDef = r;
  }
  uint16_t sad = uiScrollSad(cfg.flip, r, offset);
  uint8_t a[2] = {(uint8_t)(sad >> 8), (uint8_t)sad};
  tft.sendCommand(0x37, a, 2);
}
// Deixa preta a margem da RAM (80 linhas além do vidro). Ela nunca é desenhada depois disto: só aparece
// durante uma rolagem, e o conteúdo antigo da RAM (lixo depois de ligar) apareceria no vão.
static void tftClearMargin() {
  uint16_t a0 = cfg.flip ? 0 : 240;  // endereços de linha da margem, sem o offset da biblioteca
  uint8_t ca[4] = {0, 0, 0, 239}, ra[4] = {(uint8_t)(a0 >> 8), (uint8_t)a0, (uint8_t)((a0 + 79) >> 8), (uint8_t)(a0 + 79)};
  tft.sendCommand(ST77XX_CASET, ca, 4);
  tft.sendCommand(ST77XX_RASET, ra, 4);
  tft.startWrite();
  tft.writeCommand(ST77XX_RAMWR);
  tft.writeColor(0, 240UL * 80);
  tft.endWrite();
}
void applyPanel() {
  uiScrollEnd();
  tft.setRotation(cfg.flip ? 0 : 2);  // Adafruit: 2 = sem espelhamento/offsets 0; 0 = 180° (offset 80 já tratado)
  uint8_t madctl = (cfg.flip ? (ST77XX_MADCTL_MX | ST77XX_MADCTL_MY) : 0) | (cfg.bgr ? 0x08 : 0);
  tft.sendCommand(ST77XX_MADCTL, &madctl, 1);
  tft.invertDisplay(cfg.invert);
  tft.fillScreen(0);
  tftClearMargin();
  uiInvalidate();
}

void checkDayRollover(const struct tm &t) {
  if (t.tm_yday == cfg.todayYday) return;
  // na primeira sincronização só registra o dia, sem zerar o que já foi feito
  if (cfg.todayYday >= 0) cfg.todayCount = 0;
  cfg.todayYday = t.tm_yday;
  saveSettings();
  statsRollover(t);
}

// "14:05" para o painel web
String hhmm(const struct tm &t) {
  int h = t.tm_hour;
  if (cfg.hour12) h = h % 12 == 0 ? 12 : h % 12;
  char buf[8];
  snprintf(buf, sizeof(buf), "%02d:%02d", h, t.tm_min);
  return buf;
}

char ipText[16];

UiState makeState() {
  UiState s;
  memset(&s, 0, sizeof s);
  s.timeOk = localNow(s.now);
  s.hour12 = cfg.hour12;
  s.phase = phase;
  s.running = running;
  s.remainingMs = remainingMs;
  s.totalMs = totalMs;
  s.cycleDone = cycleDone;
  s.longEvery = cfg.longEvery;
  s.today = cfg.todayCount;
  s.dailyGoal = cfg.dailyGoal;
  s.activity = cfg.activity;
  s.streak = statsStreak;
  s.best = statsBest;
  s.weatherOn = cfg.weatherOn && cfg.city[0];
  s.wx = {wx.ok, wx.t, wx.tmin, wx.tmax, wx.hum, wx.code, wx.day, wx.version, wx.nd, {}, (uint8_t)(cfg.rainAlert ? wx.rainIn : 0)};
  memcpy(s.wx.d, wx.d, sizeof wx.d);
  s.fahr = cfg.fahr;
  if (s.fahr) {  // °F só na tela: o clima segue guardado em °C
    auto f = [](float c) { return c * 9 / 5 + 32; };
    s.wx.t = f(s.wx.t);
    s.wx.tmin = f(s.wx.tmin);
    s.wx.tmax = f(s.wx.tmax);
    for (UiDay &d : s.wx.d) {
      d.tmin = (int8_t)constrain(lroundf(f(d.tmin)), -99, 127);
      d.tmax = (int8_t)constrain(lroundf(f(d.tmax)), -99, 127);
    }
  }
  s.city = cfg.city;
  s.theme = previewActive() ? previewTheme : cfg.theme;
  s.accent = clockColor();
  strlcpy(ipText, WiFi.localIP().toString().c_str(), sizeof ipText);
  s.ip = ipText;
  s.ms = millis();
  s.celebrate = alertUntil != 0;  // fase acabou de terminar: animação de fim no Pomodoro
  return s;
}

// Tempo da última troca de tela inteira (o primeiro desenho depois de uiInvalidate ou de mudar de tela),
// para comparar 40 e 80 MHz no relógio de verdade; aparece em diag.draw.
uint16_t fullDrawMs = 0;
struct DrawProbe {
  uint32_t n0 = uiEnters, t0 = millis();
  ~DrawProbe() {
    if (uiEnters != n0) fullDrawMs = min<uint32_t>(millis() - t0, 65535);
  }
};

void renderClock() {
  DrawProbe probe;
  UiState s = makeState();
  if (s.timeOk && timeTrusted()) {  // virada do dia e histórico só com a hora confirmada
    checkDayRollover(s.now);
    statsFlush(s.now);
    s.today = cfg.todayCount;
  }
  uiRenderClock(s);
}

// Quanto a tela leva para desenhar o Pomodoro inteiro ao entrar (anel e números): o painel segura o tempo
// inicial por esse tanto, para os números dos dois começarem a descer juntos (ver "pdraw" em /api/state).
bool pomoShown = false;
uint16_t pomoDrawMs = 0;
void renderPomodoro() {
  DrawProbe probe;
  uint32_t t0 = millis();
  uint16_t seq = phaseSeq;  // a fase que este desenho mostra (o comando pode chegar durante o desenho)
  uiRenderPomodoro(makeState());
  shownSeq = seq;
  if (!pomoShown) {
    pomoShown = true;
    pomoDrawMs = millis() - t0;
  }
}

void renderAlert() {
  // pisca a tela alternando a inversão de cores
  bool flash = (millis() / 250) % 2 == 0;
  tft.invertDisplay(flash ? !cfg.invert : cfg.invert);
}

void message(const char *a, const char *b = "", const char *c = "", const char *d = "") { uiMessage(a, b, c, d); }

uint32_t colorTestUntil = 0;

// ---------- rodízio de mostradores ----------
// Lista "t:1,p:bento,..." (embutido pelo número, pacote pelo id) em /rotation.txt; a cada cfg.rotMin
// minutos, com o relógio parado (sem Pomodoro nem prévia), passa para o próximo. A troca não grava na
// EEPROM (seria uma gravação a cada troca): vale o que estiver salvo ao reiniciar.
String rotList;
uint32_t rotNext = 0;
void rotSet(const String &v);
void rotLoop();

// ---------- álbum: fotos (JPG) e GIFs no LittleFS ----------
bool fsOk = false;
void sendStats();
void setupUpdate();

// ---------- histórico de focos (/stats.bin) ----------
// Um contador por dia num anel de 371 dias (53 semanas): dá a sequência de dias com a meta batida,
// o recorde e o jardim do painel. Só o cabeçalho fica na RAM; os dias são lidos do arquivo.
struct StatsHead { uint32_t magic; uint16_t lastDay, best; uint32_t total; };
const uint32_t STATS_MAGIC = 0x544F4D31;  // "TOM1"
const uint16_t STATS_DAYS = 371;
StatsHead stats = {0, 0, 0, 0};
uint16_t statsStreak = 0, statsBest = 0;
bool statsWritePending = false;
uint16_t statsPendingDay = 0;
uint8_t statsPendingCount = 0;
bool fsWriterBusy();
void statsFlush(void);

// dias desde 1970-01-01 para a data local (algoritmo "days from civil")
uint16_t dayNumber(const struct tm &t) {
  int y = t.tm_year + 1900, m = t.tm_mon + 1;
  y -= m <= 2;
  int era = y / 400, yoe = y - era * 400, doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + t.tm_mday - 1;
  return era * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468;
}

// lê os 371 dias em ordem (o mais antigo primeiro); false sem arquivo
bool statsRead(uint8_t *days) {
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
void statsWrite(uint16_t day, uint8_t count) {
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
  j.reserve(STATS_DAYS * 3 + 96);
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
  }
  j += "]}";
  server.send(200, "application/json", j);
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
std::vector<String> album;
size_t albumIdx = 0;
bool inAlbum = false;
uint32_t viewUntil = 0;
String curFile;
AnimatedGIF *gif = nullptr;
File mediaFile;
int gifOffX = 0, gifOffY = 0;
uint32_t nextFrameAt = 0;

bool endsWithIgnoreCase(const String &s, const char *suffix) {
  size_t n = s.length(), m = strlen(suffix);
  if (m > n) return false;
  for (size_t i = 0; i < m; i++) {
    char a = s[n - m + i];
    if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
    if (a != suffix[i]) return false;
  }
  return true;
}
bool isGif(const String &n) { return endsWithIgnoreCase(n, ".gif"); }
bool isJpg(const String &n) { return endsWithIgnoreCase(n, ".jpg") || endsWithIgnoreCase(n, ".jpeg"); }

// Pastas usadas pelo firmware original: /image (fotos e GIFs) e /gif.
const char *MEDIA_DIRS[] = {"/image", "/gif"};

uint32_t fsInfoAt = 0;  // quando o espaço livre foi medido para o painel (0 = medir de novo)

void rebuildAlbum() {
  fsInfoAt = 0;  // fotos enviadas, apagadas ou formatação: o espaço livre mudou
  album.clear();
  if (!fsOk) return;
  for (const char *dir : MEDIA_DIRS) {
    Dir d = LittleFS.openDir(dir);
    while (d.next()) {
      if (d.isDirectory()) continue;
      String n = String(dir) + "/" + d.fileName();
      // a animação de boot do firmware original não entra no álbum
      if ((isJpg(n) || isGif(n)) && d.fileName() != "boot.gif" && d.fileName() != "boot.jpg") album.push_back(n);
    }
  }
}

// Callbacks de arquivo compartilhados pelos decodificadores de GIF e JPEG (mesmo autor, mesma API).
void *mediaOpen(const char *fn, int32_t *size) {
  mediaFile = LittleFS.open(fn, "r");
  if (!mediaFile) return nullptr;
  *size = mediaFile.size();
  return &mediaFile;
}
void mediaClose(void *) {
  if (mediaFile) mediaFile.close();
}

int32_t jpgRead(JPEGFILE *f, uint8_t *buf, int32_t len) {
  yield();
  File *fl = (File *)f->fHandle;
  int32_t n = len;
  if (f->iSize - f->iPos < len) n = f->iSize - f->iPos;
  if (n <= 0) return 0;
  n = fl->read(buf, n);
  f->iPos = fl->position();
  return n;
}
int32_t jpgSeek(JPEGFILE *f, int32_t pos) {
  File *fl = (File *)f->fHandle;
  if (pos < 0 || pos > f->iSize || !fl->seek(pos)) return -1;
  f->iPos = fl->position();
  return f->iPos;
}
int jpgDraw(JPEGDRAW *p) {
  if (p->y >= 240) return 0;
  if (!uiClip.on) {
    tftBitmap(p->x, p->y, p->pPixels, p->iWidth, p->iHeight);
    return 1;
  }
  blit(p->x, p->y, p->pPixels, p->iWidth, p->iHeight);  // recorta para a área suja; fora da tela, cai na faixa
  return 1;
}

const int MAX_JPEG_DIM = 1920;  // escala máxima 1/8: nunca entrega mais de 240 px ao display
bool jpgDimensionsOk(JPEGDEC *jpg) {
  return jpg->getWidth() > 0 && jpg->getHeight() > 0 && jpg->getWidth() <= MAX_JPEG_DIM && jpg->getHeight() <= MAX_JPEG_DIM;
}
int jpgCheckDraw(JPEGDRAW *) {
  yield();
  return 1;
}
bool validJpg(const String &path) {
  JPEGDEC *jpg = new (std::nothrow) JPEGDEC();
  if (!jpg) return false;
  bool opened = jpg->open(path.c_str(), mediaOpen, mediaClose, jpgRead, jpgSeek, jpgCheckDraw);
  bool ok = false;
  if (opened && jpgDimensionsOk(jpg)) {
    int sc = 1;
    while (sc < 8 && (jpg->getWidth() / sc > 240 || jpg->getHeight() / sc > 240)) sc *= 2;
    int opt = sc == 2 ? JPEG_SCALE_HALF : sc == 4 ? JPEG_SCALE_QUARTER : sc == 8 ? JPEG_SCALE_EIGHTH : 0;
    jpg->setPixelType(RGB565_LITTLE_ENDIAN);
    ok = jpg->decode(0, 0, opt);
  }
  jpg->close();
  delete jpg;
  return ok;
}

// Camada "image" dos pacotes: JPEG em /faces/<arquivo>, no ponto (x, y), sem escala.
bool drawJpgAt(const char *file, int x, int y) {
  String path = String("/faces/") + file;
  if (strstr(file, "..") || !LittleFS.exists(path)) return false;
  JPEGDEC *jpg = new (std::nothrow) JPEGDEC();
  if (!jpg) return false;
  bool ok = false;
  if (jpg->open(path.c_str(), mediaOpen, mediaClose, jpgRead, jpgSeek, jpgDraw) && jpgDimensionsOk(jpg)) {
    jpg->setPixelType(RGB565_LITTLE_ENDIAN);
    ok = jpg->decode(x, y, 0);
  }
  jpg->close();
  delete jpg;
  return ok;
}

bool drawJpg(const String &f) {
  JPEGDEC *jpg = new (std::nothrow) JPEGDEC();  // ~18 KB: só existe durante a decodificação
  if (!jpg) return false;
  bool ok = false;
  if (jpg->open(f.c_str(), mediaOpen, mediaClose, jpgRead, jpgSeek, jpgDraw) && jpgDimensionsOk(jpg)) {
    int w = jpg->getWidth(), h = jpg->getHeight(), sc = 1;
    while (sc < 8 && (w / sc > 240 || h / sc > 240)) sc *= 2;
    int opt = sc == 2 ? JPEG_SCALE_HALF : sc == 4 ? JPEG_SCALE_QUARTER : sc == 8 ? JPEG_SCALE_EIGHTH : 0;
    jpg->setPixelType(RGB565_LITTLE_ENDIAN);
    ok = jpg->decode(max(0, (240 - w / sc) / 2), max(0, (240 - h / sc) / 2), opt);
  }
  jpg->close();
  delete jpg;
  return ok;
}
int32_t gifRead(GIFFILE *f, uint8_t *buf, int32_t len) {
  yield();  // inclusive arquivos que gastam muito tempo em códigos LZW sem produzir pixels
  File *fl = (File *)f->fHandle;
  int32_t n = len;
  if (f->iSize - f->iPos < len) n = f->iSize - f->iPos;
  if (n <= 0) return 0;
  n = fl->read(buf, n);
  f->iPos = fl->position();
  return n;
}
int32_t gifSeek(GIFFILE *f, int32_t pos) {
  File *fl = (File *)f->fHandle;
  fl->seek(pos);
  f->iPos = fl->position();
  return f->iPos;
}

static int gifCheckRead(void *ctx, uint8_t *buf, int n) {
  yield();  // a conferência completa de um arquivo grande não pode disparar o watchdog
  return ((File *)ctx)->read(buf, n);
}

static bool validGif(const String &path) {
  File file = LittleFS.open(path, "r");
  if (!file) return false;
  const char *error = gifCheck(gifCheckRead, &file, 480, 480);
  file.close();
  return error == nullptr;
}

void gifDraw(GIFDRAW *p) {
  static uint16_t buf[240];
  int y = p->iY + p->y + gifOffY;
  if (y < 0 || y >= 240) return;
  int x0 = p->iX + gifOffX;
  int w = p->iWidth;
  if (x0 + w > 240) w = 240 - x0;
  if (w <= 0) return;
  uint8_t *s = p->pPixels;
  uint16_t *pal = p->pPalette;
  if (p->ucDisposalMethod == 2) {  // restaurar fundo
    for (int i = 0; i < w; i++)
      if (s[i] == p->ucTransparent) s[i] = p->ucBackground;
    p->ucHasTransparency = 0;
  }
  tft.startWrite();
  if (p->ucHasTransparency) {
    uint8_t tr = p->ucTransparent;
    int x = 0;
    while (x < w) {
      while (x < w && s[x] == tr) x++;
      int start = x, n = 0;
      while (x < w && s[x] != tr) buf[n++] = pal[s[x++]];
      if (n) {
        tft.setAddrWindow(x0 + start, y, n, 1);
        tftPixels(buf, n);
      }
    }
  } else {
    for (int i = 0; i < w; i++) buf[i] = pal[s[i]];
    tft.setAddrWindow(x0, y, w, 1);
    tftPixels(buf, w);
  }
  tft.endWrite();
}

void stopAlbum() {
  if (gif) {
    gif->close();
    delete gif;
    gif = nullptr;
  }
  inAlbum = false;
  curFile = "";
  uiInvalidate();  // o relógio redesenha tudo ao voltar
}

bool showAlbumItem(const String &f) {
  stopAlbum();
  if (!LittleFS.exists(f)) return false;
  tft.fillScreen(0);
  uiInvalidate();
  inAlbum = true;
  curFile = f;
  if (isGif(f)) {
    if (!validGif(f)) {
      uiError(tr("GIF fora do padrao", "Unsupported GIF", "GIF fuera de norma"));
      return false;
    }
    gif = new (std::nothrow) AnimatedGIF();  // ~24 KB contíguos de RAM
    if (!gif) {
      uiError(tr("Sem memoria p/ GIF", "No memory for GIF", "Sin memoria p/ GIF"));
      return true;  // mantém a tela de aviso até o próximo item
    }
    gif->begin(GIF_PALETTE_RGB565_LE);
    if (!gif->open(f.c_str(), mediaOpen, mediaClose, gifRead, gifSeek, gifDraw)) {
      gif->close();
      delete gif;
      gif = nullptr;
      uiError(tr("GIF invalido", "Invalid GIF", "GIF invalido"));
      return false;
    }
    gifOffX = max(0, (240 - gif->getCanvasWidth()) / 2);
    gifOffY = max(0, (240 - gif->getCanvasHeight()) / 2);
    nextFrameAt = 0;
  } else {
    if (!drawJpg(f)) uiError(tr("JPG invalido", "Invalid JPG", "JPG invalido"));
  }
  return true;
}

void albumLoop() {
  if (!gif || (int32_t)(millis() - nextFrameAt) < 0) return;
  int delayMs = 0;
  int r = gif->playFrame(false, &delayMs);
  if (r < 0) {  // erro: desiste deste GIF
    stopAlbum();
    return;
  }
  if (r == 0) gif->reset();  // último quadro: recomeça
  nextFrameAt = millis() + max(delayMs, 20);
}

// Decide o que mostrar quando não há Pomodoro: relógio ou um item do álbum.
void scheduleView() {
  uint32_t now = millis();
  if (viewUntil && (int32_t)(now - viewUntil) < 0) return;
  bool wantAlbum = !album.empty() && (cfg.mode == 1 || (cfg.mode == 2 && !inAlbum));
  if (wantAlbum) {
    const String &next = album[albumIdx++ % album.size()];
    // com um item só, não redesenha a mesma foto (evita piscar e reiniciar o GIF)
    if (!(inAlbum && next == curFile)) showAlbumItem(next);
  } else if (inAlbum) {
    stopAlbum();
  }
  viewUntil = now + cfg.albumDelay * 1000UL;
  if (!viewUntil) viewUntil = 1;
}

// ---------- pacotes de mostrador (/faces/<id>.json) ----------
bool idOk(const String &id) {
  if (!id.length() || id.length() > 23) return false;
  for (char c : id)
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
  return true;
}

// Lê o arquivo e carrega no motor. Em erro, msg recebe o motivo.
bool loadPackFile(const String &path, String &msg) {
  File f = fsOk ? LittleFS.open(path, "r") : File();
  if (!f) {
    msg = "arquivo nao encontrado";
    return false;
  }
  size_t n = f.size();
  if (n == 0 || n > 16384) {
    f.close();
    msg = "pacote vazio ou maior que 16 KB";
    return false;
  }
  char err[96];
  bool ok = facePackLoadFile(f, err, sizeof err);  // em passadas: o JSON não cabe inteiro na RAM
  f.close();
  if (!ok) msg = err;
  uiInvalidate();
  return ok;
}

bool loadPack(const String &id) {
  String msg;
  return idOk(id) && loadPackFile("/faces/" + id + ".json", msg);
}

// Volta o motor ao pacote salvo nas configurações (ou o libera, se o ativo é um embutido).
void restoreActivePack() {
  if (cfg.theme == THEME_PACK && loadPack(cfg.facePkg)) return;
  facePackUnload();
  if (cfg.theme == THEME_PACK) cfg.theme = THEME_MATRIX;
  uiInvalidate();
}

File packUpload;
String packUploadError;
UploadGate packGate;
bool packFaceReady = false;

// Recebe "face" (o .json) e, opcional, "preview" (o .png) em arquivos temporários.
void handlePackUpload() {
  HTTPUpload &up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    if (packUpload) packUpload.close();
    packGate.begin(false, 0);
    if (up.name == "face") { packUploadError = ""; packFaceReady = false; }
    if (!panelOk() || !uploadAuthed()) {
      packUploadError = "Proibido";
      return;
    }
    if (up.name != "face" && up.name != "preview") { packUploadError = "Campo invalido"; return; }
    if (packUploadError.length()) return;
    if (!fsOk) {
      packUploadError = "Sistema de arquivos indisponivel";
      return;
    }
    if (!LittleFS.exists("/faces")) LittleFS.mkdir("/faces");
    if (up.name == "face") { LittleFS.remove("/faces/_novo.json"); LittleFS.remove("/faces/_novo.png"); }
    packUpload = LittleFS.open(up.name == "preview" ? "/faces/_novo.png" : "/faces/_novo.json", "w");
    if (!packUpload) { packUploadError = "Falha ao criar arquivo"; return; }
    packGate.begin(true, up.name == "face" ? 16384 : 65536);
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (!packGate.active) return;
    if (!packGate.take(up.currentSize)) packUploadError = "Arquivo grande demais";
    else if (packUpload.write(up.buf, up.currentSize) != up.currentSize) packUploadError = "Memoria cheia";
    if (packUploadError.length()) { packUpload.close(); packGate.stop(); }
  } else if (up.status == UPLOAD_FILE_END || up.status == UPLOAD_FILE_ABORTED) {
    if (packUpload) packUpload.close();
    if (up.status == UPLOAD_FILE_ABORTED) packUploadError = "Envio interrompido";
    if (packGate.active && !packUploadError.length() && up.name == "face") packFaceReady = true;
    packGate.stop();
    if (packUploadError.length()) packFaceReady = false;
  }
}

// ---------- web ----------
void jsonEscapeAppend(String &o, const char *s) {
  for (; *s; s++) {
    char c = *s;
    if (c == '"' || c == '\\') o += '\\';
    if ((uint8_t)c >= 0x20) o += c;
  }
}

bool safeText(const String &s) { return s.indexOf('"') < 0 && s.indexOf('\\') < 0; }

// Deslocamento do fuso do relógio agora, em segundos (hora local - UTC), pela diferença entre localtime e gmtime.
int32_t localOffset() {
  time_t now = time(nullptr);
  struct tm lt, gt;
  localtime_r(&now, &lt);
  gmtime_r(&now, &gt);
  int32_t d = (lt.tm_hour - gt.tm_hour) * 3600 + (lt.tm_min - gt.tm_min) * 60;
  int dd = lt.tm_yday - gt.tm_yday;  // virada de dia (ou de ano) entre os dois
  if (dd == 1 || dd < -1) d += 86400;
  else if (dd == -1 || dd > 1) d -= 86400;
  return d;
}

// ---------- diagnóstico (painel: Ajustes -> Sistema) ----------
// Motivo do último reinício (e, se foi travamento, a causa e o endereço), sinal do Wi-Fi, menor memória livre
// desde que ligou e tempo ligado: se o relógio reiniciar sozinho, dá para ver o porquê em vez de adivinhar.
uint32_t diagMinHeap = 0xFFFFFFFF, diagLastMs = 0;
uint16_t diagWraps = 0;  // millis() volta a zero a cada ~49,7 dias
void diagLoop() {
  uint32_t now = millis(), h = ESP.getFreeHeap();
  if (now < diagLastMs) diagWraps++;
  diagLastMs = now;
  if (h < diagMinHeap) diagMinHeap = h;
}
void diagJson(String &j) {
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
  j += ESP.getHeapFragmentation();
  j += F(",\"up\":");
  j += (uint32_t)((((uint64_t)diagWraps << 32) | millis()) / 1000);
  j += F(",\"draw\":");  // ms do último desenho que trocou a tela inteira (0 = ainda nenhum)
  j += fullDrawMs;
  j += '}';
}

// "crash" só existe depois de uma queda: n = quantas, r = motivo (como diag.rst), x/epc/va = causa e endereços
void crashJson(String &j) {
  if (!crashRec.magic) return;
  char b[112];
  snprintf(b, sizeof b, ",\"crash\":{\"n\":%u,\"r\":%u,\"x\":%u,\"epc\":\"%08x\",\"va\":\"%08x\",\"up\":%u,\"ver\":\"%.7s\"}",
           crashRec.n, crashRec.reason, crashRec.exc, (unsigned)crashRec.epc, (unsigned)crashRec.vaddr, (unsigned)crashRec.up, crashRec.ver);
  j += b;
}

void sendState() {
  if (!readOk()) return;
  struct tm t;
  String clock = localNow(t) ? hhmm(t) : "--:--";
  char color[8];
  snprintf(color, sizeof(color), "#%02x%02x%02x", cfg.colR, cfg.colG, cfg.colB);
  // LittleFS.info() percorre o sistema de arquivos inteiro (~100 ms com 1,5 MB de fotos) e o painel
  // pede o estado a cada segundo: isso travava os GIFs. Guarda o resultado; recalcula quando os
  // arquivos mudam (fsInfoAt = 0) ou depois de 1 minuto.
  static FSInfo fi = {};
  if (fsOk && (!fsInfoAt || millis() - fsInfoAt > 60000)) {
    LittleFS.info(fi);
    fsInfoAt = millis() | 1;
  }
  String j;
  j.reserve(1150 + rotList.length());  // estado, ajustes e diagnóstico: uma alocação só
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
  j += F(",\"used\":");
  j += (uint32_t)fi.usedBytes;
  j += F(",\"total\":");
  j += (uint32_t)fi.totalBytes;
  j += F("},\"cfg\":{\"work\":");
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
  j += F(",\"alexa\":");
  j += cfg.alexa;
  j += F(",\"rotmin\":");
  j += cfg.rotMin;
  j += F(",\"rot\":\"");
  j += rotList;
  j += F("\"},\"alexa\":{\"ago\":");  // segundos desde o último pedido de um Echo (-1 = nenhum)
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
  j += F("\",\"heap\":");
  j += ESP.getFreeHeap();
  j += F(",\"blk\":");
  j += ESP.getMaxFreeBlockSize();
  j += F(",\"pdraw\":");  // ms que a tela levou para desenhar o Pomodoro (0 = ainda não mediu)
  j += pomoDrawMs;
  j += F(",\"pseq\":");  // fase atual e a última que a tela já mostrou com os números (iguais = na tela)
  j += phaseSeq;
  j += F(",\"pshown\":");
  j += shownSeq;
  diagJson(j);
  crashJson(j);
  j += F(",\"ver\":\"" FW_VERSION "\"}");
  server.sendHeader("Cache-Control", "no-store");  // cidade, rede e ajustes: o navegador não guarda
  server.send(200, "application/json", j);
}

// "-23.5478" -> -23.5478. Lê só o que o painel manda (latitude/longitude), sem a leitura de decimais da newlib.
float parseDec(const String &s) {
  const char *p = s.c_str();
  bool neg = *p == '-';
  if (*p == '-' || *p == '+') p++;
  float v = 0, k = 1;
  for (; *p >= '0' && *p <= '9'; p++) v = v * 10 + (*p - '0');
  if (*p == '.')
    for (p++; *p >= '0' && *p <= '9'; p++) v += (*p - '0') * (k /= 10);
  return neg ? -v : v;
}

uint8_t argRange(const char *name, uint8_t cur, int lo, int hi) {
  if (!server.hasArg(name) || server.arg(name).length() == 0) return cur;
  return constrain(server.arg(name).toInt(), lo, hi);
}

// Caminho de mídia válido: só dentro de /image ou /gif, sem "..".
bool mediaPath(const String &p) {
  return (p.startsWith("/image/") || p.startsWith("/gif/")) && p.indexOf("..") < 0;
}

File uploadFile;
String uploadPath, uploadError;
UploadGate photoGate;

void handleUpload() {
  HTTPUpload &up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    if (uploadFile) uploadFile.close();
    photoGate.begin(false, 0);
    uploadError = "";
    uploadPath = "";
    if (!panelOk() || !uploadAuthed()) {
      uploadError = "Proibido";
      return;
    }
    if (!fsOk) {
      uploadError = "Sistema de arquivos indisponivel";
      return;
    }
    // nome seguro: letras, números, - _ . e no máximo 28 caracteres (limite do LittleFS é 31)
    String name;
    for (char c : up.filename)
      if (isalnum(c) || c == '-' || c == '_' || c == '.') name += c;
    while (name.indexOf("..") >= 0) name.replace("..", ".");  // mediaPath recusa "..": o arquivo não poderia ser apagado
    if (name.length() > 28) name = name.substring(name.length() - 28);
    if (!isJpg(name) && !isGif(name)) {
      uploadError = "Use JPG ou GIF";
      return;
    }
    FSInfo fi;
    LittleFS.info(fi);
    if (fi.totalBytes - fi.usedBytes < 16384) {
      uploadError = "Memoria cheia";
      return;
    }
    if (!LittleFS.exists("/image")) LittleFS.mkdir("/image");
    uploadPath = "/image/" + name;
    uploadFile = LittleFS.open(uploadPath, "w");
    if (!uploadFile) uploadError = "Falha ao criar arquivo";
    else photoGate.begin(true, 3 * 1024 * 1024);
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (!photoGate.active) return;
    if (!photoGate.take(up.currentSize)) uploadError = "Arquivo grande demais";
    else if (uploadFile.write(up.buf, up.currentSize) != up.currentSize) uploadError = "Memoria cheia";
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
    } else if (up.status == UPLOAD_FILE_END && !uploadError.length() && isJpg(uploadPath) && !validJpg(uploadPath)) {
      LittleFS.remove(uploadPath);
      uploadError = tr("JPG fora do padrao", "Unsupported JPG", "JPG fuera de norma");
    }
    rebuildAlbum();
  }
}

void sendFiles() {
  if (!readOk()) return;
  size_t cap = 16;
  for (const String &n : album) cap += n.length() * 2 + 32;
  String j;
  j.reserve(cap);  // uma alocação, em vez de crescer a resposta a cada arquivo
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
  }
  j += "]}";
  server.send(200, "application/json", j);
}

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
  server.send(403, "text/plain", "Proibido");
  return false;
}
// Senha do painel (a mesma do /update, usuário admin): com ela definida, todo o painel e a API pedem
// login HTTP (o navegador pergunta uma vez e lembra). Erros usam espera progressiva por IP sem bloquear
// o loop do relógio. A API da Alexa e o modo de recuperação não pedem senha.
// O login Basic é conferido aqui: o authenticate() do servidor traz junto o modo Digest e o MD5 (~2,5 KB).
bool passOk() {
  return !cfg.otaPass[0] || server.header("Authorization") == "Basic " + base64::encode(String("admin:") + cfg.otaPass, false);
}
void askLogin() {
  server.sendHeader(F("WWW-Authenticate"), F("Basic realm=\"SmallTV\""));
  server.send(401, "text/plain", "");
}
AuthThrottle authThrottle;
uint32_t requestIp() {
  IPAddress ip = server.client().remoteIP();
  return ((uint32_t)ip[0] << 24) | ((uint32_t)ip[1] << 16) | ((uint32_t)ip[2] << 8) | ip[3];
}
bool authBlocked(uint32_t ip, bool reply) {
  uint32_t wait = 0;
  if (!cfg.otaPass[0] || !authThrottle.blocked(ip, millis(), &wait)) return false;
  if (reply) {
    server.sendHeader("Retry-After", String((wait + 999) / 1000));
    server.send(429, "text/plain", "Tente novamente em alguns segundos");
  }
  return true;
}
bool authCheck(bool reply) {
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
UploadGate panelGate;
br_sha256_context panelSha;
String panelResult;

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

void handlePanelUpload() {
  HTTPUpload &up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    if (panelUpload) panelUpload.close();
    panelGate.begin(false, 0);
    panelResult = "";
    if (!panelOk() || !uploadAuthed() || !fsOk) return;
    if (!LittleFS.exists("/www")) LittleFS.mkdir("/www");
    panelUpload = LittleFS.open("/www/_novo", "w");
    if (!panelUpload) return;
    br_sha256_init(&panelSha);
    panelGate.begin(true, 65536);
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (!panelGate.active) return;
    if (!panelGate.take(up.currentSize) || panelUpload.write(up.buf, up.currentSize) != up.currentSize) {
      panelGate.stop();
      panelUpload.close();
      LittleFS.remove("/www/_novo");
      return;
    }
    br_sha256_update(&panelSha, up.buf, up.currentSize);
  } else if (up.status == UPLOAD_FILE_END || up.status == UPLOAD_FILE_ABORTED) {
    bool ok = panelGate.active && up.status == UPLOAD_FILE_END && panelGate.bytes > 18;
    panelGate.stop();
    if (panelUpload) panelUpload.close();
    if (!ok) {
      LittleFS.remove("/www/_novo");
      return;
    }
    uint8_t d[32];
    br_sha256_out(&panelSha, d);
    char hex[25];
    for (int i = 0; i < 12; i++) snprintf(hex + i * 2, 3, "%02x", d[i]);
    String name = String("/www/") + hex + ".gz";
    LittleFS.remove(name);
    LittleFS.rename("/www/_novo", name);
    // guarda só o desta versão e o recém-chegado (o da próxima, durante uma atualização)
    Dir dir = LittleFS.openDir("/www");
    std::vector<String> old;
    while (dir.next())
      if (dir.fileName() != String(hex) + ".gz" && dir.fileName() != PANEL_HASH ".gz") old.push_back(dir.fileName());
    for (const String &n : old) LittleFS.remove("/www/" + n);
    panelResult = hex;
  }
}

void setupWeb() {
  server.collectHeaders("X-SmallTV", "Content-Length", "Authorization", "Origin", "X-Firmware-Sig");
  // painel: o desta versão na memória de arquivos ou, sem ele, o carregador que o instala
  server.on("/", HTTP_GET, [] {
    if (!readOk()) return;  // com senha, o navegador pede o login já ao abrir o painel
    server.sendHeader("Cache-Control", "no-cache");  // depois de atualizar o firmware, o navegador busca o painel novo
    File f = fsOk ? LittleFS.open("/www/" PANEL_HASH ".gz", "r") : File();
    if (f) {
      server.streamFile(f, "text/html");  // .gz: o servidor manda "Content-Encoding: gzip"
      f.close();
      return;
    }
    server.sendHeader("Content-Encoding", "gzip");
    server.send_P(200, "text/html", (PGM_P)LOADER_HTML_GZ, sizeof(LOADER_HTML_GZ));
  });
  server.on("/api/panel", HTTP_POST, [] {
    if (!guard()) return;
    if (!panelResult.length()) server.send(400, "text/plain", "Painel invalido");
    else server.send(200, "text/plain", panelResult == PANEL_HASH ? "ok" : "guardado " + panelResult);
  }, handlePanelUpload);
  server.on("/api/state", HTTP_GET, sendState);
  server.on("/api/stats", HTTP_GET, sendStats);
  server.on("/api/crash", HTTP_POST, [] {  // "Limpar" do painel
    if (!guard()) return;
    crashRec.magic = 0;
    EEPROM.put(CRASH_OFF, crashRec);
    EEPROM.commit();
    server.send(200, "text/plain", "ok");
  });
  // avisos de fim de fase: GET lê, POST (w, b) grava, POST test=w|b chama o endereço e devolve o código
  server.on("/api/hooks", HTTP_GET, [] {
    if (!guard()) return;  // os links levam o código da conta do serviço
    server.sendHeader("Cache-Control", "no-store");
    String j;
    j.reserve((hookWork.length() + hookBreak.length()) * 2 + 20);
    j = F("{\"w\":\"");
    jsonEscapeAppend(j, hookWork.c_str());
    j += F("\",\"b\":\"");
    jsonEscapeAppend(j, hookBreak.c_str());
    j += F("\"}");
    server.send(200, "application/json", j);
  });
  server.on("/api/hooks", HTTP_POST, [] {
    if (!guard()) return;
    if (server.hasArg("test")) {
      int code = hookFire(server.arg("test") == "b" ? hookBreak : hookWork);
      server.send(200, "text/plain", String(code));
      return;
    }
    String work = hookClean(server.arg("w")), pause = hookClean(server.arg("b"));
    String rawWork = server.arg("w"), rawPause = server.arg("b");
    rawWork.trim(); rawPause.trim();
    if ((rawWork.length() && !work.length()) || (rawPause.length() && !pause.length())) {
      server.send(400, "text/plain", "Use um endereco http:// valido (sem espacos ou controles)");
      return;
    }
    hookWork = work;
    hookBreak = pause;
    File f = fsOk ? LittleFS.open("/hooks.txt", "w") : File();
    if (f) {
      f.print(hookWork + "\n" + hookBreak + "\n");
      f.close();
    }
    server.send(f ? 200 : 500, "text/plain", f ? "ok" : "falhou");
  });
  // restaura o histórico de um backup: "days" = contagens do mais antigo ao dia "day"
  server.on("/api/statsimport", HTTP_POST, [] {
    if (!guard()) return;
    if (!fsOk || !server.hasArg("days") || !server.hasArg("day")) {
      server.send(400, "text/plain", "Backup invalido");
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
      if (v.size() >= STATS_DAYS) { server.send(400, "text/plain", "Backup invalido"); return; }
      v.push_back(n < 0 ? 0 : n > 255 ? 255 : n);
      i = e + 1;
    }
    if (v.empty() || v.size() > STATS_DAYS) {
      server.send(400, "text/plain", "Backup invalido");
      return;
    }
    stats = {STATS_MAGIC, day, (uint16_t)server.arg("best").toInt(), (uint32_t)server.arg("total").toInt()};
    File fh = LittleFS.open("/stats.bin", "w");
    if (fh) {
      uint8_t ring[STATS_DAYS] = {0};
      for (size_t i = 0; i < v.size(); i++) ring[(day - (v.size() - 1) + i) % STATS_DAYS] = v[i];
      fh.write((const uint8_t *)&stats, sizeof stats);
      fh.write(ring, STATS_DAYS);
      fh.close();
    }
    statsCompute();
    server.send(fh ? 200 : 500, "text/plain", fh ? "ok" : "falhou");
  });
  // lista dos mostradores para a galeria do painel (vem de faces_list.h)
  server.on("/api/faces", HTTP_GET, [] {
    if (!readOk()) return;
    String j;
    j.reserve(1800);
    j = '[';
    for (uint8_t t = 0; t < THEME_COUNT; t++) {
      if (t) j += ',';
      j += F("{\"n\":\"");
      jsonEscapeAppend(j, uiFaceName(t));
      j += F("\",\"d\":\"");
      jsonEscapeAppend(j, uiFaceDesc(t));
      j += F("\"}");
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
      }
    }
    j += "]";
    server.send(200, "application/json", j);
  });
  // prévia PNG de um mostrador para a galeria do painel. As dos embutidos ficam no site (geradas pelo
  // simulador, sem ocupar a flash do relógio); as dos pacotes instalados, no LittleFS.
  server.on("/face", HTTP_GET, [] {
    if (!readOk()) return;
    if (server.hasArg("pkg")) {  // prévia de pacote instalado (/faces/<id>.png, se veio junto)
      const String &id = server.arg("pkg");
      File f = fsOk && idOk(id) ? LittleFS.open("/faces/" + id + ".png", "r") : File();
      if (!f) {
        server.send(404, "text/plain", "");
        return;
      }
      server.sendHeader("Cache-Control", "max-age=300");
      server.streamFile(f, "image/png");
      f.close();
      return;
    }
    int t = server.arg("t").toInt();
    if (t < 0 || t >= THEME_COUNT) {
      server.send(404, "text/plain", "");
      return;
    }
    server.sendHeader("Location", String(F("https://davicorrea0.github.io/smalltv-pomodoro/img/faces/face-")) + t + ".png");
    server.send(302, "text/plain", "");
  });
  // anti-fantasma: v=0..63 aplica e mostra a estampa (3 min); save=1 guarda; stop=1 volta ao salvo
  server.on("/api/vcom", HTTP_POST, [] {
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
    server.send(200, "application/json", String(F("{\"vcom\":")) + cfg.vcom + "}");
  });
  server.on("/api/cmd", HTTP_POST, [] {
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
  server.on("/api/settings", HTTP_POST, [] {
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
      wxNext = millis();  // busca o clima já
    }
    saveSettings();
    updateBrightness(true);
    if (panelChanged) applyPanel();
    // Brilho, horário noturno e opções que não aparecem na tela: sem apagar e redesenhar tudo, que piscaria
    // a cada movimento do controle (junto com a rampa do brilho)
    static const char *const QUIET[] = {"bri", "non", "nstart", "nend", "nbri", "nsun", "beta", "alexa", "rotmin"};
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
  server.on("/api/files", HTTP_GET, sendFiles);
  // instala um pacote de mostrador: valida no motor, usa o "id" do JSON como nome do arquivo
  server.on("/api/faceinstall", HTTP_POST, [] {
    const bool ready = packFaceReady;
    packFaceReady = false;
    if (!guard()) return;
    String msg;
    bool ok = ready && !packUploadError.length() && loadPackFile("/faces/_novo.json", msg);
    String id = ok ? facePackId() : "", name = ok ? facePackName() : "";
    if (ok && id[0] == '_') {  // "_novo" e afins são os temporários do envio
      ok = false;
      msg = "id invalido";
    }
    if (ok) {
      LittleFS.remove("/faces/" + id + ".json");
      LittleFS.rename("/faces/_novo.json", "/faces/" + id + ".json");
      LittleFS.remove("/faces/" + id + ".png");
      if (LittleFS.exists("/faces/_novo.png")) LittleFS.rename("/faces/_novo.png", "/faces/" + id + ".png");
      File t = LittleFS.open("/faces/" + id + ".txt", "w");  // metadados para a lista do painel
      if (t) {
        t.printf("%s\n%s\n%s\n", facePackName(), facePackDesc(), facePackAuthor());
        t.close();
      }
    } else {
      LittleFS.remove("/faces/_novo.json");
      LittleFS.remove("/faces/_novo.png");
    }
    restoreActivePack();  // o teste carregou o pacote novo no motor; volta ao que estava ativo
    if (!ok) {
      server.send(400, "text/plain", packUploadError.length() ? packUploadError : "Pacote invalido: " + msg);
      return;
    }
    String reply;
    reply.reserve(id.length() + name.length() * 2 + 32);
    reply = F("{\"ok\":1,\"id\":\"");
    reply += id;
    reply += F("\",\"name\":\"");
    jsonEscapeAppend(reply, name.c_str());
    reply += F("\"}");
    server.send(200, "application/json", reply);
  }, handlePackUpload);
  server.on("/api/facedelete", HTTP_POST, [] {
    if (!guard()) return;
    const String &id = server.arg("pkg");
    if (!fsOk || !idOk(id) || !LittleFS.remove("/faces/" + id + ".json")) {
      server.send(400, "text/plain", "Pacote nao encontrado");
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
    server.send(200, "text/plain", "ok");
  });
  server.on("/api/upload", HTTP_POST, [] {
    if (!guard()) return;
    if (uploadError.length()) server.send(400, "text/plain", uploadError);
    else server.send(200, "text/plain", "ok");
  }, handleUpload);
  server.on("/api/delete", HTTP_POST, [] {
    if (!guard()) return;
    const String &f = server.arg("f");
    if (curFile == f) {
      stopAlbum();
      viewUntil = 0;
    }
    if (!fsOk || !mediaPath(f) || !LittleFS.remove(f)) {
      server.send(400, "text/plain", "Nao foi possivel apagar");
      return;
    }
    rebuildAlbum();
    server.send(200, "text/plain", "ok");
  });
  server.on("/api/storage", HTTP_GET, sendStorage);
  // apaga qualquer arquivo ou pasta da memória, menos o que o relógio precisa (fsKeep)
  server.on("/api/fsdelete", HTTP_POST, [] {
    if (!guard()) return;
    String f = server.arg("f");
    if (!fsOk || !f.startsWith("/") || f.length() < 2 || f.length() > 96 || f.indexOf("..") >= 0 || f.indexOf("//") >= 0 ||
        f.endsWith("/") || fsKeep(f) || !LittleFS.exists(f)) {
      server.send(400, "text/plain", "Nao foi possivel apagar");
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
    server.send(ok ? 200 : 500, "text/plain", ok ? "ok" : "Nao foi possivel apagar");
  });
  server.on("/api/show", HTTP_POST, [] {
    if (!guard()) return;
    const String &f = server.arg("f");
    if (phase != IDLE) {
      server.send(409, "text/plain", "Pomodoro em andamento");
      return;
    }
    if (!fsOk || !mediaPath(f) || !showAlbumItem(f)) {
      server.send(400, "text/plain", "Arquivo invalido");
      return;
    }
    viewUntil = millis() + cfg.albumDelay * 1000UL;
    server.send(200, "text/plain", "ok");
  });
  server.on("/api/format", HTTP_POST, [] {
    if (!guard()) return;
    stopAlbum();
    LittleFS.end();
    fsOk = LittleFS.format() && LittleFS.begin();
    if (fsOk) LittleFS.mkdir("/image");
    rebuildAlbum();
    server.send(fsOk ? 200 : 500, "text/plain", fsOk ? "ok" : "falhou");
  });
  // Arquivos de mídia para as miniaturas do painel
  server.on("/fs", HTTP_GET, [] {
    if (!readOk()) return;
    const String &f = server.arg("f");
    File file;
    if (fsOk && mediaPath(f)) file = LittleFS.open(f, "r");
    if (!file) {
      server.send(404, "text/plain", "nao encontrado");
      return;
    }
    server.sendHeader("Cache-Control", "max-age=86400");
    server.streamFile(file, isGif(f) ? "image/gif" : "image/jpeg");
    file.close();
  });
  // Troca a senha do /update. Se já houver senha, exige a atual.
  server.on("/api/otapass", HTTP_POST, [] {
    if (!guard()) return;
    if (cfg.otaPass[0] && server.arg("old") != cfg.otaPass) {
      server.send(403, "text/plain", "Senha atual incorreta");
      return;
    }
    const String &p = server.arg("new");
    if (p.length() >= sizeof(cfg.otaPass)) {
      server.send(400, "text/plain", "Senha muito longa (max 23)");
      return;
    }
    strlcpy(cfg.otaPass, p.c_str(), sizeof(cfg.otaPass));
    saveSettings();
    server.send(200, "text/plain", p.length() ? "Senha definida (usuario: admin)" : "Senha removida");
  });
  server.on("/api/wifireset", HTTP_POST, [] {
    if (!guard()) return;
    server.send(200, "text/plain", "ok");
    delay(300);
    WiFi.persistent(true);
    WiFi.disconnect(true);  // com persistent, apaga a rede salva
    safeRestart();
  });
  setupUpdate();
  server.on("/description.xml", HTTP_GET, sendHueDescription);  // Alexa (ponte Hue)
  server.onNotFound(alexaApi);                                   // /api/<usuário>/lights...
  server.begin();
}

// ---------- atualização do firmware (/update) ----------
// Aceita o nosso firmware e o original da GeekMagic (para voltar ao de fábrica), mas confere o arquivo
// antes de trocar: imagem do ESP8266 (0xE9 no início e no aplicativo), o tamanho e o CRC32 que o elf2bin
// grava no cabeçalho (offsets 4112 e 4116; todas as versões V9.0.x originais também têm) e se cabe no
// espaço livre. Arquivo truncado, corrompido ou de outro aparelho é recusado e o firmware atual fica.
// Usuário "admin"; senha vazia (ou o modo de recuperação) dispensa a autenticação.
struct OtaCheck { uint32_t got, size, want, crc; uint8_t field[8]; String err; bool authOk, begun, gz, finished; } ota;

void otaConsume(const uint8_t *p, size_t n) {
  for (size_t i = 0; i < n && !ota.err.length(); i++, ota.got++) {
    uint32_t o = ota.got;
    uint8_t v = p[i];
    if ((o == 0 || o == 4096) && v != 0xE9) ota.err = F("nao e um firmware do ESP8266");
    if (o >= 4112 && o < 4120) {
      ota.field[o - 4112] = v;
      v = 0;  // os campos de tamanho e CRC entram zerados na conta
      if (o == 4119) {
        memcpy(&ota.size, ota.field, 4);
        memcpy(&ota.want, ota.field + 4, 4);
        uint32_t room = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
        if (ota.size < 4120 || (!ota.gz && ota.size > room)) ota.err = F("tamanho invalido ou sem espaco");  // .gz: conferido em otaVerifyGz
      }
    }
    if (ota.size && o >= ota.size) ota.err = F("arquivo maior que o indicado");
    ota.crc ^= (uint32_t)v << 24;
    for (int k = 0; k < 8; k++) ota.crc = (ota.crc << 1) ^ (ota.crc & 0x80000000 ? 0x04C11DB7 : 0);
  }
}

// ---------- atualização comprimida (.bin.gz) ----------
// O .gz ocupa ~74% do firmware, então cabe mais firmware na área de 1 MB: a versão nova não precisa
// mais caber inteira ao lado da atual. O .gz é gravado no FIM da área e o gerenciador de boot (eboot)
// descompacta no reinício, por cima do firmware atual, setor por setor. Um .gz corrompido deixaria o
// relógio sem firmware; por isso, antes de liberar a troca, o relógio lê o .gz gravado, descompacta
// tudo e confere: CRC do .gz, cabeçalho, tamanho e CRC da imagem (otaConsume) e que a imagem
// descompactada não alcança o .gz que o eboot ainda vai ler. Se algo falhar, a troca é cancelada.
// Janela de 8 KB (compilar.sh comprime assim): um .gz com janela maior é recusado pelo uzlib.
static uint32_t gzAddr, gzEnd;
static uint8_t *gzBuf;
static int gzRead(struct uzlib_uncomp *m) {
  if (gzAddr >= gzEnd) return -1;
  uint32_t n = gzEnd - gzAddr < 512 ? gzEnd - gzAddr : 512;
  if (!ESP.flashRead(gzAddr, gzBuf, n)) return -1;
  gzAddr += n;
  m->source = gzBuf + 1;
  m->source_limit = gzBuf + n;
  return gzBuf[0];
}

bool otaVerifyGz() {
  eboot_command cmd;
  if (eboot_command_read(&cmd) || cmd.action != ACTION_COPY_RAW) {
    ota.err = F("troca nao agendada");
    return false;
  }
  uint32_t src = cmd.args[0];
  uint8_t *dict = (uint8_t *)malloc(8192), out[256];
  TINF_DATA *d = (TINF_DATA *)calloc(1, sizeof(TINF_DATA));  // ~1,2 KB de tabelas: fora da pilha
  gzBuf = (uint8_t *)malloc(512);
  if (dict && gzBuf && d) {
    gzAddr = src;
    gzEnd = src + cmd.args[2];
    uzlib_init();
    d->source_read_cb = gzRead;
    uzlib_uncompress_init(d, dict, 8192);
    int res = uzlib_gzip_parse_header(d);
    ota.got = ota.size = ota.want = 0;  // otaConsume confere agora a imagem descompactada
    ota.crc = 0xFFFFFFFF;
    while (res == TINF_OK && !ota.err.length()) {
      d->dest_start = d->dest = out;
      d->dest_limit = out + sizeof out;
      res = uzlib_uncompress_chksum(d);
      if (res == TINF_OK || res == TINF_DONE) otaConsume(out, d->dest - out);
      yield();
    }
    if (ota.err.length()) {}
    else if (res != TINF_DONE) ota.err = F("arquivo comprimido corrompido");
    else if (ota.got < 4120 || ota.got != ota.size) ota.err = F("imagem incompleta");
    else if (ota.crc != ota.want) ota.err = F("imagem corrompida (CRC)");
    else if (ota.size > src) ota.err = F("firmware grande demais para descompactar");
    else {
      // O eboot não lê o tamanho do gzip: usa os 4 ÚLTIMOS bytes da área gravada como o tamanho da
      // imagem. Qualquer coisa depois do gzip (como os 260 bytes de assinatura da 1.20.0) vira o
      // tamanho, e o eboot grava só o começo por cima do setor 0 e apaga o próprio gerenciador de
      // boot: o relógio não liga mais. Só libera a troca se esse número for o da imagem conferida.
      uint8_t t[4];
      if (cmd.args[2] < 4 || !ESP.flashRead(src + cmd.args[2] - 4, t, 4)) ota.err = F("fim do arquivo ilegivel");
      else if ((uint32_t)(t[0] | t[1] << 8 | t[2] << 16 | (uint32_t)t[3] << 24) != ota.got)
        ota.err = F("dados depois do gzip (envie o .bin.gz sem assinatura ou o .bin)");
    }
  } else ota.err = F("sem memoria para conferir");
  free(dict);
  free(gzBuf);
  free(d);
  if (ota.err.length()) eboot_command_clear();  // cancela a troca: o firmware atual continua
  return !ota.err.length();
}

// ---------- assinatura das atualizações ----------
// Pela rede, o relógio só troca de firmware se o arquivo vier com a assinatura do projeto: RSA-2048 sobre o
// SHA-256 do arquivo exatamente como enviado (.bin ou .bin.gz), em base64 no cabeçalho X-Firmware-Sig.
// A assinatura NÃO vai dentro do arquivo: na 1.20.0 ela ia no fim do .bin.gz e o eboot usou os 4 últimos
// bytes como tamanho da imagem, apagando o gerenciador de boot. Também não usa a assinatura do Updater do
// núcleo (que espera o mesmo sufixo). A conta é feita antes de Update.end(true): sem assinatura válida
// nada é agendado e o firmware atual continua. O modo de recuperação (físico: rede própria com a senha na
// tela) aceita firmware sem assinatura, para voltar ao original ou se a chave se perder.
// Só a conta RSA do BearSSL (i15, a menor), com a chave em bytes: as classes do núcleo (PublicKey,
// SigningVerifier) trazem junto o leitor de PEM e as curvas elípticas, ~30 KB a mais no firmware.
// A conta roda na pilha extra do núcleo (6,2 KB, alocada só durante a conferência).
extern "C" uint32_t thunk_br_rsa_i15_pkcs1_vrfy(const unsigned char *x, size_t xlen, const unsigned char *oid,
                                                size_t hlen, const br_rsa_public_key *pk, unsigned char *out);
make_stack_thunk(br_rsa_i15_pkcs1_vrfy);
static br_sha256_context otaHash;
static uint8_t otaSig[256];
static bool otaSigGiven;

static void otaSigBegin() {
  br_sha256_init(&otaHash);
  otaSigGiven = false;
  const String &b64 = server.header("X-Firmware-Sig");
  if (b64.length() < 340 || b64.length() > 348) return;  // 256 bytes = 344 caracteres
  char buf[264];
  base64_decodestate st;
  base64_init_decodestate(&st);
  if (base64_decode_block(b64.c_str(), b64.length(), buf, &st) != 256) return;
  memcpy(otaSig, buf, 256);
  otaSigGiven = true;
}

static bool otaSigOk() {
  if (recoveryMode) return true;
  if (!otaSigGiven) {
    ota.err = F("firmware sem assinatura (use o arquivo do site ou o modo de recuperacao)");
    return false;
  }
  if (ESP.getMaxFreeBlockSize() < 7500) {  // a conta usa uma pilha extra de 6,2 KB
    ota.err = F("sem memoria para conferir a assinatura");
    return false;
  }
  uint8_t n[sizeof OTA_KEY_N], e[sizeof OTA_KEY_E], want[32], got[32];
  memcpy_P(n, OTA_KEY_N, sizeof n);  // o BearSSL lê a chave byte a byte: precisa estar na RAM
  memcpy_P(e, OTA_KEY_E, sizeof e);
  br_rsa_public_key key = {n, sizeof n, e, sizeof e};
  br_sha256_out(&otaHash, want);
  stack_thunk_add_ref();
  bool ok = thunk_br_rsa_i15_pkcs1_vrfy(otaSig, sizeof otaSig, BR_HASH_OID_SHA256, sizeof got, &key, got) &&
            !memcmp(got, want, sizeof got);
  stack_thunk_del_ref();
  if (!ok) ota.err = F("assinatura invalida");
  return ok;
}

void setupUpdate() {
  auto authOk = [] {
    if (!panelOk()) return false;
    if (recoveryMode) return true;
    return uploadAuthed();
  };
  server.on("/update", HTTP_GET, [] {
    if (!hostOk()) { deny(); return; }
    if (!recoveryMode && !authed()) return;
    String p = F("<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'><title>SmallTV</title>"
                 "<style>body{font:16px system-ui;background:#111;color:#eee;max-width:420px;margin:auto;padding:20px}"
                 "input,button{width:100%;padding:12px;margin:8px 0;border-radius:10px;border:1px solid #333;background:#1c1c1c;color:#eee;font:inherit}"
                 "button{background:#ff5a47;border:0;font-weight:700}</style><h2>");
    p += tr("Atualizar firmware", "Update firmware", "Actualizar firmware");
    p += F("</h2><form method=post enctype=multipart/form-data><input type=file name=firmware accept=.bin,.gz required>");
    if (!recoveryMode) {  // fora do modo de recuperação só entra firmware assinado: o .sig vem junto no site
      p += tr("Assinatura (.sig)", "Signature (.sig)", "Firma (.sig)");
      p += F("<input type=file name=sig accept=.sig required>");
    }
    p += F("<button>");
    p += tr("Enviar", "Upload", "Enviar");
    // Só o firmware vai no corpo; a assinatura segue em base64 no cabeçalho X-Firmware-Sig.
    p += F("</button></form><small>v" FW_VERSION "</small>"
           "<script>document.forms[0].onsubmit=async e=>{e.preventDefault();const f=e.target,h={'X-SmallTV':'1'},d=new FormData();"
           "d.append('firmware',f.firmware.files[0]);"
           "if(f.sig&&f.sig.files[0])h['X-Firmware-Sig']=btoa(String.fromCharCode(...new Uint8Array(await f.sig.files[0].arrayBuffer())));"
           "document.body.innerHTML='...';"
           "fetch('/update',{method:'POST',body:d,headers:h}).then(r=>r.text()).then(t=>document.body.innerHTML=t)}</script>");
    server.send(200, "text/html", p);
  });
  server.on("/update", HTTP_POST, [authOk] {
      if (!authOk() || !ota.authOk) return askLogin();
      if (!ota.err.length() && !ota.finished) ota.err = F("envio incompleto");
      if (ota.err.length()) {
        server.send(400, "text/plain", "Update error: " + ota.err);
        return;
      }
      server.send(200, "text/html", F("<meta http-equiv=refresh content='20;URL=/'>Update Success! Rebooting..."));
      delay(200);
      safeRestart();
    }, [authOk] {
      HTTPUpload &up = server.upload();
      if (up.status == UPLOAD_FILE_START) {
        ota = {0, 0, 0, 0xFFFFFFFF, {0}, String(), authOk(), false, false, false};
        otaSigBegin();
        if (!ota.authOk) ota.err = F("senha");
        else if (inAlbum) stopAlbum();  // libera RAM e o LittleFS durante a gravação
      } else if (up.status == UPLOAD_FILE_WRITE && !ota.err.length()) {
        if (!ota.begun) {  // primeiro pedaço: .gz vai para o fim da área (tamanho do envio); .bin, para o espaço livre
          ota.begun = true;
          ota.gz = up.currentSize > 1 && up.buf[0] == 0x1F && up.buf[1] == 0x8B;
          uint32_t size = ota.gz ? server.header("Content-Length").toInt() : (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
          if (!size || !Update.begin(size, U_FLASH)) ota.err = size ? Update.getErrorString() : String(F("tamanho desconhecido"));
        }
        if (!ota.gz) otaConsume(up.buf, up.currentSize);
        br_sha256_update(&otaHash, up.buf, up.currentSize);
        if (!ota.err.length() && Update.write(up.buf, up.currentSize) != up.currentSize) ota.err = Update.getErrorString();
        if (ota.err.length()) Update.end();  // desiste: o firmware atual continua
      } else if (up.status == UPLOAD_FILE_END && !ota.err.length()) {
        if (!ota.begun) ota.err = F("arquivo vazio");
        else if (ota.gz) {
          if (!otaSigOk()) Update.end();  // assinatura antes de agendar a troca: sem ela nada muda
          else if (!Update.end(true)) ota.err = Update.getErrorString();
          else otaVerifyGz();
        } else if (ota.got < 4120 || ota.got != ota.size) ota.err = F("arquivo incompleto");
        else if (ota.crc != ota.want) ota.err = F("arquivo corrompido (CRC)");
        else otaSigOk();
        if (ota.gz) {}
        else if (ota.err.length()) Update.end();
        else if (!Update.end(true)) ota.err = Update.getErrorString();
        ota.finished = !ota.err.length();
      } else if (up.status == UPLOAD_FILE_ABORTED) {
        Update.end();
        ota.err = F("envio interrompido");
      }
      yield();
    });
}

// ---------- modo de recuperação ----------
#define RECOVERY_AP "SmallTV-Recuperacao"
void startRecovery() {
  newFormToken();
  WiFi.mode(WIFI_AP);
  WiFi.softAP(RECOVERY_AP, cfg.apPass);
  String pw = String(tr("senha: ", "password: ", "clave: ")) + cfg.apPass;
  message(tr("Modo de recuperacao", "Recovery mode", "Modo de recuperacion"), RECOVERY_AP, pw.c_str(), "http://192.168.4.1");
  server.on("/", HTTP_GET, [] {
    String p = F("<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'><title>SmallTV</title>"
                 "<style>body{font:16px system-ui;background:#111;color:#eee;max-width:420px;margin:auto;padding:20px}"
                 "button,a{display:block;width:100%;box-sizing:border-box;text-align:center;padding:12px;margin:8px 0;border-radius:10px;"
                 "border:1px solid #333;background:#1c1c1c;color:#eee;font:inherit;text-decoration:none}.p{background:#ff5a47;border:0;font-weight:700}</style><h2>");
    p += tr("Modo de recuperacao", "Recovery mode", "Modo de recuperacion");
    p += F("</h2><p>v" FW_VERSION " &middot; ");
    p += WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String(F("192.168.4.1"));
    p += F("</p><a class=p href=/update>");
    p += tr("Atualizar firmware", "Update firmware", "Actualizar firmware");
    p += F("</a><a href=/api/stats download=historico.json>");
    p += tr("Baixar historico de focos", "Download focus history", "Descargar historial");
    p += F("</a><form method=post action=/rec/factory>");
    p += formTokenField(); p += F("<button>");
    p += tr("Voltar aos ajustes de fabrica", "Reset settings to defaults", "Restablecer ajustes");
    p += F("</button></form><form method=post action=/rec/wifi>");
    p += formTokenField(); p += F("<button>");
    p += tr("Esquecer o Wi-Fi", "Forget Wi-Fi", "Olvidar el Wi-Fi");
    p += F("</button></form><form method=post action=/rec/restart>");
    p += formTokenField(); p += F("<button class=p>");
    p += tr("Reiniciar normalmente", "Restart normally", "Reiniciar normalmente");
    p += F("</button></form><form method=post action=/rec/wipe onsubmit=\"return confirm(this.dataset.q)\" data-q=\"");
    p += tr("Apagar fotos, historico, ajustes e Wi-Fi?", "Erase photos, history, settings and Wi-Fi?", "Borrar fotos, historial, ajustes y Wi-Fi?");
    p += F("\">");
    p += formTokenField(); p += F("<button>");
    p += tr("Apagar todos os dados pessoais", "Erase all personal data", "Borrar todos los datos personales");
    p += F("</button></form>");
    server.send(200, "text/html", p);
  });
  auto done = [](const __FlashStringHelper *msg) {
    server.sendHeader("Location", "/");
    server.send(303, "text/plain", msg);
  };
  server.on("/rec/factory", HTTP_POST, [done] {
    if (!formOk()) { deny(); return; }
    uint8_t lang = cfg.lang;
    defaultSettings();
    cfg.lang = lang;
    saveSettings();
    done(F("ok"));
  });
  server.on("/rec/wifi", HTTP_POST, [done] {
    if (!formOk()) { deny(); return; }
    WiFi.persistent(true);
    WiFi.disconnect(true);
    done(F("ok"));
  });
  server.on("/rec/restart", HTTP_POST, [] {
    if (!formOk()) { deny(); return; }
    server.send(200, "text/plain", "ok");
    delay(300);
    safeRestart();
  });
  // antes de doar ou vender o relógio: a flash não é criptografada, então apaga de fato
  // (arquivos, ajustes com senhas, rede salva) e volta como saído da caixa
  server.on("/rec/wipe", HTTP_POST, [] {
    if (!formOk()) { deny(); return; }
    server.send(200, "text/plain", "ok");
    delay(300);
    if (fsOk) LittleFS.format();
    for (int i = 0; i < EE_SIZE; i++) EEPROM.write(i, 0);
    EEPROM.commit();
    WiFi.persistent(true);
    WiFi.disconnect(true);
    ESP.eraseConfig();
    delay(300);
    ESP.restart();
  });
  server.on("/api/stats", HTTP_GET, sendStats);
  server.collectHeaders("X-SmallTV", "Authorization", "Content-Length", "X-Firmware-Sig");
  setupUpdate();
  server.begin();
}

void rotSet(const String &v) {
  String s;
  s.reserve(min((size_t)300, v.length()) + 1);
  for (char c : v)  // só o formato da lista: nada de aspas ou barras no JSON do estado
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == ':' || c == ',' || c == '-' || c == '_') s += c;
  if (s.length() > 300) s = s.substring(0, s.lastIndexOf(',', 300));
  rotList = s;
  rotNext = millis() + cfg.rotMin * 60000UL;
  if (!fsOk) return;
  if (!s.length()) LittleFS.remove("/rotation.txt");
  else {
    File f = LittleFS.open("/rotation.txt", "w");
    if (f) {
      f.print(s);
      f.close();
    }
  }
}

void rotLoop() {
  if (!cfg.rotMin || !rotList.length() || phase != IDLE || previewActive() || inAlbum) return;
  if ((int32_t)(millis() - rotNext) < 0) return;
  rotNext = millis() + cfg.rotMin * 60000UL;
  // o item depois do mostrador atual (ou o primeiro, se o atual não está na lista)
  String cur = cfg.theme == THEME_PACK ? String("p:") + cfg.facePkg : String("t:") + cfg.theme, l = rotList + ",";
  int at = ("," + l).indexOf("," + cur + ","), start = at >= 0 ? at + cur.length() + 1 : 0;
  for (int tries = 0; tries < 32; tries++) {  // pula itens inválidos (pacote removido)
    if (start >= (int)l.length()) start = 0;
    int e = l.indexOf(',', start);
    String it = l.substring(start, e);
    start = e + 1;
    if (it.startsWith("t:")) {
      int t = it.substring(2).toInt();
      if (t < 0 || t >= THEME_COUNT) continue;
      cfg.theme = t;
      facePackUnload();
    } else if (it.startsWith("p:") && loadPack(it.substring(2))) {
      cfg.theme = THEME_PACK;
      strlcpy(cfg.facePkg, it.substring(2).c_str(), sizeof cfg.facePkg);
    } else continue;
    uiInvalidateSlide();  // mostrador novo entra rolando (ui.cpp)
    return;
  }
}

// ---------- avisos de fim de fase (Alexa e afins) ----------
// Um endereço por evento em /hooks.txt (fim do foco, fim da pausa). Quando a fase termina o relógio
// chama o endereço (GET), e o serviço dispara uma rotina: a Virtual Smart Home e o Voice Monkey fazem a
// Alexa falar, o IFTTT liga a centenas de serviços. Vai sem criptografia (http): os três aceitam, e
// TLS não cabe no relógio. Um "https://" colado no painel vira "http://"; o resto do endereço é conferido
// por safeHttpUrl (security.h) antes de montar o pedido.

void hooksLoad() {
  File f = fsOk ? LittleFS.open("/hooks.txt", "r") : File();
  if (!f) return;
  hookWork = f.readStringUntil('\n');
  hookBreak = f.readStringUntil('\n');
  f.close();
  hookWork = hookClean(hookWork);
  hookBreak = hookClean(hookBreak);
}

String hookClean(String u) {
  u.trim();
  if (u.startsWith("https://")) u = "http://" + u.substring(8);
  if (!safeHttpUrl(u.c_str(), u.length())) return "";
  return u;
}

// chama o endereço; devolve o código HTTP (0 = sem endereço ou sem Wi-Fi, negativo = falha de conexão)
int hookFire(const String &url) {
  if (!url.length() || WiFi.status() != WL_CONNECTED) return 0;
  WiFiClient client;
  int code = httpGet(client, url, 4000);
  client.stop();
  return code;
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
WiFiEventHandler wifiAuthHandler;
volatile bool wifiDowngrade = false;

static int authRank(uint8_t m) { return m == AUTH_OPEN ? 0 : m == AUTH_WEP ? 1 : 2; }  // WPA, WPA2 e misto: 2

// Rede salva por outro firmware (o original) pode não exigir WPA: passa a exigir, como faz o begin() do núcleo
void wifiHarden() {
  struct station_config conf;
  if (!wifi_station_get_config_default(&conf) || !conf.password[0]) return;  // rede aberta salva: nada a exigir
  bool ok = conf.threshold.authmode >= AUTH_WPA_PSK;  // NONOS 2.2: só se conecta a redes com este modo ou melhor
#if (NONOSDK >= (0x30000))
  ok = ok && conf.open_and_wep_mode_disable;
  conf.open_and_wep_mode_disable = 1;
#endif
  if (ok) return;
  conf.threshold.authmode = AUTH_WPA_PSK;
  wifi_station_set_config(&conf);
}

void wifiGuardBegin() {
  wifiAuthHandler = WiFi.onStationModeAuthModeChanged([](const WiFiEventStationModeAuthModeChanged &e) {
    if (authRank(e.newMode) < authRank(e.oldMode)) wifiDowngrade = true;  // trata no loop, fora do SDK
  });
}

void wifiGuardLoop() {
  if (!wifiDowngrade) return;
  wifiDowngrade = false;
  wifiDowngrades++;
  WiFi.reconnect();
}

bool connectStored(uint32_t timeoutMs) {
  WiFi.mode(WIFI_STA);
  WiFi.hostname(netName);
  wifiHarden();
  WiFi.begin();
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeoutMs) delay(100);
  return WiFi.status() == WL_CONNECTED;
}

void htmlEscapeAppend(String &o, const String &s) {
  for (char c : s) {
    if (c == '<') o += "&lt;";
    else if (c == '>') o += "&gt;";
    else if (c == '&') o += "&amp;";
    else if (c == '"') o += "&quot;";
    else o += c;
  }
}

void startPortal() {
  newFormToken();
  portalMode = true;
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_NAME, cfg.apPass);
  dns.start(53, "*", WiFi.softAPIP());
  String pw = String(tr("senha: ", "password: ", "clave: ")) + cfg.apPass;
  message(tr("Configure o Wi-Fi", "Set up Wi-Fi", "Configura el Wi-Fi"), AP_NAME, pw.c_str(), "http://192.168.4.1");
  server.on("/", HTTP_GET, [] {
    int n = WiFi.scanNetworks();
    String opts;
    opts.reserve(min(2048, max(0, n) * 48));
    for (int i = 0; i < n; i++) {
      opts += F("<option>");
      htmlEscapeAppend(opts, WiFi.SSID(i));
      opts += F("</option>");
    }
    String p;
    p.reserve(900 + opts.length());
    p = F("<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
          "<title>SmallTV Wi-Fi</title><style>body{font:16px system-ui;background:#111;color:#eee;max-width:380px;margin:auto;padding:20px}"
          "input,button{width:100%;padding:12px;margin:6px 0 14px;border-radius:10px;border:1px solid #333;background:#1c1c1c;color:#eee;font:inherit}"
          "button{background:#ff5a47;border:0;font-weight:700}</style><h2>SmallTV Wi-Fi</h2><form method=post action=/save>");
    p += tr("Rede", "Network", "Red");
    p += F("<input name=s list=l required><datalist id=l>");
    p += opts;
    p += F("</datalist>");
    p += formTokenField();
    p += tr("Senha", "Password", "Contrase&ntilde;a");
    p += F("<input name=p type=password><button>");
    p += tr("Conectar", "Connect", "Conectar");
    p += F("</button></form>");
    server.send(200, "text/html", p);
  });
  server.on("/save", HTTP_POST, [] {
    if (!formOk()) { deny(); return; }
    String p = F("<meta charset=utf-8><body style='font:16px system-ui;background:#111;color:#eee;padding:20px'>");
    PGM_P done[3] = {PSTR("Conectando... O rel&oacute;gio vai reiniciar e mostrar o endere&ccedil;o do painel na tela."),
                     PSTR("Connecting... The clock will restart and show the panel address on screen."),
                     PSTR("Conectando... El reloj se reiniciar&aacute; y mostrar&aacute; la direcci&oacute;n del panel en la pantalla.")};
    p += FPSTR(done[cfg.lang]);  // longo demais para o buffer do tr()
    server.send(200, "text/html", p);
    delay(500);
    WiFi.persistent(true);
    WiFi.begin(server.arg("s").c_str(), server.arg("p").c_str());
    delay(1000);
    safeRestart();
  });
  // qualquer outro endereço cai no formulário (portal cativo do celular)
  server.onNotFound([] {
    server.sendHeader("Location", "http://192.168.4.1/");
    server.send(302, "text/plain", "");
  });
  server.begin();
}

// ---------- setup / loop ----------
void setup() {
  snprintf(netName, sizeof netName, HOSTNAME "-%04x", (unsigned)(ESP.getChipId() & 0xFFFF));
  pinMode(PIN_BL, OUTPUT);
  analogWriteRange(BL_RANGE);
  analogWriteFreq(BL_HZ);
  loadSettings();
  BootCount bc;
  EEPROM.get(BOOT_OFF, bc);
  uint8_t boots = bc.magic == BOOT_MAGIC ? bc.n : 0;
  crashLoad();
  recoveryMode = boots + 1 >= 3;
  setBootCount(recoveryMode ? 0 : boots + 1);  // a recuperação vale uma vez; o próximo reinício é normal
  updateBrightness(true);

  tft.init(240, 240, SPI_MODE3);
  tft.setSPISpeed(40000000);  // 80 MHz passa do limite do ST7789: a imagem vira lixo
  applyVcom(cfg.vcom);
  uiBegin(&tft);
  uiSetLang(cfg.lang);
  // entre os quadros de animação (virada do Flip): cede tempo ao Wi-Fi e dá ritmo à virada
  uiFrameHook = [] { yield(); };  // a animação é guiada pelo relógio: só cede tempo ao Wi-Fi
  uiMillis = [] { return (uint32_t)millis(); };
  // linhas de pixels pelo caminho rápido do ST7789 (drawRGBBitmap não é virtual no Adafruit_GFX)
  uiBlit = tftBitmap;
  uiScrollHw = tftScroll;
  applyPanel();

  // Monta o LittleFS do firmware original SEM formatar: se falhar, os arquivos ficam intactos.
  LittleFSConfig fcfg;
  fcfg.setAutoFormat(false);
  LittleFS.setConfig(fcfg);
  fsOk = LittleFS.begin();
  statsLoad();
  if (fsOk) {
    File rf = LittleFS.open("/rotation.txt", "r");
    if (rf) {
      rotList = rf.readString();
      rf.close();
    }
  }
  hooksLoad();
  if (recoveryMode) {  // sem pacotes, álbum nem clima: só o necessário para consertar
    startRecovery();
    return;
  }
  uiDrawImage = drawJpgAt;
  if (cfg.theme == THEME_PACK) restoreActivePack();  // pacote ausente/inválido: volta ao Matriz
  rebuildAlbum();

  message("SmallTV Pomodoro", tr("Conectando Wi-Fi", "Connecting Wi-Fi", "Conectando Wi-Fi"), "v" FW_VERSION);

  // Usa a rede salva no ESP8266 (inclusive a que o firmware original já tinha configurado).
  wifiGuardBegin();
  timeGuardBegin();
  if (!connectStored(20000)) {
    startPortal();
    return;
  }
  applyTimezone();
  setupWeb();
  alexaBegin();
  mdnsStart();
  String addr = String(lanName[0] ? lanName : netName) + ".local";
  message(tr("Conectado", "Connected", "Conectado"), addr.c_str(), WiFi.localIP().toString().c_str());
  delay(3000);  // tempo de ler o endereço do painel
}

uint32_t lastRender = 0, lastBriCheck = 0;

void loop() {
  diagLoop();
  if (bootCount && millis() > 30000) setBootCount(0);  // ficou ligado: não foi uma inicialização curta
  if (recoveryMode) {
    server.handleClient();
    return;
  }
  if (portalMode) {
    dns.processNextRequest();
    server.handleClient();
    // sem configuração em 5 min (ex.: roteador estava desligado): reinicia e tenta a rede salva de novo
    if (millis() > 300000) safeRestart();
    return;
  }
  server.handleClient();
  mdnsLoop();
  statsFlush();
  wifiGuardLoop();
  alexaLoop();
  tickPomodoro();
  weatherLoop();
  rotLoop();
  if (hookPending >= 0) {  // a fase acabou: avisa (fora do tick do Pomodoro; leva até 4 s sem Wi-Fi bom)
    int8_t p = hookPending;
    hookPending = -1;
    hookFire(p ? hookBreak : hookWork);
  }

  uint32_t now = millis();
  if (now - lastBriCheck > 5000) {
    lastBriCheck = now;
    updateBrightness();
    timeGuard.tick(time(nullptr), now);
  }
  briFade();

  if (vcomTestUntil) {
    if ((int32_t)(now - vcomTestUntil) < 0) {
      if (vcomDraw) {
        if (inAlbum) stopAlbum();
        uiVcomTest(vcomTest, vcomDraw == 2);
        vcomDraw = 0;
      }
      return;
    }
    vcomTestUntil = 0;  // fim do teste: volta ao valor salvo e ao mostrador
    applyVcom(cfg.vcom);
    uiInvalidate();
    viewUntil = 0;
  }

  if (colorTestRequested) {
    colorTestRequested = false;
    if (inAlbum) stopAlbum();
    uiColorTest();
    colorTestUntil = now + 10000;
  }
  if (colorTestUntil) {
    if ((int32_t)(now - colorTestUntil) < 0) return;
    colorTestUntil = 0;
    uiInvalidate();
    viewUntil = 0;
  }

  if (previewUntil && !previewActive()) {  // fim da prévia: volta ao mostrador salvo
    previewUntil = 0;
    viewUntil = 0;
    if (previewTheme == THEME_PACK) restoreActivePack();
  }
  if (previewActive()) {
    if (inAlbum) stopAlbum();
  } else if (phase == IDLE) {
    scheduleView();
    albumLoop();  // quadros de GIF precisam rodar fora do limite de 100 ms
  } else if (inAlbum) {
    stopAlbum();
    viewUntil = 0;
  }

  if (now - lastRender < 100) return;
  lastRender = now;

  if (alertUntil) {
    if ((int32_t)(now - alertUntil) < 0) renderAlert();
    else { alertUntil = 0; tft.invertDisplay(cfg.invert); }  // volta ao ajuste do painel
  }
  if (phase != IDLE) renderPomodoro();
  else {
    pomoShown = false;  // na próxima entrada a tela do Pomodoro é desenhada inteira de novo
    if (!inAlbum) renderClock();
  }
}
