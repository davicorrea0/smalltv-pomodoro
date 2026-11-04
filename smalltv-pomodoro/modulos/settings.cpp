// Ajustes na EEPROM, registro da última queda, contador de inicializações e reinício seguro.
#include "../app.h"

Settings cfg;

void defaultSettings() {
  cfg = Settings{SETTINGS_MAGIC, 25, 5, 15, 4, 80, 0, 0, "<-03>3", "", 0, -1,
                 THEME_MATRIX, 0, 10, 0, 22, 7, 15, 1, 242, 242, 242, 0, 0, "", 0, 1, 0, 8, ACT_READ, "", 0, 0, 0, 1, "", UI_VCOM_DEFAULT, 0, 0, 1, 0, 0, 0, 1};
}

uint8_t bootCount = 0;

bool recoveryMode = false;

FsMark fsMark;

CrashRec crashRec;  // magic != CRASH_MAGIC: nenhuma queda guardada

// roda no travamento (o core chama no lugar do aviso extra): guarda há quanto tempo estava ligado
extern "C" void custom_crash_callback(struct rst_info *, uint32_t, uint32_t) {
  uint32_t v[2] = {CRASH_RTC_TAG, millis() / 1000};
  ESP.rtcUserMemoryWrite(RTC_CRASH_BLOCK, v, sizeof v);
}

void crashLoad() {
  EEPROM.get(CRASH_OFF, crashRec);
  if (crashRec.magic != CRASH_MAGIC) crashRec.magic = 0;
  const rst_info *r = ESP.getResetInfoPtr();
  if (!r || !(r->reason == REASON_WDT_RST || r->reason == REASON_EXCEPTION_RST || r->reason == REASON_SOFT_WDT_RST)) return;
  uint32_t v[2] = {0, 0};
  ESP.rtcUserMemoryRead(RTC_CRASH_BLOCK, v, sizeof v);
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

void safeRestart() {
  if (bootCount) setBootCount(0);
  rtcTimeSave();  // a hora atravessa o reinício (memória RTC): o mostrador já volta com ela
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
  if (cfg.warm > 100) cfg.warm = 0;
  if (cfg.lcdGreen > 1) cfg.lcdGreen = 1;
  cfg.spi80 = 0;  // quem ligou no teste volta aos 40 MHz
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
