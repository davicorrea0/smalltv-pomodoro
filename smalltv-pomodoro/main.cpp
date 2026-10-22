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
#include "src/uzlib/uzlib.h"  // o mesmo descompactador do gerenciador de boot (licença zlib)
extern "C" uint32_t _FS_start;
#include "src/JPEGDEC/JPEGDEC.h"  // cópia da JPEGDEC 1.8.4 com as tabelas de cor geradas só durante a decodificação
#include <AnimatedGIF.h>
#include <new>
#include <vector>
#include <time.h>
#include "ui.h"
#include "web.h"
#include "facepack.h"

void blit(int16_t x, int16_t y, uint16_t *px, int16_t w, int16_t h);  // ui.cpp: recorta para a área suja; fora da tela, cai na faixa

#define FW_VERSION "1.16.1"
#define HOSTNAME   "smalltv"
#define AP_NAME    "SmallTV-Pomodoro"

#define PIN_DC  0
#define PIN_RST 2
#define PIN_BL  5


Adafruit_ST7789 tft(-1, PIN_DC, PIN_RST);
ESP8266WebServer server(80);
bool panelOk();  // proteção do painel (mais abaixo, antes de setupWeb)
bool readOk();

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
};
const uint32_t SETTINGS_MAGIC = 0x504F4D34;     // "POM4"
const uint32_t SETTINGS_MAGIC_V3 = 0x504F4D33;  // "POM3": mesmos campos, sem os do v4
Settings cfg;

void defaultSettings() {
  cfg = Settings{SETTINGS_MAGIC, 25, 5, 15, 4, 80, 0, 0, "<-03>3", "", 0, -1,
                 THEME_MATRIX, 0, 10, 0, 22, 7, 15, 1, 242, 242, 242, 0, 0, "", 0, 1, 0, 8, ACT_READ, "", 0, 0, 0, 1, "", UI_VCOM_DEFAULT, 0};
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
  EEPROM.begin(BOOT_OFF + sizeof(BootCount));
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
int curBri = -1;

int16_t sunRise = -1, sunSet = -1;  // minutos do dia (hora local), da última busca do clima

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
  analogWriteRange(255);
  analogWrite(PIN_BL, 255 - map(b, 0, 100, 0, 255));
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

void startPhase(Phase p, bool run) {
  phase = p;
  totalMs = remainingMs = (uint32_t)phaseMinutes(p) * 60000UL;
  running = run;
  lastTick = millis();
}

void statsFocus();                      // um foco terminou (histórico de focos, mais abaixo)
void statsRollover(const struct tm &t);  // o dia mudou
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
WiFiUDP hueUdp;
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
  String req = server.uri(), body = server.arg("plain");
  if (!cfg.alexa || req.indexOf("api") < 0) {
    server.send(404, "text/plain", "");
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
} wx;
uint32_t wxNext = 0;

// GET em HTTP/1.0 (resposta sem "chunked", fecha no fim) no lugar do HTTPClient (~4 KB).
// Devolve o código HTTP (negativo se não conectar) e deixa o cliente no começo do corpo.
int httpGet(WiFiClient &c, const String &url, uint16_t timeoutMs) {
  if (!url.startsWith("http://")) return -1;
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
  c.print("GET " + path + F(" HTTP/1.0\r\nHost: ") + host + F("\r\nUser-Agent: SmallTV\r\nConnection: close\r\n\r\n"));
  String line = c.readStringUntil('\n');  // "HTTP/1.1 200 OK"
  int sp = line.indexOf(' ');
  int code = sp > 0 ? line.substring(sp + 1).toInt() : 0;
  if (code <= 0) return -2;
  while (c.connected() || c.available()) {  // pula os cabeçalhos até a linha vazia
    line = c.readStringUntil('\n');
    if (line.length() <= 1) break;
  }
  return code;
}

bool fetchWeather() {
  WiFiClient client;
  String url = "http://api.open-meteo.com/v1/forecast?latitude=" + String(cfg.lat, 4) +
               "&longitude=" + String(cfg.lon, 4) +
               "&current=temperature_2m,relative_humidity_2m,weather_code,is_day"
               "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,sunrise,sunset"
               "&timezone=auto&forecast_days=" + String(UI_DAYS);
  if (httpGet(client, url, 5000) != 200) return false;
  JsonDocument filter;
  filter["current"]["temperature_2m"] = true;
  filter["current"]["relative_humidity_2m"] = true;
  filter["current"]["weather_code"] = true;
  filter["current"]["is_day"] = true;
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
  wx.ok = true;
  wx.version++;
  return true;
}

void weatherLoop() {
  if (!cfg.weatherOn || !cfg.city[0] || WiFi.status() != WL_CONNECTED) return;
  if ((int32_t)(millis() - wxNext) < 0) return;
  // a cada 15 min; se falhar, tenta de novo em 2 min
  wxNext = millis() + (fetchWeather() ? 15UL : 2UL) * 60000UL;
}

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
void applyPanel() {
  tft.setRotation(cfg.flip ? 0 : 2);  // Adafruit: 2 = sem espelhamento/offsets 0; 0 = 180° (offset 80 já tratado)
  uint8_t madctl = (cfg.flip ? (ST77XX_MADCTL_MX | ST77XX_MADCTL_MY) : 0) | (cfg.bgr ? 0x08 : 0);
  tft.sendCommand(ST77XX_MADCTL, &madctl, 1);
  tft.invertDisplay(cfg.invert);
  tft.fillScreen(0);
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
  s.wx = {wx.ok, wx.t, wx.tmin, wx.tmax, wx.hum, wx.code, wx.day, wx.version, wx.nd, {}};
  memcpy(s.wx.d, wx.d, sizeof wx.d);
  s.city = cfg.city;
  s.theme = previewActive() ? previewTheme : cfg.theme;
  s.accent = clockColor();
  strlcpy(ipText, WiFi.localIP().toString().c_str(), sizeof ipText);
  s.ip = ipText;
  s.ms = millis();
  s.celebrate = alertUntil != 0;  // fase acabou de terminar: animação de fim no Pomodoro
  return s;
}

void renderClock() {
  UiState s = makeState();
  if (s.timeOk) {
    checkDayRollover(s.now);
    s.today = cfg.todayCount;
  }
  uiRenderClock(s);
}

void renderPomodoro() { uiRenderPomodoro(makeState()); }

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
  File f = LittleFS.open("/stats.bin", stats.magic == STATS_MAGIC ? "r+" : "w+");
  if (!f) return;
  if (stats.magic != STATS_MAGIC || day - stats.lastDay >= STATS_DAYS || day < stats.lastDay) {
    if (stats.magic != STATS_MAGIC) stats = {STATS_MAGIC, day, 0, 0};
    uint8_t zero[64] = {0};
    f.seek(sizeof(StatsHead));
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
  if (f && f.size() == sizeof(StatsHead) + STATS_DAYS) f.read((uint8_t *)&stats, sizeof stats);
  if (f) f.close();
  if (stats.magic != STATS_MAGIC) stats = {0, 0, 0, 0};
  statsCompute();
}

void statsFocus() {
  struct tm t;
  if (!localNow(t)) return;
  stats.total++;
  statsWrite(dayNumber(t), cfg.todayCount > 255 ? 255 : cfg.todayCount);
  statsCompute();
}

void statsRollover(const struct tm &t) {
  statsWrite(dayNumber(t), cfg.todayCount > 255 ? 255 : cfg.todayCount);
  statsCompute();
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

bool isGif(const String &n) { return n.endsWith(".gif") || n.endsWith(".GIF"); }
bool isJpg(const String &n) {
  String l = n;
  l.toLowerCase();
  return l.endsWith(".jpg") || l.endsWith(".jpeg");
}

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
  File *fl = (File *)f->fHandle;
  int32_t n = fl->read(buf, len);
  f->iPos = fl->position();
  return n;
}
int32_t jpgSeek(JPEGFILE *f, int32_t pos) {
  File *fl = (File *)f->fHandle;
  fl->seek(pos);
  f->iPos = fl->position();
  return f->iPos;
}
int jpgDraw(JPEGDRAW *p) {
  if (p->y >= 240) return 0;
  if (!uiClip.on) {
    tft.drawRGBBitmap(p->x, p->y, p->pPixels, p->iWidth, p->iHeight);
    return 1;
  }
  blit(p->x, p->y, p->pPixels, p->iWidth, p->iHeight);  // recorta para a área suja; fora da tela, cai na faixa
  return 1;
}

// Camada "image" dos pacotes: JPEG em /faces/<arquivo>, no ponto (x, y), sem escala.
bool drawJpgAt(const char *file, int x, int y) {
  String path = String("/faces/") + file;
  if (strstr(file, "..") || !LittleFS.exists(path)) return false;
  JPEGDEC *jpg = new (std::nothrow) JPEGDEC();
  if (!jpg) return false;
  bool ok = false;
  if (jpg->open(path.c_str(), mediaOpen, mediaClose, jpgRead, jpgSeek, jpgDraw)) {
    jpg->setPixelType(RGB565_LITTLE_ENDIAN);
    ok = jpg->decode(x, y, 0);
    jpg->close();
  }
  delete jpg;
  return ok;
}

bool drawJpg(const String &f) {
  JPEGDEC *jpg = new (std::nothrow) JPEGDEC();  // ~18 KB: só existe durante a decodificação
  if (!jpg) return false;
  bool ok = false;
  if (jpg->open(f.c_str(), mediaOpen, mediaClose, jpgRead, jpgSeek, jpgDraw)) {
    int w = jpg->getWidth(), h = jpg->getHeight(), sc = 1;
    while (sc < 8 && (w / sc > 240 || h / sc > 240)) sc *= 2;
    int opt = sc == 2 ? JPEG_SCALE_HALF : sc == 4 ? JPEG_SCALE_QUARTER : sc == 8 ? JPEG_SCALE_EIGHTH : 0;
    jpg->setPixelType(RGB565_LITTLE_ENDIAN);
    ok = jpg->decode(max(0, (240 - w / sc) / 2), max(0, (240 - h / sc) / 2), opt);
    jpg->close();
  }
  delete jpg;
  return ok;
}
int32_t gifRead(GIFFILE *f, uint8_t *buf, int32_t len) {
  File *fl = (File *)f->fHandle;
  int32_t n = len;
  if (f->iSize - f->iPos < len) n = f->iSize - f->iPos - 1;  // igual ao exemplo oficial da biblioteca
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
        tft.writePixels(buf, n);
      }
    }
  } else {
    for (int i = 0; i < w; i++) buf[i] = pal[s[i]];
    tft.setAddrWindow(x0, y, w, 1);
    tft.writePixels(buf, w);
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
    gif = new (std::nothrow) AnimatedGIF();  // ~24 KB contíguos de RAM
    if (!gif) {
      uiError(tr("Sem memoria p/ GIF", "No memory for GIF", "Sin memoria p/ GIF"));
      return true;  // mantém a tela de aviso até o próximo item
    }
    gif->begin(GIF_PALETTE_RGB565_LE);
    if (!gif->open(f.c_str(), mediaOpen, mediaClose, gifRead, gifSeek, gifDraw)) {
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
  char *buf = (char *)malloc(n + 1);
  if (!buf) {
    f.close();
    msg = "sem memoria";
    return false;
  }
  f.readBytes(buf, n);
  f.close();
  buf[n] = 0;
  char err[96];
  bool ok = facePackLoad(buf, n, err, sizeof err);
  free(buf);
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

// Recebe "face" (o .json) e, opcional, "preview" (o .png) em arquivos temporários.
void handlePackUpload() {
  HTTPUpload &up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    if (up.name == "face") packUploadError = "";
    if (!panelOk()) {
      packUploadError = "Proibido";
      return;
    }
    if (!fsOk) {
      packUploadError = "Sistema de arquivos indisponivel";
      return;
    }
    if (!LittleFS.exists("/faces")) LittleFS.mkdir("/faces");
    packUpload = LittleFS.open(up.name == "preview" ? "/faces/_novo.png" : "/faces/_novo.json", "w");
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (packUpload && packUpload.write(up.buf, up.currentSize) != up.currentSize) packUploadError = "Memoria cheia";
  } else if (up.status == UPLOAD_FILE_END || up.status == UPLOAD_FILE_ABORTED) {
    if (packUpload) packUpload.close();
  }
}

// ---------- web ----------
String jsonEscape(const String &s) {
  String o;
  for (char c : s) {
    if (c == '"' || c == '\\') o += '\\';
    if ((uint8_t)c >= 0x20) o += c;
  }
  return o;
}

bool safeText(const String &s) { return s.indexOf('"') < 0 && s.indexOf('\\') < 0; }

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
  j.reserve(900);
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
  j += jsonEscape(curFile);
  j += F("\",\"wx\":{\"ok\":");
  j += wx.ok ? 1 : 0;
  j += F(",\"t\":");
  j += String(wx.t, 1);
  j += F(",\"min\":");
  j += String(wx.tmin, 1);
  j += F(",\"max\":");
  j += String(wx.tmax, 1);
  j += F(",\"h\":");
  j += wx.hum;
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
  j += jsonEscape(cfg.tz);
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
  j += F(",\"lat\":");
  j += String(cfg.lat, 4);
  j += F(",\"lon\":");
  j += String(cfg.lon, 4);
  j += F(",\"city\":\"");
  j += jsonEscape(cfg.city);
  j += F("\",\"bgr\":");
  j += cfg.bgr;
  j += F(",\"inv\":");
  j += cfg.invert;
  j += F(",\"flip\":");
  j += cfg.flip;
  j += F(",\"vcom\":");
  j += cfg.vcom;
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
  j += F(",\"heap\":");
  j += ESP.getFreeHeap();
  j += F(",\"blk\":");
  j += ESP.getMaxFreeBlockSize();
  j += F(",\"ver\":\"" FW_VERSION "\"}");
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

void handleUpload() {
  HTTPUpload &up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    uploadError = "";
    uploadPath = "";
    if (!panelOk()) {
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
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (uploadFile && uploadFile.write(up.buf, up.currentSize) != up.currentSize) {
      uploadError = "Memoria cheia";
      uploadFile.close();
      LittleFS.remove(uploadPath);
    }
  } else if (up.status == UPLOAD_FILE_END || up.status == UPLOAD_FILE_ABORTED) {
    if (uploadFile) uploadFile.close();
    if (up.status == UPLOAD_FILE_ABORTED && uploadPath.length()) LittleFS.remove(uploadPath);
    rebuildAlbum();
  }
}

void sendFiles() {
  if (!readOk()) return;
  String j = "{\"files\":[";
  bool first = true;
  for (const String &n : album) {
    File f = LittleFS.open(n, "r");
    if (!first) j += ',';
    first = false;
    j += "{\"n\":\"" + jsonEscape(n) + "\",\"s\":" + String(f ? f.size() : 0) + "}";
    if (f) f.close();
  }
  j += "]}";
  server.send(200, "application/json", j);
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
  for (char c : h)
    if (!((c >= '0' && c <= '9') || c == '.' || c == ':')) return false;
  return h.length() > 0;
}
bool panelOk() { return hostOk() && server.header("X-SmallTV") == "1"; }
bool deny() {
  server.send(403, "text/plain", "Proibido");
  return false;
}
// Senha do painel (a mesma do /update, usuário admin): com ela definida, todo o painel e a API pedem
// login HTTP (o navegador pergunta uma vez e lembra). Cada senha errada espera 1 s, o que deixa
// tentativas em série lentas demais. A API da Alexa e o modo de recuperação não pedem senha.
// O login Basic é conferido aqui: o authenticate() do servidor traz junto o modo Digest e o MD5 (~2,5 KB).
bool passOk() {
  return !cfg.otaPass[0] || server.header("Authorization") == "Basic " + base64::encode(String("admin:") + cfg.otaPass, false);
}
void askLogin() {
  server.sendHeader(F("WWW-Authenticate"), F("Basic realm=\"SmallTV\""));
  server.send(401, "text/plain", "");
}
bool authed() {
  if (passOk()) return true;
  if (server.hasHeader("Authorization")) delay(1000);  // tentou e errou
  askLogin();
  return false;
}
bool guard() { return (panelOk() || deny()) && authed(); }   // ações (POST)
bool readOk() { return (hostOk() || deny()) && authed(); }   // leituras

void setupWeb() {
  server.collectHeaders("X-SmallTV", "Content-Length", "Authorization");
  // painel comprimido (web/index.html -> web/build.js -> web.h); o navegador descompacta
  server.on("/", HTTP_GET, [] {
    if (!readOk()) return;  // com senha, o navegador pede o login já ao abrir o painel
    server.sendHeader("Content-Encoding", "gzip");
    server.sendHeader("Cache-Control", "no-cache");  // depois de atualizar o firmware, o navegador busca o painel novo
    server.send_P(200, "text/html", (PGM_P)INDEX_HTML_GZ, sizeof(INDEX_HTML_GZ));
  });
  server.on("/api/state", HTTP_GET, sendState);
  server.on("/api/stats", HTTP_GET, sendStats);
  // avisos de fim de fase: GET lê, POST (w, b) grava, POST test=w|b chama o endereço e devolve o código
  server.on("/api/hooks", HTTP_GET, [] {
    if (!guard()) return;  // os links levam o código da conta do serviço
    server.send(200, "application/json", "{\"w\":\"" + hookWork + "\",\"b\":\"" + hookBreak + "\"}");
  });
  server.on("/api/hooks", HTTP_POST, [] {
    if (!guard()) return;
    if (server.hasArg("test")) {
      int code = hookFire(server.arg("test") == "b" ? hookBreak : hookWork);
      server.send(200, "text/plain", String(code));
      return;
    }
    hookWork = hookClean(server.arg("w"));
    hookBreak = hookClean(server.arg("b"));
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
    String d = server.arg("days");
    uint16_t day = server.arg("day").toInt();
    std::vector<uint8_t> v;
    for (int i = 0; i < (int)d.length();) {
      int e = d.indexOf(',', i);
      if (e < 0) e = d.length();
      int n = d.substring(i, e).toInt();
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
    String j = "[";
    for (uint8_t t = 0; t < THEME_COUNT; t++) {
      if (t) j += ',';
      j += "{\"n\":\"" + jsonEscape(uiFaceName(t)) + "\",\"d\":\"" + jsonEscape(uiFaceDesc(t)) + "\"}";
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
        j += ",{\"n\":\"" + jsonEscape(name) + "\",\"d\":\"" + jsonEscape(desc) + "\",\"a\":\"" + jsonEscape(author) +
             "\",\"pkg\":\"" + id + "\"}";
      }
    }
    j += "]";
    server.send(200, "application/json", j);
  });
  // prévia PNG de um mostrador para a galeria do painel. As dos embutidos ficam no site (geradas pelo
  // simulador, sem ocupar a flash do relógio); as dos pacotes instalados, no LittleFS.
  server.on("/face", HTTP_GET, [] {
    if (server.hasArg("pkg")) {  // prévia de pacote instalado (/faces/<id>.png, se veio junto)
      String id = server.arg("pkg");
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
    String color = server.arg("color");
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
    if (server.hasArg("lat") && server.hasArg("lon") && safeText(server.arg("city"))) {
      cfg.lat = parseDec(server.arg("lat"));
      cfg.lon = parseDec(server.arg("lon"));
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
    uiInvalidate();     // redesenha com a cor/tema novos
    viewUntil = 0;      // reavalia relógio/álbum com o novo modo
    sendState();
  });
  server.on("/api/files", HTTP_GET, sendFiles);
  // instala um pacote de mostrador: valida no motor, usa o "id" do JSON como nome do arquivo
  server.on("/api/faceinstall", HTTP_POST, [] {
    if (!guard()) return;
    String msg;
    bool ok = !packUploadError.length() && loadPackFile("/faces/_novo.json", msg);
    String id = ok ? facePackId() : "", name = ok ? facePackName() : "";
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
    server.send(200, "application/json", "{\"ok\":1,\"id\":\"" + id + "\",\"name\":\"" + jsonEscape(name) + "\"}");
  }, handlePackUpload);
  server.on("/api/facedelete", HTTP_POST, [] {
    if (!guard()) return;
    String id = server.arg("pkg");
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
    String f = server.arg("f");
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
  server.on("/api/show", HTTP_POST, [] {
    if (!guard()) return;
    String f = server.arg("f");
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
    String f = server.arg("f");
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
    String p = server.arg("new");
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
struct OtaCheck { uint32_t got, size, want, crc; uint8_t field[8]; String err; bool authOk, begun, gz; } ota;

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
  } else ota.err = F("sem memoria para conferir");
  free(dict);
  free(gzBuf);
  free(d);
  if (ota.err.length()) eboot_command_clear();  // cancela a troca: o firmware atual continua
  return !ota.err.length();
}

void setupUpdate() {
  auto authOk = [] {
    if (recoveryMode || (panelOk() && passOk())) return true;
    if (server.hasHeader("Authorization")) delay(1000);
    return false;
  };
  server.on("/update", HTTP_GET, [] {
    if (!recoveryMode && !passOk()) return askLogin();
    String p = F("<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'><title>SmallTV</title>"
                 "<style>body{font:16px system-ui;background:#111;color:#eee;max-width:420px;margin:auto;padding:20px}"
                 "input,button{width:100%;padding:12px;margin:8px 0;border-radius:10px;border:1px solid #333;background:#1c1c1c;color:#eee;font:inherit}"
                 "button{background:#ff5a47;border:0;font-weight:700}</style><h2>");
    p += tr("Atualizar firmware", "Update firmware", "Actualizar firmware");
    p += F("</h2><form method=post enctype=multipart/form-data><input type=file name=firmware accept=.bin,.gz required><button>");
    p += tr("Enviar", "Upload", "Enviar");
    p += F("</button></form><small>v" FW_VERSION "</small>"
           "<script>document.forms[0].onsubmit=e=>{e.preventDefault();document.body.innerHTML='...';"
           "fetch('/update',{method:'POST',body:new FormData(e.target),headers:{'X-SmallTV':'1'}}).then(r=>r.text()).then(t=>document.body.innerHTML=t)}</script>");
    server.send(200, "text/html", p);
  });
  server.on("/update", HTTP_POST, [] {
      if (!ota.authOk) return askLogin();
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
        ota = {0, 0, 0, 0xFFFFFFFF, {0}, String(), authOk(), false, false};
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
        if (!ota.err.length() && Update.write(up.buf, up.currentSize) != up.currentSize) ota.err = Update.getErrorString();
        if (ota.err.length()) Update.end();  // desiste: o firmware atual continua
      } else if (up.status == UPLOAD_FILE_END && !ota.err.length()) {
        if (!ota.begun) ota.err = F("arquivo vazio");
        else if (ota.gz) {
          if (!Update.end(true)) ota.err = Update.getErrorString();
          else otaVerifyGz();
        } else if (ota.got < 4120 || ota.got != ota.size) ota.err = F("arquivo incompleto");
        else if (ota.crc != ota.want) ota.err = F("arquivo corrompido (CRC)");
        if (ota.gz) {}
        else if (ota.err.length()) Update.end();
        else if (!Update.end(true)) ota.err = Update.getErrorString();
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
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(RECOVERY_AP, cfg.apPass);
  WiFi.begin();  // tenta também a rede salva: o painel de recuperação abre pelo IP de casa
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
    p += F("</a><form method=post action=/rec/factory><button>");
    p += tr("Voltar aos ajustes de fabrica", "Reset settings to defaults", "Restablecer ajustes");
    p += F("</button></form><form method=post action=/rec/wifi><button>");
    p += tr("Esquecer o Wi-Fi", "Forget Wi-Fi", "Olvidar el Wi-Fi");
    p += F("</button></form><form method=post action=/rec/restart><button class=p>");
    p += tr("Reiniciar normalmente", "Restart normally", "Reiniciar normalmente");
    p += F("</button></form>");
    server.send(200, "text/html", p);
  });
  auto done = [](const __FlashStringHelper *msg) {
    server.sendHeader("Location", "/");
    server.send(303, "text/plain", msg);
  };
  server.on("/rec/factory", HTTP_POST, [done] {
    uint8_t lang = cfg.lang;
    defaultSettings();
    cfg.lang = lang;
    saveSettings();
    done(F("ok"));
  });
  server.on("/rec/wifi", HTTP_POST, [done] {
    WiFi.persistent(true);
    WiFi.disconnect(true);
    done(F("ok"));
  });
  server.on("/rec/restart", HTTP_POST, [] {
    server.send(200, "text/plain", "ok");
    delay(300);
    safeRestart();
  });
  server.on("/api/stats", HTTP_GET, sendStats);
  server.collectHeaders("Content-Length");  // /update com .bin.gz
  setupUpdate();
  server.begin();
}

void rotSet(const String &v) {
  String s;
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
    uiInvalidate();
    return;
  }
}

// ---------- avisos de fim de fase (Alexa e afins) ----------
// Um endereço por evento em /hooks.txt (fim do foco, fim da pausa). Quando a fase termina o relógio
// chama o endereço (GET), e o serviço dispara uma rotina: a Virtual Smart Home e o Voice Monkey fazem a
// Alexa falar, o IFTTT liga a centenas de serviços. Vai sem criptografia (http): os três aceitam, e
// TLS não cabe no relógio. Um "https://" colado no painel vira "http://".

void hooksLoad() {
  File f = fsOk ? LittleFS.open("/hooks.txt", "r") : File();
  if (!f) return;
  hookWork = f.readStringUntil('\n');
  hookBreak = f.readStringUntil('\n');
  f.close();
  hookWork.trim();
  hookBreak.trim();
}

String hookClean(String u) {
  u.trim();
  if (u.startsWith("https://")) u = "http://" + u.substring(8);
  if (!u.startsWith("http://") || u.length() > 400 || u.indexOf('"') >= 0 || u.indexOf('\\') >= 0 || u.indexOf(' ') >= 0) return "";
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

bool connectStored(uint32_t timeoutMs) {
  WiFi.mode(WIFI_STA);
  WiFi.hostname(HOSTNAME);
  WiFi.begin();
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeoutMs) delay(100);
  return WiFi.status() == WL_CONNECTED;
}

String htmlEscape(const String &s) {
  String o;
  for (char c : s) {
    if (c == '<') o += "&lt;";
    else if (c == '>') o += "&gt;";
    else if (c == '&') o += "&amp;";
    else if (c == '"') o += "&quot;";
    else o += c;
  }
  return o;
}

void startPortal() {
  portalMode = true;
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_NAME, cfg.apPass);
  dns.start(53, "*", WiFi.softAPIP());
  String pw = String(tr("senha: ", "password: ", "clave: ")) + cfg.apPass;
  message(tr("Configure o Wi-Fi", "Set up Wi-Fi", "Configura el Wi-Fi"), AP_NAME, pw.c_str(), "http://192.168.4.1");
  server.on("/", HTTP_GET, [] {
    int n = WiFi.scanNetworks();
    String opts;
    for (int i = 0; i < n; i++) opts += "<option>" + htmlEscape(WiFi.SSID(i)) + "</option>";
    String p = F("<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
                 "<title>SmallTV Wi-Fi</title><style>body{font:16px system-ui;background:#111;color:#eee;max-width:380px;margin:auto;padding:20px}"
                 "input,button{width:100%;padding:12px;margin:6px 0 14px;border-radius:10px;border:1px solid #333;background:#1c1c1c;color:#eee;font:inherit}"
                 "button{background:#ff5a47;border:0;font-weight:700}</style><h2>SmallTV Wi-Fi</h2><form method=post action=/save>"
                 );
    p += tr("Rede", "Network", "Red");
    p += F("<input name=s list=l required><datalist id=l>");
    p += opts;
    p += F("</datalist>");
    p += tr("Senha", "Password", "Contrase&ntilde;a");
    p += F("<input name=p type=password><button>");
    p += tr("Conectar", "Connect", "Conectar");
    p += F("</button></form>");
    server.send(200, "text/html", p);
  });
  server.on("/save", HTTP_POST, [] {
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
  pinMode(PIN_BL, OUTPUT);
  loadSettings();
  BootCount bc;
  EEPROM.get(BOOT_OFF, bc);
  uint8_t boots = bc.magic == BOOT_MAGIC ? bc.n : 0;
  recoveryMode = boots + 1 >= 3;
  setBootCount(recoveryMode ? 0 : boots + 1);  // a recuperação vale uma vez; o próximo reinício é normal
  updateBrightness(true);

  tft.init(240, 240, SPI_MODE3);
  tft.setSPISpeed(40000000);
  applyVcom(cfg.vcom);
  uiBegin(&tft);
  uiSetLang(cfg.lang);
  // entre os quadros de animação (virada do Flip): cede tempo ao Wi-Fi e dá ritmo à virada
  uiFrameHook = [] { yield(); };  // a animação é guiada pelo relógio: só cede tempo ao Wi-Fi
  uiMillis = [] { return (uint32_t)millis(); };
  // linhas de pixels pelo caminho rápido do ST7789 (drawRGBBitmap não é virtual no Adafruit_GFX)
  uiBlit = [](int16_t x, int16_t y, uint16_t *px, int16_t w, int16_t h) { tft.drawRGBBitmap(x, y, px, w, h); };
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
  if (!connectStored(20000)) {
    startPortal();
    return;
  }
  applyTimezone();
  setupWeb();
  alexaBegin();
}

uint32_t lastRender = 0, lastBriCheck = 0;

void loop() {
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
  }

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
  else if (!inAlbum) renderClock();
}
