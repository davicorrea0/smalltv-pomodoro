// app.h — declarações compartilhadas entre os módulos do firmware (constantes, estruturas, o estado
// global e os protótipos). Cada módulo (modulos/*.cpp, incluídos por main.cpp) define o que é seu; o que só ele usa fica static.
#pragma once
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
#include <libb64/cdecode.h>
#include "security.h"
#include "warm.h"
#include "bri_curva.h"  // curva de brilho em tabela (sim/gerar_brilho.js)
#include <new>
#include <vector>
#include <time.h>
#include <sys/time.h>
#include <lwip/dns.h>  // dns_gethostbyname assíncrono (httpGet nunca espera por um nome)
#include <coredecls.h>  // settimeofday_cb: avisa quando o SNTP muda a hora
#include "ui.h"
#include "facepack.h"
#include "src/AnimatedGIF/AnimatedGIF.h"
extern "C" uint32_t _FS_start;

void blit(int16_t x, int16_t y, uint16_t *px, int16_t w, int16_t h);  // ui.cpp: recorta para a área suja; fora da tela, cai na faixa


#define FW_VERSION "1.24.13"
#define HOSTNAME   "smalltv"
#define AP_NAME    "SmallTV-Pomodoro"
#define PIN_DC  0
#define PIN_RST 2
#define PIN_BL  5
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
  uint8_t lang;         // idioma da tela e do painel: 0 português, 1 inglês, 2 espanhol; padrão de fábrica 1 (inglês); lixo vira 0 (quem já usa não muda)
  uint8_t nightSun;     // brilho noturno do pôr ao nascer do sol (Open-Meteo) em vez de nightStart/nightEnd
  uint8_t rotMin;       // rodízio de mostradores: minutos entre trocas (0 = desligado); lista em /rotation.txt
  uint8_t alexa;        // 1 = aparece para a Alexa como a lâmpada "Pomodoro"
  char apPass[9];       // senha das redes SmallTV-Pomodoro e SmallTV-Recuperacao (sorteada, mostrada na tela)
  uint8_t vcom, vcomChk;  // tensão comum do painel (registrador VCOMS) e marca de calibrado (vcom ^ 0xDA)
  uint8_t fahr;         // 1 = temperaturas em °F; lixo vira 0
  uint8_t rainAlert;    // 1 = avisa na tela quando vai chover na próxima hora; lixo vira 1
  uint8_t beta;         // 1 = o painel oferece também as versões de teste (firmware/beta.json); lixo vira 0
  uint8_t spi80;        // reservado: era a opção de 80 MHz de um teste (o ST7789 não aguenta e a imagem vira lixo); sempre 0
  uint8_t warm;         // cor quente à noite: 0 = desligada, 1..100 = quanto do azul sai (gama digital do ST7789V); lixo vira 0
  uint8_t lcdGreen;     // 1 = tom verde de LCD antigo nos mostradores LCD retrô, Previsão LCD, Mundo e Mundo relógio (warm.h); lixo vira 1
};
const uint32_t SETTINGS_MAGIC = 0x504F4D34;     // "POM4"
const uint32_t SETTINGS_MAGIC_V3 = 0x504F4D33;  // "POM3": mesmos campos, sem os do v4
// ---------- recuperação: 3 inicializações curtas seguidas ----------
// O aparelho não tem botão: ligar e desligar 3 vezes seguidas (cada vez por menos de 30 s), ou um
// firmware que trava logo ao ligar, abre o modo de recuperação (rede própria, atualização sem senha,
// ajustes de fábrica). O contador fica na EEPROM, depois das configurações: sobrevive a tirar da
// tomada, ao contrário da memória RTC. 30 s ligado zeram a contagem.
const int BOOT_OFF = 400;
static_assert(sizeof(Settings) <= BOOT_OFF, "as configuracoes invadiriam o contador de inicializacoes");
struct BootCount { uint32_t magic; uint8_t n; };
const uint32_t BOOT_MAGIC = 0x424F4F54;  // "BOOT"
// Última queda (exceção ou watchdog), para o painel dizer por que o relógio reiniciou. Fica na EEPROM
// depois do contador e só é gravada quando o reinício foi anormal: início normal não gasta flash.
// "up" (segundos ligado antes da queda) vem da memória RTC, que o callback abaixo preenche ao travar.
struct CrashRec { uint32_t magic; uint8_t n, reason, exc, pad; uint32_t epc, vaddr, up; char ver[8]; };
const uint32_t CRASH_MAGIC = 0x43524153;  // "CRAS"
const int CRASH_OFF = BOOT_OFF + sizeof(BootCount);
// Marca da memória de arquivos no layout 4M2M: "FS2M" = o LittleFS deste endereço já foi formatado por nós.
// Sem ela (primeiro início depois de vir do 4M3M ou do firmware original) o conteúdo ali é lixo do layout
// antigo e fsMount formata; `pend` fica 1 até o painel confirmar que o backup foi restaurado.
struct FsMark { uint32_t magic; uint8_t pend; };
const uint32_t FS_MAGIC = 0x46533241;  // "FS2A"
const int FS_OFF = CRASH_OFF + sizeof(CrashRec);
// Último roteador: canal e BSSID, para o próximo início conectar sem varrer todos os canais (ver connectStored).
// Só é gravado quando muda (trocar de rede ou o roteador trocar de canal): ligar não gasta flash.
struct WifiHint { uint32_t magic; uint32_t ssid; uint8_t bssid[6]; uint8_t chan, pad; };
const uint32_t WIFI_MAGIC = 0x57494648;  // "WIFH"
const int WIFIH_OFF = FS_OFF + sizeof(FsMark);
const int EE_SIZE = WIFIH_OFF + sizeof(WifiHint);
static_assert(sizeof(WifiHint) == 16, "registro do Wi-Fi mudou de tamanho");
static_assert(sizeof(CrashRec) == 28, "registro da queda mudou de tamanho");
static_assert(sizeof(FW_VERSION) <= sizeof(CrashRec::ver), "a versao nao cabe no registro da queda");
const uint32_t CRASH_RTC_TAG = 0x55505453;  // "UPTS"
// Memória RTC de usuário: os blocos 0-31 (128 bytes) são do comando que a atualização deixa para o eboot
// (Updater, eboot_command em 0x60001200). Escrever ali entre o fim da atualização e o reinício corrompe o
// comando, e o eboot liga a versão antiga (foi o que a 1.23.2 fazia ao guardar a hora). Nossos dados: do 32 em diante.
const uint32_t RTC_CRASH_BLOCK = 32;  // 2 blocos: marca e segundos ligado
// A hora passa pelo reinício por software na memória RTC (fora dos blocos 0-31 do eboot e dos 32-33 da queda).
// Volta como hora de reserva: aparece na hora, mas só o SNTP junto do clima a confirma.
const uint32_t RTC_CLOCK_BLOCK = 40;
// ---------- brilho (com horário noturno) ----------
// PWM a 20 kHz (fora da faixa audível e de piscar) com 1023 degraus; pino invertido (0 = aceso no máximo).
// Curva gama 2.2 (perceptual) sobre o 0-100 do cfg; 1 % já sai com ~1 % de duty, para não apagar.
const uint16_t BL_RANGE = 1023;
const uint32_t BL_HZ = 20000;
const uint16_t BL_MIN = 10;      // duty mínimo para qualquer brilho > 0
const uint16_t BL_FADE_MS = 400;  // rampa entre dois níveis (dia/noite, slider)
// ---------- Pomodoro ----------
enum Phase : uint8_t { IDLE, WORK, SHORT_BREAK, LONG_BREAK };
enum MdnsTo : uint8_t { MDNS_GROUP, MDNS_DIRECT, MDNS_BROADCAST };  // multicast, quem perguntou, broadcast da sub-rede
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
};
// GET em HTTP/1.0 (resposta sem "chunked", fecha no fim) no lugar do HTTPClient (~4 KB).
// Devolve o código HTTP (negativo se não conectar) e deixa o cliente no começo do corpo.
const int HTTP_WAIT = -3;  // o nome (DNS) ainda está sendo buscado: tente de novo no próximo laço
const int HTTP_DNS = -4;   // o nome não resolveu
const uint16_t HTTP_CONNECT_MS = 1500;  // conectar: com o IP já em cache só falta o SYN/ACK; mais que isso é rede sem saída
const char *weatherText(int code);  // de ui.cpp (o mesmo texto que aparece na tela)
// ---------- histórico de focos (/stats.bin) ----------
// Um contador por dia num anel de 371 dias (53 semanas): dá a sequência de dias com a meta batida,
// o recorde e o jardim do painel. Só o cabeçalho fica na RAM; os dias são lidos do arquivo.
struct StatsHead { uint32_t magic; uint16_t lastDay, best; uint32_t total; };
const uint32_t STATS_MAGIC = 0x544F4D31;  // "TOM1"
const uint16_t STATS_DAYS = 371;
// Resultado de uma foto: falta de memória no momento não é arquivo estragado.
enum JpgResult : uint8_t { JPG_OK, JPG_BAD, JPG_NOMEM };
// ---------- atualização do firmware (/update) ----------
// Aceita o nosso firmware e o original da GeekMagic (para voltar ao de fábrica), mas confere o arquivo
// antes de trocar: imagem do ESP8266 (0xE9 no início e no aplicativo), o tamanho e o CRC32 que o elf2bin
// grava no cabeçalho (offsets 4112 e 4116; todas as versões V9.0.x originais também têm) e se cabe no
// espaço livre. Arquivo truncado, corrompido ou de outro aparelho é recusado e o firmware atual fica.
// Usuário "admin"; senha vazia (ou o modo de recuperação) dispensa a autenticação.
struct OtaCheck { uint32_t got, size, want, crc; uint8_t field[8]; String err; bool authOk, begun, gz, finished; };
// ---------- modo de recuperação ----------
#define RECOVERY_AP "SmallTV-Recuperacao"

// ---------- estado global (cada variável é definida no módulo dono) ----------
extern Settings cfg;
extern uint8_t bootCount;
extern bool recoveryMode;
extern FsMark fsMark;
extern CrashRec crashRec;
extern TimeGuard timeGuard;
extern uint32_t bootScreenMs, bootWifiMs, bootTimeMs, bootNtpMs;
extern uint8_t bootTimeSrc;
extern uint8_t bootWifiPath;
extern uint32_t ntpAskAt;
extern uint8_t ntpTries;
extern bool syncHintOn;
extern bool timeSoft;
extern bool tftReady;
extern bool warmConj;
extern uint8_t warmFrom, warmTo, warmNow, warmPreview;
extern int16_t warmSent;
extern uint32_t warmPreviewUntil;
extern bool lcdViewActive;
extern const char * PHASE_KEY[];
extern Phase phase;
extern bool running;
extern uint32_t totalMs, remainingMs, lastTick;
extern uint8_t cycleDone;
extern uint32_t alertUntil;
extern uint16_t phaseSeq, shownSeq;
extern String hookWork, hookBreak;
extern bool colorTestRequested;
extern uint8_t previewTheme;
extern uint32_t previewUntil;
extern char netName[16], lanName[12];
extern uint32_t alexaSeen;
extern IPAddress alexaIp;
extern uint8_t alexaLast;
extern uint8_t alexaMin;
extern int16_t sunRise, sunSet;
extern Weather wx;
extern HttpBackoff wxBack;
extern HttpBackoff hookBack;
extern bool fsOk;
extern StatsHead stats;
extern uint16_t statsStreak, statsBest;
extern uint32_t fsInfoAt;
extern String rotList;
extern std::vector<String> album;
extern bool inAlbum;
extern uint32_t viewUntil;
extern String curFile;
extern AnimatedGIF * gif;
extern File packUpload;
extern String packUploadError;
extern bool packFaceReady;
extern int8_t setupQrShown;
extern ESP8266WebServer server;
extern String formToken;
extern uint8_t diagWhere;
extern File uploadFile;
extern File panelUpload;
extern DNSServer dns;
extern bool portalMode;
extern Adafruit_ST7789 tft;
extern uint32_t vcomTestUntil;
extern uint8_t vcomTest;
extern int8_t vcomDraw;
extern uint16_t fullDrawMs;
extern uint16_t pomoDrawMs;
extern uint16_t packLoadMs;  // ms da última carga de pacote de mostrador (modulos/album.cpp); diag.pack

// ---------- funções compartilhadas ----------
void defaultSettings();
void crashLoad();
void setBootCount(uint8_t n);
void safeRestart();
void loadSettings();
void saveSettings();
bool localNow(struct tm &t);
void softTime(int64_t sec, uint8_t src);
void rtcTimeSave();
void rtcTimeRestore();
void applyTimezone(uint8_t rot = 0);
void timeGuardBegin();
int32_t localOffset();
void warmFlush();
void setLcdView(bool lcd);
void briFade();
void updateBrightness(bool force = false);
void startPhase(Phase p, bool run);
void tickPomodoro();
void command(const String &c);
void hooksLoad();
String hookClean(String u);
void hookLoop();
int hookFire(const String &url);
void mdnsStart();
void mdnsLoop();
void alexaApi();
void alexaBegin();
void alexaLoop();
void sendHueDescription();
void appendFixed(String &out, float value, uint8_t digits);
int httpGet(WiFiClient &c, const String &url, uint16_t readMs);
void weatherLoop();
bool fsLayout2();
void fsMount();
void statsCompute();
void sendStats();
void handleStatsImport();
void statsLoad();
void statsFocus();
void statsFlush(const struct tm &t);
void statsRollover(const struct tm &t);
bool fsKeep(const String &p);
void sendStorage();
bool fsRemoveAll(const String &p, int depth);
bool fsWriterBusy();
void statsFlush();
bool isGif(const String &n);
bool isJpg(const String &n);
void rebuildAlbum();
JpgResult validJpg(const String &path);
bool drawJpgAt(const char *file, int x, int y);
bool validGif(const String &path);
void stopAlbum();
bool showAlbumItem(const String &f);
void albumLoop();
void scheduleView();
bool idOk(const String &id);
bool loadPackFile(const String &path, String &msg);
bool loadPack(const String &id);
void restoreActivePack();
void packEnsureLoaded();
void handlePackUpload();
void rotSet(const String &v);
void rotLoop();
void setupUpdate();
void setupQrScreen(bool recovery);
void startRecovery();
void jsonBegin();
void jsonFlush(String &chunk);
void jsonEnd(String &chunk);
void newFormToken();
String formTokenField();
void jsonEscapeAppend(String &o, const char *s);
void diagLoop();
void diagBlkSample();         // amostra pontual do maior bloco livre (diag.minblk): só antes de foto, GIF, pacote e OTA
void diagNoMem(uint8_t what);  // conta uma falta de memória: 0 foto, 1 GIF (diag.nomem)
bool hostOk();
bool panelOk();
bool deny();
void askLogin();
bool authed();
bool uploadAuthed();
bool guard();
bool readOk();
void setupWeb();
void wifiGuardBegin();
void wifiGuardLoop();
bool connectStored(uint32_t timeoutMs);
void wifiHintSave();
void startPortal();
void tftPixels(uint16_t *px, int n);
void tftBitmap(int16_t x, int16_t y, uint16_t *px, int16_t w, int16_t h);
void applyVcom(uint8_t v);
void applyPanel();
String hhmm(const struct tm &t);
void message(const char *a, const char *b = "", const char *c = "", const char *d = "");
void bootAnim(uint8_t done);

// ---------- pequenas, inline ----------
inline bool formOk() { return formToken.length() && server.arg("csrf") == formToken; }
// Idioma das páginas do portal de Wi-Fi e da recuperação (ver pageLang): troca só o uiLang durante a página
uint8_t pageLang();
struct PageLang {
  uint8_t keep = uiLang;
  PageLang() { uiLang = pageLang(); }
  ~PageLang() { uiLang = keep; }
};
inline uint16_t clockColor() { return rgb(cfg.colR, cfg.colG, cfg.colB); }
// ---------- tempo ----------
inline bool timeValid() { return time(nullptr) > 1700000000; }
// só grava o histórico com a hora confirmada: um NTP forjado não apaga nem embaralha os dias
inline bool timeTrusted() { return timeGuard.ok(time(nullptr), millis()); }
inline bool previewActive() { return previewUntil && (int32_t)(millis() - previewUntil) < 0; }
// O clima confirma a hora enquanto estiver configurado e respondendo; sem ele, valem duas sincronizações.
inline bool weatherConfirms() { return cfg.weatherOn && cfg.city[0] && wxBack.fails < 3; }
