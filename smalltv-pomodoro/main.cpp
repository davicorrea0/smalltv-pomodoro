// SmallTV Ultra (GeekMagic, ESP8266) - firmware alternativo
// Relógio (3 temas) + clima + álbum de fotos/GIFs + brilho noturno + Pomodoro
//
// Hardware (fonte: devices.esphome.io/devices/geekmagic-ultra):
//   Tela ST7789 240x240, SPI hardware CLK=GPIO14 MOSI=GPIO13, sem CS (SPI modo 3)
//   DC=GPIO0  RST=GPIO2  Backlight=GPIO5 (PWM invertido)
// Flash: layout 4M2M (programa até ~1 MB; LittleFS em 0x200000-0x3FA000). Até a 1.23 o layout era 4M3M, igual
// ao firmware original (LittleFS em 0x100000-0x3FA000, constantes do binário V9.0.50, offset 0x14d00), que
// deixava só ~1 MB para o programa e a cópia da atualização. EEPROM (0x3FB000), RF cal e dados do SDK ficam
// nos mesmos endereços nos dois layouts, então os ajustes sobrevivem; o LittleFS é que muda de lugar: no
// primeiro início em 4M2M ele é formatado (ver fsMount). A mesma fonte ainda compila em 4M3M (LAYOUT=4M3M
// em compilar.sh), para uma versão de transição que só leva o painel novo.


#include "app.h"
// Os módulos ficam em modulos/ (fora da raiz do sketch, o arduino-cli não os compila sozinhos) e entram aqui,
// numa só unidade de compilação: assim o compilador ainda junta as funções pequenas entre eles, e o binário fica
// do tamanho de quando tudo estava neste arquivo. Separados em 11 .cpp, o firmware crescia ~1,8 KB (inlining
// perdido, construtores globais e cópias dos templates do servidor web em cada arquivo). Cada módulo inclui app.h
// e usa dos outros só o que está declarado nele; o que é de um módulo só fica static (nomes repetidos não compilam).
#include "modulos/settings.cpp"
#include "modulos/clock_sync.cpp"
#include "modulos/backlight.cpp"
#include "modulos/pomodoro.cpp"
#include "modulos/net_services.cpp"
#include "modulos/weather.cpp"
#include "modulos/storage_stats.cpp"
#include "modulos/album.cpp"
#include "modulos/ota_update.cpp"
#include "modulos/web_portal.cpp"

Adafruit_ST7789 tft(-1, PIN_DC, PIN_RST);

// Linhas de pixels para a tela. A Adafruit GFX 1.13 manda cada linha pelo SPI.writeBytes, que lê a
// memória de 4 em 4 bytes: com a linha num endereço que não é múltiplo de 4 (texto da fonte vetorial,
// recortes, faixas de largura ímpar) o ESP8266 dá exceção de alinhamento e reinicia (issue #4).
// Toda linha passa por aqui: desalinhada, vai por uma cópia alinhada.
// A cópia alinhada usa uiRowBuf(), o mesmo buffer do gifDraw e do Flip (nunca em uso ao mesmo tempo): quem já
// o entrega alinhado nem é copiado. Um buffer só em vez de três (RAM fixa).
void tftPixels(uint16_t *px, int n) {
  if (!((uintptr_t)px & 3)) {
    tft.writePixels(px, n);
    return;
  }
  while (n > 0) {
    int k = n < 240 ? n : 240;
    memmove(uiRowBuf(), px, k * 2);  // px pode estar dentro do próprio buffer (linha do Flip recortada)
    tft.writePixels(uiRowBuf(), k);
    px += k;
    n -= k;
  }
}

// drawRGBBitmap com o recorte da tela feito antes, para cada linha passar por tftPixels
void tftBitmap(int16_t x, int16_t y, uint16_t *px, int16_t w, int16_t h) {
  int x0 = max<int>(x, 0), y0 = max<int>(y, 0), x1 = min<int>(x + w, tft.width()), y1 = min<int>(y + h, tft.height());
  if (x1 <= x0 || y1 <= y0) return;
  tft.startWrite();
  tft.setAddrWindow(x0, y0, x1 - x0, y1 - y0);
  for (int r = y0; r < y1; r++) tftPixels(px + (r - y) * w + (x0 - x), x1 - x0);
  tft.endWrite();
}

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
  tftReady = true;
  warmSent = -1;  // o gama digital vai de novo (briFade reenvia na hora; no boot, já com a tela iniciada)
  warmFlush();
  uiInvalidate();
}

static void checkDayRollover(const struct tm &t) {
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

static char ipText[16];

static UiState makeState() {
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
  s.utcOff = localOffset();
  s.lat10 = (int16_t)lroundf(cfg.lat * 10);
  s.lon10 = (int16_t)lroundf(cfg.lon * 10);
  s.hasLoc = cfg.city[0];
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

static void renderClock() {
  packEnsureLoaded();  // o álbum ou o OTA podem ter tirado o pacote da RAM (modulos/album.cpp)
  DrawProbe probe;
  UiState s = makeState();
  if (!s.timeOk) {  // sem hora ainda: nunca parece travado
    syncHintOn = true;
    uiRenderClock(s);
    uiSyncHint(s.ms);
    return;
  }
  if (syncHintOn) {  // a hora chegou: apaga o indicador redesenhando o mostrador
    syncHintOn = false;
    uiInvalidate();
  }
  if (s.timeOk && timeTrusted()) {  // virada do dia e histórico só com a hora confirmada
    checkDayRollover(s.now);
    statsFlush(s.now);
    s.today = cfg.todayCount;
  }
  uiRenderClock(s);
}

// Quanto a tela leva para desenhar o Pomodoro inteiro ao entrar (anel e números): o painel segura o tempo
// inicial por esse tanto, para os números dos dois começarem a descer juntos (ver "pdraw" em /api/state).
static bool pomoShown = false;

uint16_t pomoDrawMs = 0;

static void renderPomodoro() {
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

static void renderAlert() {
  // pisca a tela alternando a inversão de cores
  bool flash = (millis() / 250) % 2 == 0;
  tft.invertDisplay(flash ? !cfg.invert : cfg.invert);
}

void message(const char *a, const char *b, const char *c, const char *d) { uiMessage(a, b, c, d); }

// Barra da tela de início: done = etapas concluídas (0 Wi-Fi, 1 hora, 2 pronto); anima a cada ~60 ms
void bootAnim(uint8_t done) {
  static uint32_t last = 0;
  uint32_t now = millis();
  if (now - last < 60) return;
  last = now;
  uiBootProgress(now, done);
}

static uint32_t colorTestUntil = 0;

// ---------- setup / loop ----------
// Depois de conectar, a tela de início fica uns segundos com o endereço do painel; o loop segue normal (painel,
// SNTP, clima) em vez de travar num delay. Ela sai antes se alguém usar o relógio.
static uint32_t bootHoldUntil = 0;

static bool bootTitleTime = false;  // o título já diz "Conectado" (a hora chegou)

static void bootConnectedScreen() {
  String addr = String(lanName[0] ? lanName : netName) + ".local";
  bootTitleTime = timeValid();
  message(bootTitleTime ? tr("Conectado", "Connected", "Conectado") : tr("Acertando a hora", "Setting the time", "Ajustando la hora"),
          addr.c_str(), WiFi.localIP().toString().c_str());
}

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
  rtcTimeRestore();  // reinício por software: a hora de antes já vale (sem confirmar) enquanto o Wi-Fi conecta
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
  uiLcdViewHook = setLcdView;
  uiMillis = [] { return (uint32_t)millis(); };
  // linhas de pixels pelo caminho rápido do ST7789 (drawRGBBitmap não é virtual no Adafruit_GFX)
  uiBlit = tftBitmap;
  uiScrollHw = tftScroll;
  applyPanel();
  // A tela de início aparece já: montar a memória de arquivos, o histórico, o mostrador e a lista de fotos leva
  // alguns segundos, e com a tela preta parecia travado.
  message("SmallTV Pomodoro", tr("Iniciando", "Starting", "Iniciando"), "v" FW_VERSION);
  bootScreenMs = millis() | 1;
  uiBootProgress(millis(), 0);

  fsMount();
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

  uiMessageLine(tr("Conectando Wi-Fi", "Connecting Wi-Fi", "Conectando Wi-Fi"));

  // Usa a rede salva no ESP8266 (inclusive a que o firmware original já tinha configurado).
  wifiGuardBegin();
  timeGuardBegin();
  if (!connectStored(20000)) {
    startPortal();
    return;
  }
  bootWifiMs = millis() | 1;
  wifiHintSave();
  applyTimezone();  // o SNTP já pergunta a hora enquanto o resto sobe
  ntpAskAt = millis();
  setupWeb();
  alexaBegin();
  mdnsStart();
  bootConnectedScreen();
  bootHoldUntil = (millis() + 3000) | 1;  // o endereço fica na tela por 3 s, mas o resto já roda (ver loop)
}

static uint32_t lastRender = 0, lastBriCheck = 0;

void loop() {
  diagLoop();
  if (bootCount && millis() > 30000) setBootCount(0);  // ficou ligado: não foi uma inicialização curta
  if (recoveryMode) {
    server.handleClient();
    setupQrScreen(true);
    return;
  }
  if (portalMode) {
    dns.processNextRequest();
    server.handleClient();
    setupQrScreen(false);
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
  hookLoop();

  uint32_t now = millis();
  if (warmPreviewUntil && (int32_t)(now - warmPreviewUntil) >= 0) {  // fim da prévia: volta ao que vale agora
    warmPreviewUntil = 0;
    if (warmConj) warmSent = -1;
    warmConj = false;
    updateBrightness();
  }
  if (now - lastBriCheck > 5000) {
    lastBriCheck = now;
    updateBrightness();
    timeGuard.tick(time(nullptr), now);
    rtcTimeSave();
  }
  briFade();
  // O SNTP não respondeu: pergunta de novo (com outro servidor na frente) em vez de esperar o lwIP, que leva ~15 s
  if (ntpAskAt && !bootNtpMs && ntpTries < 4 && now - ntpAskAt > 6000 && WiFi.status() == WL_CONNECTED) {
    ntpAskAt = now;
    applyTimezone(++ntpTries);
  }

  if (bootHoldUntil) {
    if ((int32_t)(now - bootHoldUntil) < 0 && phase == IDLE && !previewActive() && !vcomTestUntil && !colorTestRequested) {
      if (timeValid() && !bootTitleTime) bootConnectedScreen();
      bootAnim(timeValid() ? 2 : 1);
      return;
    }
    bootHoldUntil = 0;
    uiInvalidate();
    viewUntil = 0;
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
  else {
    pomoShown = false;  // na próxima entrada a tela do Pomodoro é desenhada inteira de novo
    if (!inAlbum) renderClock();
  }
}
