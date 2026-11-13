// Brilho no loop: compila modulos/backlight.cpp REAL e o trecho do loop() do main.cpp que o chama (fim da prévia da
// cor quente, updateBrightness a cada 5 s e briFade a cada volta), roda minutos simulados e registra cada
// escrita no pino do backlight (sigma-delta e nível fixo) e cada comando de gama enviado ao ST7789. Pega o "vai e vem"
// do lado do firmware: com o alvo parado, nada pode ser reescrito, e numa troca (dia/noite, sol, prévia, slider) o
// nível que o pino mostra anda num sentido só até o alvo, inclusive na passagem entre o sigma-delta e o nível fixo
// (0 e 100 %), onde uma ordem errada piscaria.
const fs = require('fs'), path = require('path');
const dir = path.join(__dirname, '../smalltv-pomodoro');
const app = fs.readFileSync(path.join(dir, 'app.h'), 'utf8');
const main = fs.readFileSync(path.join(dir, 'main.cpp'), 'utf8');
let bl = fs.readFileSync(path.join(dir, 'modulos/backlight.cpp'), 'utf8');
if (!bl.includes('#include "../app.h"')) throw Error('backlight.cpp sem o include de app.h');
bl = bl.replace('#include "../app.h"', '');
const consts = ['BL_FULL', 'BL_MIN', 'BL_SD_PRESCALER', 'BL_FADE_MS'].map(n => {
  const m = app.match(new RegExp(`const uint(8|16|32)_t ${n} = \\d+;`));
  if (!m) throw Error('não achei ' + n + ' em app.h');
  return m[0];
}).join('\n');
const a = main.indexOf('  uint32_t now = millis();\n  if (warmPreviewUntil');
const b = main.indexOf('  briFade();\n', a);
if (a < 0 || b < 0) throw Error('não achei o trecho do brilho no loop() do main.cpp');
const loopBody = main.slice(a, b + '  briFade();\n'.length);
const out = process.argv[2];
if (!out) throw Error('output.cpp required');
fs.writeFileSync(out, `#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <vector>
#define PROGMEM
#define pgm_read_byte(a) (*(const uint8_t *)(a))
#define HIGH 1
#define LOW 0
// bri_curva.h, warm.h e noite.h embutidos (o .cpp gerado também roda no WSL, longe desta pasta)
${fs.readFileSync(path.join(dir, 'bri_curva.h'), 'utf8')}
${fs.readFileSync(path.join(dir, 'warm.h'), 'utf8')}
${fs.readFileSync(path.join(dir, 'noite.h'), 'utf8')}
#define PIN_BL 5
${consts}
// ---- o que o backlight.cpp usa do resto do firmware ----
struct { uint8_t brightness = 60, nightOn = 0, nightStart = 22, nightEnd = 7, nightBri = 10, nightSun = 0, warm = 0, lcdGreen = 1; } cfg;
int16_t sunRise = -1, sunSet = -1;
bool recoveryMode = false, portalMode = false;  // brilho noturno 0: recuperação e portal sempre acesos
static uint32_t simMs = 0;
static uint32_t daySec0 = 0;  // hora local (segundos do dia) quando simMs = 0
static uint32_t millis() { return simMs; }
static bool localNow(struct tm &t) {
  uint32_t s = (daySec0 + simMs / 1000) % 86400;
  memset(&t, 0, sizeof t);
  t.tm_hour = s / 3600; t.tm_min = s / 60 % 60; t.tm_sec = s % 60;
  return true;
}
// O pino: nível fixo (digitalWrite) ou sigma-delta preso a ele. pwm = o que o pino mostra (aceso em 1/256), a cada mudança
struct PwmW { uint32_t ms; int v; };
static std::vector<PwmW> pwm;
static int gamaCmds = 0, escritas = 0;
static bool sdPreso = false;
static int pinoAlto = 0, sdAlvo = 0;
static void marca(int pin) {
  if (pin != PIN_BL) { puts("pino errado"); exit(1); }
  escritas++;
  int v = sdPreso ? BL_FULL - sdAlvo : pinoAlto ? 0 : BL_FULL;  // invertido: alto = apagado
  if (pwm.empty() || pwm.back().v != v) pwm.push_back({simMs, v});
}
static void digitalWrite(int pin, int v) { pinoAlto = v; marca(pin); }
static void sigmaDeltaWrite(uint8_t, uint8_t t) { sdAlvo = t; marca(PIN_BL); }
static bool sigmaDeltaIsPinAttached(uint8_t) { return sdPreso; }
static void sigmaDeltaAttachPin(uint8_t pin, uint8_t = 0) { sdPreso = true; marca(pin); }
static void sigmaDeltaDetachPin(uint8_t pin) { sdPreso = false; marca(pin); }
struct { void sendCommand(uint8_t, const uint8_t *, uint8_t) { gamaCmds++; } } tft;
struct { void tick(time_t, uint32_t) {} } timeGuard;
static void rtcTimeSave() {}
void updateBrightness(bool force = false);  // protótipos de app.h
void briFade();
void warmFlush();
${bl}
// ---- loop() do main.cpp, só o trecho do brilho ----
static uint32_t lastBriCheck = 0;
static void loopBrilho() {
${loopBody}}

static int falhas = 0;
#define CONFERE(c, ...) do { if (!(c)) { printf("brilho_loop: FALHA [%s] ", cena); printf(__VA_ARGS__); puts(""); falhas++; } } while (0)
static const char *cena = "";
// Avança o tempo com voltas do loop de duração variada (1 a 40 ms, às vezes uma volta longa de 300 ms: GIF, clima)
static void roda(uint32_t ms) {
  uint32_t fim = simMs + ms;
  while (simMs < fim) {
    uint32_t r = rand() % 100;
    simMs += r < 2 ? 300 : 1 + rand() % 40;
    loopBrilho();
  }
}
// Quantas vezes o PWM inverteu o sentido (desce e sobe, ou sobe e desce) nas escritas a partir de i0
static int inversoes(size_t i0) {
  int sent = 0, n = 0;
  for (size_t i = i0 + 1; i < pwm.size(); i++) {
    int d = pwm[i].v - pwm[i - 1].v, s = d > 0 ? 1 : d < 0 ? -1 : 0;
    if (s && sent && s != sent) n++;
    if (s) sent = s;
  }
  return n;
}
// Liga do zero (boot) com a hora local h:m
static void boot(int h, int m) {
  simMs = 0; daySec0 = h * 3600 + m * 60; lastBriCheck = 0;
  curBri = -1; curWarm = -1; blFrom = blTo = blNow = 0; blT0 = 0; blBoot = true;
  warmFrom = warmTo = warmNow = 0; warmSent = -1; warmPreviewUntil = 0; warmConj = false; lcdViewActive = false;
  tftReady = false; sdPreso = false; pinoAlto = 0; sdAlvo = 0;  // pinMode(OUTPUT): baixo = aceso
  updateBrightness(true);  // setup(): antes da tela
  tftReady = true; warmSent = -1; warmFlush();  // applyPanel()
  pwm.clear(); gamaCmds = 0; escritas = 0;
}
static int duty(int b) { return briDuty(b); }  // o que o pino mostra (aceso em 1/256)

int main() {
  srand(1234);
  // 1) Dia, brilho fixo, sem horário noturno: 10 min sem nenhuma escrita
  cena = "dia fixo";
  cfg = {}; cfg.brightness = 60; boot(14, 0);
  roda(600000);
  CONFERE(escritas == 0, "%d escritas no pino", escritas);
  CONFERE(gamaCmds == 0, "%d comandos de gama", gamaCmds);

  // 2) Noite (horário fixo) com cor quente: 10 min sem nenhuma escrita
  cena = "noite fixa + cor quente";
  cfg = {}; cfg.nightOn = 1; cfg.nightStart = 22; cfg.nightEnd = 7; cfg.nightBri = 10; cfg.warm = 60; boot(23, 0);
  roda(600000);
  CONFERE(escritas == 0, "%d escritas no pino", escritas);
  CONFERE(gamaCmds == 0, "%d comandos de gama", gamaCmds);

  // 3) Automático por horário: 21:55 -> 22:05. Uma rampa só, descendo (o pino invertido sobe), e depois nada
  for (int nb : {1, 10, 59, 100}) for (int db : {5, 60, 100}) {
    cena = "automático por horário";
    cfg = {}; cfg.brightness = db; cfg.nightOn = 1; cfg.nightStart = 22; cfg.nightEnd = 7; cfg.nightBri = nb; cfg.warm = 60;
    boot(21, 55);
    roda(600000);
    CONFERE(inversoes(0) == 0, "dia %d noite %d: %d inversões", db, nb, inversoes(0));
    CONFERE(duty(db) == duty(nb) ? pwm.empty() : !pwm.empty() && pwm.back().v == duty(nb), "dia %d noite %d: termina em %d", db, nb, pwm.empty() ? -1 : pwm.back().v);
    if (!pwm.empty()) CONFERE(pwm.back().ms - pwm.front().ms <= BL_FADE_MS + 300, "rampa de %u ms", pwm.back().ms - pwm.front().ms);
    for (auto &w : pwm) CONFERE(w.ms >= 5 * 60000 && w.ms < 5 * 60000 + 5000 + BL_FADE_MS + 300, "escrita fora da troca em %u ms", w.ms);
  }

  // 4) Automático pelo sol: pôr do sol às 17:30, de 17:25 a 17:45
  cena = "automático pelo sol";
  cfg = {}; cfg.brightness = 80; cfg.nightOn = 1; cfg.nightSun = 1; cfg.nightBri = 20; cfg.warm = 100;
  sunRise = 6 * 60 + 10; sunSet = 17 * 60 + 30;
  boot(17, 25);
  roda(20 * 60000);
  CONFERE(inversoes(0) == 0 && !pwm.empty() && pwm.back().v == duty(20), "inversões %d", inversoes(0));
  int gamaTroca = gamaCmds;
  CONFERE(gamaTroca > 0, "a cor quente não entrou");
  pwm.clear(); gamaCmds = 0; escritas = 0;
  roda(30 * 60000);  // meia hora depois: parado
  CONFERE(escritas == 0 && gamaCmds == 0, "%d escritas, %d gama depois da troca", escritas, gamaCmds);
  sunRise = sunSet = -1;

  // 5) Prévia da cor quente pelo painel (/api/warm): entra, dura 15 s, sai; uma ida e uma volta, sem repetir
  cena = "prévia da cor quente";
  cfg = {}; cfg.brightness = 70; cfg.nightOn = 1; cfg.nightStart = 22; cfg.nightEnd = 7; cfg.nightBri = 15; cfg.warm = 50;
  boot(15, 0);
  warmPreview = 80; warmPreviewUntil = (millis() + 15000) | 1; updateBrightness(true);  // o que o handler faz
  roda(120000);
  CONFERE(inversoes(0) == 1, "%d inversões (esperado 1: ida e volta)", inversoes(0));
  CONFERE(!pwm.empty() && pwm.back().v == duty(70) && pwm.back().ms < 15000 + BL_FADE_MS + 300, "não voltou ao brilho do dia na hora");

  // 6) Salvar os ajustes sem mudar o brilho (updateBrightness(true) a cada 2 s, como o /save): nada reescrito
  cena = "salvar sem mudar";
  cfg = {}; cfg.brightness = 45; boot(10, 0);
  for (int i = 0; i < 60; i++) { roda(2000); updateBrightness(true); }
  CONFERE(escritas == 0 && gamaCmds == 0, "%d escritas, %d gama", escritas, gamaCmds);

  // 7) Slider arrastado ida e volta (60 -> 20 -> 60): no máximo uma inversão, termina no valor pedido e para
  cena = "slider";
  cfg = {}; cfg.brightness = 60; boot(10, 0);
  cfg.brightness = 20; updateBrightness(true); roda(1000);
  cfg.brightness = 60; updateBrightness(true); roda(1000);
  CONFERE(inversoes(0) == 1 && pwm.back().v == duty(60), "%d inversões", inversoes(0));
  int n = escritas; roda(300000);
  CONFERE(escritas == n, "%d escritas depois de parar", escritas - n);

  // 7b) Passagem pelo 100 % (nível fixo) e de volta ao sigma-delta: 90 -> 100 -> 90, 5 -> 100 -> 5, 99 -> 100 -> 99
  for (int lo : {90, 5, 99}) {
    cena = "slider passando por 100 %";
    cfg = {}; cfg.brightness = (uint8_t)lo; boot(10, 0);
    cfg.brightness = 100; updateBrightness(true); roda(1000);
    CONFERE(!pwm.empty() && pwm.back().v == BL_FULL && !sdPreso && !pinoAlto, "%d -> 100: não ficou em nível fixo aceso", lo);
    cfg.brightness = (uint8_t)lo; updateBrightness(true); roda(1000);
    CONFERE(inversoes(0) == 1 && pwm.back().v == duty(lo) && sdPreso, "%d -> 100 -> %d: %d inversões, termina em %d", lo, lo, inversoes(0), pwm.back().v);
    for (auto &w : pwm) CONFERE(w.v >= duty(lo) && w.v <= BL_FULL, "%d: nível %d fora da rampa", lo, w.v);
  }
  // 7c) Boot em 100 %: o pino fica em nível fixo, sem sigma-delta
  cena = "boot em 100 %";
  cfg = {}; cfg.brightness = 100; boot(10, 0); roda(60000);
  CONFERE(!sdPreso && !pinoAlto && escritas == 0, "sd %d alto %d escritas %d", sdPreso, pinoAlto, escritas);
  // 7d) 0 % (hoje o painel não chega lá, mas blOut cobre): 50 -> 0 -> 100 -> 0 entra e sai do nível fixo sem piscar
  cena = "passagem por 0 %";
  cfg = {}; cfg.brightness = 50; boot(10, 0);
  for (int b : {0, 100, 0}) { cfg.brightness = (uint8_t)b; updateBrightness(true); roda(1000); }
  CONFERE(inversoes(0) == 2 && pwm.back().v == 0 && !sdPreso && pinoAlto, "%d inversões, termina em %d", inversoes(0), pwm.back().v);

  // 7e) Brilho noturno 0 (tela apagada): nível fixo apagado; um aviso acende pelo sigma-delta no mínimo útil e, passado o
  // tempo, volta ao nível fixo apagado (sem ficar preso ao sigma-delta)
  cena = "noite 0 e aviso";
  cfg = {}; cfg.brightness = 60; cfg.nightOn = 1; cfg.nightStart = 22; cfg.nightEnd = 7; cfg.nightBri = 0; boot(23, 0);
  CONFERE(!sdPreso && pinoAlto, "no boot: sd %d alto %d", sdPreso, pinoAlto);
  wakeScreen(10000); roda(2000);
  CONFERE(sdPreso && BL_FULL - sdAlvo == duty(NIGHT_WAKE_BRI), "aceso: sd %d mostra %d (esperado %d)", sdPreso, BL_FULL - sdAlvo, duty(NIGHT_WAKE_BRI));
  roda(70000);
  CONFERE(!sdPreso && pinoAlto && pwm.back().v == 0, "depois do aviso: sd %d alto %d mostra %d", sdPreso, pinoAlto, pwm.back().v);

  // 8) Mostrador LCD com verde ligado: entrar na tela manda o gama uma vez; ficar nela não reenvia
  cena = "verde LCD";
  cfg = {}; cfg.brightness = 60; boot(10, 0);
  setLcdView(true); int g1 = gamaCmds;
  for (int i = 0; i < 100; i++) { setLcdView(true); roda(1000); }
  CONFERE(g1 == 3 && gamaCmds == g1 && escritas == 0, "gama %d -> %d", g1, gamaCmds);
  // 8b) A UI avisa a tela a cada desenho (notifyLcdView em uiRenderClock/uiRenderPomodoro...). Com o verde DESLIGADO,
  // trocar entre mostrador LCD e outra tela 1x por segundo por 10 min, de dia e de noite com cor quente: zero gama
  for (int noite : {0, 1}) {
    cena = "verde desligado, troca de tela";
    cfg = {}; cfg.brightness = 60; cfg.lcdGreen = 0; cfg.nightOn = 1; cfg.nightStart = 22; cfg.nightEnd = 7; cfg.warm = 70;
    boot(noite ? 23 : 12, 0);
    for (int i = 0; i < 600; i++) { setLcdView(i & 1); roda(1000); }
    CONFERE(gamaCmds == 0 && escritas == 0, "%s: %d comandos de gama, %d escritas no pino em 10 min", noite ? "noite" : "dia", gamaCmds, escritas);
  }
  // 8c) Verde LIGADO e a mesma troca: só o que a troca pede (3 comandos para entrar no verde, 1 ou 3 para sair), nada a mais
  cena = "verde ligado, troca de tela";
  cfg = {}; cfg.brightness = 60; cfg.lcdGreen = 1; boot(12, 0);
  for (int i = 0; i < 600; i++) { setLcdView(i & 1); setLcdView(i & 1); roda(1000); }  // dois desenhos por segundo
  CONFERE(gamaCmds == 300 * 3 + 299, "%d comandos de gama (esperado %d: 300 entradas e 299 saídas)", gamaCmds, 300 * 3 + 299);

  if (falhas) { printf("brilho_loop: %d falha(s)\\n", falhas); return 1; }
  puts("brilho_loop: ok (dia, noite, troca por horário e pelo sol, prévia, salvar, slider, passagem por 100 %, noite 0 com aviso e verde LCD: sem vai e vem nem reescrita parada)");
  return 0;
}
`);
