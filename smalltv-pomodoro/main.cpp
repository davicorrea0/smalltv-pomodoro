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
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266HTTPClient.h>
#include <DNSServer.h>
#include <EEPROM.h>
#include <LittleFS.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <ArduinoJson.h>
#include <JPEGDEC.h>
#include <AnimatedGIF.h>
#include <new>
#include <vector>
#include <time.h>
#include "ui.h"
#include "web.h"

#define FW_VERSION "1.3.4"
#define HOSTNAME   "smalltv"
#define AP_NAME    "SmallTV-Pomodoro"

#define PIN_DC  0
#define PIN_RST 2
#define PIN_BL  5


Adafruit_ST7789 tft(-1, PIN_DC, PIN_RST);
ESP8266WebServer server(80);
ESP8266HTTPUpdateServer updater;

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
};
const uint32_t SETTINGS_MAGIC = 0x504F4D34;     // "POM4"
const uint32_t SETTINGS_MAGIC_V3 = 0x504F4D33;  // "POM3": mesmos campos, sem os do v4
Settings cfg;

void loadSettings() {
  EEPROM.begin(sizeof(Settings));
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
    cfg.magic = SETTINGS_MAGIC;
  } else if (cfg.magic != SETTINGS_MAGIC) {
    cfg = Settings{SETTINGS_MAGIC, 25, 5, 15, 4, 80, 0, 0, "<-03>3", "", 0, -1,
                   THEME_MATRIX, 0, 10, 0, 22, 7, 15, 1, 242, 242, 242, 0, 0, "", 0, 1, 0, 8};
  }
  if (cfg.theme >= THEME_COUNT) cfg.theme = THEME_MATRIX;
  if (!cfg.dailyGoal) cfg.dailyGoal = 8;
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

void updateBrightness(bool force = false) {
  int b = cfg.brightness;
  struct tm t;
  if (cfg.nightOn && cfg.nightStart != cfg.nightEnd && localNow(t)) {
    int h = t.tm_hour, s = cfg.nightStart, e = cfg.nightEnd;
    bool night = s < e ? (h >= s && h < e) : (h >= s || h < e);
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

void advance(bool completed) {
  if (phase == WORK) {
    if (completed) {
      cycleDone++;
      cfg.todayCount++;
      saveSettings();
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

// ---------- clima (Open-Meteo, sem chave de API) ----------
struct Weather {
  bool ok = false;
  float t = 0, tmin = 0, tmax = 0;
  int hum = 0, code = 0;
  bool day = true;
  uint16_t version = 0;
} wx;
uint32_t wxNext = 0;

bool fetchWeather() {
  WiFiClient client;
  HTTPClient http;
  String url = "http://api.open-meteo.com/v1/forecast?latitude=" + String(cfg.lat, 4) +
               "&longitude=" + String(cfg.lon, 4) +
               "&current=temperature_2m,relative_humidity_2m,weather_code,is_day"
               "&daily=temperature_2m_max,temperature_2m_min&timezone=auto&forecast_days=1";
  if (!http.begin(client, url)) return false;
  http.useHTTP10(true);
  http.setTimeout(5000);
  if (http.GET() != 200) {
    http.end();
    return false;
  }
  JsonDocument filter;
  filter["current"]["temperature_2m"] = true;
  filter["current"]["relative_humidity_2m"] = true;
  filter["current"]["weather_code"] = true;
  filter["current"]["is_day"] = true;
  filter["daily"]["temperature_2m_max"] = true;
  filter["daily"]["temperature_2m_min"] = true;
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  if (err || doc["current"]["temperature_2m"].isNull()) return false;
  wx.t = doc["current"]["temperature_2m"];
  wx.hum = doc["current"]["relative_humidity_2m"];
  wx.code = doc["current"]["weather_code"];
  wx.day = doc["current"]["is_day"] | 1;
  wx.tmax = doc["daily"]["temperature_2m_max"][0];
  wx.tmin = doc["daily"]["temperature_2m_min"][0];
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

const char *weatherText(int c) {
  if (c == 0) return "Ceu limpo";
  if (c <= 2) return "Poucas nuvens";
  if (c == 3) return "Nublado";
  if (c == 45 || c == 48) return "Neblina";
  if (c >= 51 && c <= 57) return "Garoa";
  if ((c >= 61 && c <= 67) || (c >= 80 && c <= 82)) return "Chuva";
  if ((c >= 71 && c <= 77) || c == 85 || c == 86) return "Neve";
  if (c >= 95) return "Tempestade";
  return "";
}

// ---------- tela (desenho em ui.cpp) ----------
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
  s.weatherOn = cfg.weatherOn && cfg.city[0];
  s.wx = {wx.ok, wx.t, wx.tmin, wx.tmax, wx.hum, wx.code, wx.day, wx.version};
  s.city = cfg.city;
  s.theme = cfg.theme;
  s.accent = clockColor();
  strlcpy(ipText, WiFi.localIP().toString().c_str(), sizeof ipText);
  s.ip = ipText;
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

// ---------- álbum: fotos (JPG) e GIFs no LittleFS ----------
bool fsOk = false;
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

void rebuildAlbum() {
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
  tft.drawRGBBitmap(p->x, p->y, p->pPixels, p->iWidth, p->iHeight);
  return 1;
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
      uiError("Sem memoria p/ GIF");
      return true;  // mantém a tela de aviso até o próximo item
    }
    gif->begin(GIF_PALETTE_RGB565_LE);
    if (!gif->open(f.c_str(), mediaOpen, mediaClose, gifRead, gifSeek, gifDraw)) {
      delete gif;
      gif = nullptr;
      uiError("GIF invalido");
      return false;
    }
    gifOffX = max(0, (240 - gif->getCanvasWidth()) / 2);
    gifOffY = max(0, (240 - gif->getCanvasHeight()) / 2);
    nextFrameAt = 0;
  } else {
    if (!drawJpg(f)) uiError("JPG invalido");
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
  struct tm t;
  String clock = localNow(t) ? hhmm(t) : "--:--";
  char color[8];
  snprintf(color, sizeof(color), "#%02x%02x%02x", cfg.colR, cfg.colG, cfg.colB);
  FSInfo fi = {};
  if (fsOk) LittleFS.info(fi);
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
  j += F(",\"city\":\"");
  j += jsonEscape(cfg.city);
  j += F("\",\"bgr\":");
  j += cfg.bgr;
  j += F(",\"inv\":");
  j += cfg.invert;
  j += F(",\"flip\":");
  j += cfg.flip;
  j += F(",\"goal\":");
  j += cfg.dailyGoal;
  j += F("},\"heap\":");
  j += ESP.getFreeHeap();
  j += F(",\"blk\":");
  j += ESP.getMaxFreeBlockSize();
  j += F(",\"ver\":\"" FW_VERSION "\"}");
  server.send(200, "application/json", j);
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

void setupWeb() {
  server.on("/", HTTP_GET, [] { server.send_P(200, "text/html", INDEX_HTML); });
  server.on("/api/state", HTTP_GET, sendState);
  server.on("/api/cmd", HTTP_POST, [] {
    command(server.arg("c"));
    sendState();
  });
  server.on("/api/settings", HTTP_POST, [] {
    cfg.work = argRange("work", cfg.work, 1, 99);
    cfg.shortBreak = argRange("short", cfg.shortBreak, 1, 99);
    cfg.longBreak = argRange("long", cfg.longBreak, 1, 99);
    cfg.longEvery = argRange("every", cfg.longEvery, 1, 8);
    cfg.brightness = argRange("bri", cfg.brightness, 5, 100);  // mínimo 5: com 0 a tela apagaria
    cfg.autoStart = argRange("auto", cfg.autoStart, 0, 1);
    cfg.hour12 = argRange("h12", cfg.hour12, 0, 1);
    cfg.theme = argRange("theme", cfg.theme, 0, THEME_COUNT - 1);
    cfg.dailyGoal = argRange("goal", cfg.dailyGoal, 1, 30);
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
      cfg.lat = server.arg("lat").toFloat();
      cfg.lon = server.arg("lon").toFloat();
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
  server.on("/api/upload", HTTP_POST, [] {
    if (uploadError.length()) server.send(400, "text/plain", uploadError);
    else server.send(200, "text/plain", "ok");
  }, handleUpload);
  server.on("/api/delete", HTTP_POST, [] {
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
    stopAlbum();
    LittleFS.end();
    fsOk = LittleFS.format() && LittleFS.begin();
    if (fsOk) LittleFS.mkdir("/image");
    rebuildAlbum();
    server.send(fsOk ? 200 : 500, "text/plain", fsOk ? "ok" : "falhou");
  });
  // Arquivos de mídia para as miniaturas do painel
  server.on("/fs", HTTP_GET, [] {
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
    updater.updateCredentials("admin", cfg.otaPass);
    server.send(200, "text/plain", p.length() ? "Senha definida (usuario: admin)" : "Senha removida");
  });
  server.on("/api/wifireset", HTTP_POST, [] {
    server.send(200, "text/plain", "ok");
    delay(300);
    WiFi.persistent(true);
    WiFi.disconnect(true);  // com persistent, apaga a rede salva
    ESP.restart();
  });
  // /update aceita qualquer .bin, inclusive o firmware original da GeekMagic (para voltar ao de fábrica).
  // Usuário "admin"; senha vazia desativa a autenticação.
  updater.setup(&server, "/update", "admin", cfg.otaPass);
  server.begin();
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
  WiFi.softAP(AP_NAME);
  dns.start(53, "*", WiFi.softAPIP());
  message("Configure o Wi-Fi", AP_NAME, "Conecte nessa rede e abra", "http://192.168.4.1");
  server.on("/", HTTP_GET, [] {
    int n = WiFi.scanNetworks();
    String opts;
    for (int i = 0; i < n; i++) opts += "<option>" + htmlEscape(WiFi.SSID(i)) + "</option>";
    String p = F("<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
                 "<title>SmallTV Wi-Fi</title><style>body{font:16px system-ui;background:#111;color:#eee;max-width:380px;margin:auto;padding:20px}"
                 "input,button{width:100%;padding:12px;margin:6px 0 14px;border-radius:10px;border:1px solid #333;background:#1c1c1c;color:#eee;font:inherit}"
                 "button{background:#ff5a47;border:0;font-weight:700}</style><h2>SmallTV Wi-Fi</h2><form method=post action=/save>"
                 "Rede<input name=s list=l required><datalist id=l>");
    p += opts;
    p += F("</datalist>Senha<input name=p type=password><button>Conectar</button></form>");
    server.send(200, "text/html", p);
  });
  server.on("/save", HTTP_POST, [] {
    server.send(200, "text/html", F("<meta charset=utf-8><body style='font:16px system-ui;background:#111;color:#eee;padding:20px'>"
                                    "Conectando... O relógio vai reiniciar e mostrar o endereço do painel na tela."));
    delay(500);
    WiFi.persistent(true);
    WiFi.begin(server.arg("s").c_str(), server.arg("p").c_str());
    delay(1000);
    ESP.restart();
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
  updateBrightness(true);

  tft.init(240, 240, SPI_MODE3);
  tft.setSPISpeed(40000000);
  uiBegin(&tft);
  applyPanel();

  // Monta o LittleFS do firmware original SEM formatar: se falhar, os arquivos ficam intactos.
  LittleFSConfig fcfg;
  fcfg.setAutoFormat(false);
  LittleFS.setConfig(fcfg);
  fsOk = LittleFS.begin();
  rebuildAlbum();

  message("SmallTV Pomodoro", "Conectando Wi-Fi", "v" FW_VERSION);

  // Usa a rede salva no ESP8266 (inclusive a que o firmware original já tinha configurado).
  if (!connectStored(20000)) {
    startPortal();
    return;
  }
  applyTimezone();
  setupWeb();
}

uint32_t lastRender = 0, lastBriCheck = 0;

void loop() {
  if (portalMode) {
    dns.processNextRequest();
    server.handleClient();
    // sem configuração em 5 min (ex.: roteador estava desligado): reinicia e tenta a rede salva de novo
    if (millis() > 300000) ESP.restart();
    return;
  }
  server.handleClient();
  tickPomodoro();
  weatherLoop();

  uint32_t now = millis();
  if (now - lastBriCheck > 5000) {
    lastBriCheck = now;
    updateBrightness();
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

  if (phase == IDLE) {
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
