// Exercita as funções reais de seleção de gama e de entrada no álbum com as telas reais do simulador.
const fs = require('fs'), path = require('path');
const source = fs.readFileSync(path.join(__dirname, '../smalltv-pomodoro/main.cpp'), 'utf8');
function fn(signature) {
  const a = source.indexOf(signature + ' {'), b = source.indexOf('\n}', a);
  if (a < 0 || b < 0) throw Error('Função ausente: ' + signature);
  return source.slice(a, b + 2);
}
fs.writeFileSync(process.argv[2], `
namespace TintTest {
struct Cmd { uint8_t c; std::vector<uint8_t> d; };
static std::vector<Cmd> sent;
static struct { bool lcdGreen = true; int theme = THEME_LCD; } cfg;
static bool lcdViewActive = false, tftReady = true, warmConj = false;
static uint8_t warmNow = 0, previewTheme = THEME_LCD;
static int16_t warmSent = -1;
static bool previewActive() { return false; }
static struct Device {
  void sendCommand(uint8_t c, uint8_t *d, uint8_t n) { sent.push_back({c, std::vector<uint8_t>(d, d + n)}); }
  void fillScreen(uint16_t c) { G->fillScreen(c); }
} tft;
${fn('bool lcdGreenNow()')}
${fn('void warmFlush()')}
${fn('void setLcdView(bool lcd)')}

using String = std::string;
using std::max;
static struct { bool exists(const String &) { return true; } } LittleFS;
static bool inAlbum = false;
static String curFile;
static int gifOffX, gifOffY, nextFrameAt;
static const int GIF_PALETTE_RGB565_LE = 0;
static void *mediaOpen, *mediaClose, *gifRead, *gifSeek, *gifDraw;
struct AnimatedGIF {
  void begin(int) {}
  template<class... Args> bool open(const char *, Args...) { return true; }
  void close() {}
  int getCanvasWidth() { return 240; }
  int getCanvasHeight() { return 240; }
};
static AnimatedGIF *gif = nullptr;
static void stopAlbum() { delete gif; gif = nullptr; inAlbum = false; curFile.clear(); uiInvalidate(); }
static bool isGif(const String &s) { return s.find(".gif") != String::npos; }
static bool validGif(const String &) { return true; }
static bool drawJpg(const String &) { G->drawPixel(0, 0, TEXT); return true; }
${fn('bool showAlbumItem(const String &f)')}

// Verifica também a ordem: o estado do chip deve estar certo ANTES do primeiro pixel de cada tela.
struct TintFrame : Frame {
  int expected = 0, bad = 0;
  void drawPixel(int16_t x, int16_t y, uint16_t c) override {
    if (warmSent != expected) bad++;
    Frame::drawPixel(x, y, c);
  }
};
static int run() {
  int fails = 0;
  auto oldHook = uiLcdViewHook;
  uiLcdViewHook = setLcdView;
  TintFrame *f = new TintFrame();
  uiBegin(f);
  for (int night = 0; night < 2; night++) for (int enabled = 0; enabled < 2; enabled++) {
    cfg.lcdGreen = enabled;
    warmNow = night ? 70 : 0;
    int normal = warmNow, lcd = enabled ? WARM_GREEN : normal;
    UiState s = base(14, 37, 24);
    s.theme = THEME_LCD;
    f->expected = lcd;
    uiRenderClock(s);
    fails += warmSent != lcd;
    size_t n = sent.size();
    for (int i = 0; i < 10; i++) uiRenderClock(s);
    fails += sent.size() != n;  // nada de reenviar o gama enquanto a tela continua igual

    s.theme = THEME_FORECAST_LCD;
    uiInvalidateSlide();
    uiRenderClock(s);
    fails += warmSent != lcd || sent.size() != n;  // não apaga o verde no meio da transição LCD -> LCD

    for (int phase = PH_WORK; phase <= PH_LONG; phase++) {
      f->expected = normal;
      s.phase = phase; s.totalMs = s.remainingMs = 60000; s.running = phase != PH_SHORT;
      uiRenderPomodoro(s);
      fails += warmSent != normal;
    }
    f->expected = lcd; s.phase = PH_IDLE; s.theme = THEME_LCD;
    uiRenderClock(s);
    fails += warmSent != lcd;
    for (const char *name : {"foto.jpg", "animacao.gif"}) {
      f->expected = normal;
      fails += !showAlbumItem(name) || warmSent != normal;
      stopAlbum();
      f->expected = lcd; uiRenderClock(s);
    }

    f->expected = normal; uiMessage("SmallTV", "Configuracao", "", "");
    fails += warmSent != normal;
    f->expected = lcd; uiRenderClock(s);
    f->expected = normal; uiColorTest();
    fails += warmSent != normal;
    f->expected = lcd; uiRenderClock(s);
    f->expected = normal; uiVcomTest(30, true);
    fails += warmSent != normal;
    f->expected = lcd; uiRenderClock(s);
    f->expected = normal; uiError("erro");
    fails += warmSent != normal;

    cfg.theme = THEME_MATRIX;  // prévia de LCD com outro mostrador salvo: a tela real vence
    f->expected = lcd; uiRenderClock(s);
    fails += warmSent != lcd;
    cfg.theme = THEME_LCD; s.theme = THEME_TOMATO;
    f->expected = normal; uiRenderClock(s);
    fails += warmSent != normal;
  }
  fails += f->bad != 0;
  uiLcdViewHook = oldHook;
  delete f;
  printf("gama por tela visivel: %s (LCDs, Pomodoro, JPG/GIF, mensagens, testes, pre-visualizacao, dia/noite)\\n", fails ? "FALHOU" : "ok");
  return fails;
}
} // namespace TintTest
`);
