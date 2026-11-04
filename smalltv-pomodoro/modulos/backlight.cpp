// Brilho (PWM), rampa suave e cor quente/modo LCD verde.
#include "../app.h"

static int curBri = -1;

static int curWarm = -1;

bool tftReady = false;  // a tela já foi iniciada (o brilho é ajustado antes, no começo do setup)

static uint16_t blFrom = 0, blTo = 0, blNow = 0;

static uint32_t blT0 = 0;

static bool blBoot = true;  // primeira escrita (boot): sem rampa

static uint16_t briDuty(int b) {
  if (b <= 0) return 0;
  return BL_MIN + (uint16_t)((BL_RANGE - BL_MIN) * powf(b / 100.0f, 2.2f) + 0.5f);
}

// Cor quente à noite (warm.h): gama digital do ST7789V, ligado à mesma rampa do brilho. warmNow é a intensidade
// 0..100 no instante; warmSent, a que o chip tem (-1 = desconhecida: depois de applyPanel ou antes de iniciar a tela).
bool warmConj = false;  // só na prévia de comparação (ver warm.h)

uint8_t warmFrom = 0, warmTo = 0, warmNow = 0, warmPreview = 0;

int16_t warmSent = -1;

uint32_t warmPreviewUntil = 0;  // prévia "ver agora" (aparência noturna) até este millis()

bool lcdViewActive = false;  // tela realmente desenhada, não o mostrador salvo para depois do Pomodoro/álbum

static bool lcdGreenNow() {
  return cfg.lcdGreen && lcdViewActive;
}

void warmFlush() {
  uint8_t st = warmState(lcdGreenNow(), warmNow);
  if (st == warmSent) return;
  warmApplyState([](uint8_t c, uint8_t *d, uint8_t n) { tft.sendCommand(c, d, n); }, st, warmConj);
  warmSent = st;
}

void setLcdView(bool lcd) {
  lcdViewActive = lcd;
  if (tftReady) warmFlush();  // antes do primeiro pixel da tela nova; warmSent evita reenvios iguais
}

// Chamada a cada volta do loop: anda da posição atual até o alvo em BL_FADE_MS e só mexe no PWM quando muda
void briFade() {
  uint32_t el = millis() - blT0;
  warmNow = el >= BL_FADE_MS ? warmTo : (uint8_t)((int32_t)warmFrom + ((int32_t)warmTo - (int32_t)warmFrom) * (int32_t)el / (int32_t)BL_FADE_MS);
  if (tftReady) warmFlush();
  uint16_t lvl = el >= BL_FADE_MS ? blTo : blFrom + (int32_t)(blTo - blFrom) * (int32_t)el / (int32_t)BL_FADE_MS;
  if (lvl == blNow) return;
  blNow = lvl;
  analogWrite(PIN_BL, BL_RANGE - lvl);
}

void updateBrightness(bool force) {
  int b = cfg.brightness;
  struct tm t;
  bool night = false, preview = warmPreviewUntil && (int32_t)(millis() - warmPreviewUntil) < 0;
  if (cfg.nightOn && localNow(t)) {
    int h = t.tm_hour, m = h * 60 + t.tm_min, s = cfg.nightStart, e = cfg.nightEnd;
    if (cfg.nightSun && sunRise >= 0 && sunSet > sunRise) night = m < sunRise || m >= sunSet;  // do pôr ao nascer
    else if (s != e) night = s < e ? (h >= s && h < e) : (h >= s || h < e);  // horário fixo (ou sem o sol ainda)
  }
  if (night || preview) b = cfg.nightBri;
  uint8_t wt = warmTarget(night, preview, cfg.warm, warmPreview);
  if (b == curBri && wt == curWarm && !force) return;
  curBri = b;
  curWarm = wt;
  blFrom = blNow;
  warmFrom = warmNow;
  warmTo = wt;
  blTo = briDuty(b);
  blT0 = millis();
  if (blBoot) {  // boot: acende direto no nível pedido
    blBoot = false;
    blFrom = blTo;
    blNow = blTo;
    warmFrom = warmNow = warmTo;
    analogWrite(PIN_BL, BL_RANGE - blTo);
    return;
  }
  briFade();
}
