// Foto sem memória × foto estragada: compila drawJpg/jpgAlloc/showAlbumItem reais de main.cpp com um JPEGDEC
// de mentira e confere que a falta de memória (alocação nula ou maior bloco livre pequeno) mostra
// "Sem memoria p/ foto" e NÃO "JPG invalido", e que só o decodificador recusando o arquivo dá "JPG invalido".
// Uso: node sim/jpg_nomem_test.cjs saida.cpp [main.cpp]; depois g++ -std=c++17 saida.cpp.
const fs = require('fs'), path = require('path');
const source = fs.readFileSync(process.argv[3] || path.join(__dirname, '../smalltv-pomodoro/main.cpp'), 'utf8');
const out = process.argv[2];
if (!out) throw Error('output.cpp required');
function fn(sig) { const a = source.indexOf(sig), b = source.indexOf('\n}', a); if (a < 0 || b < 0) throw Error(sig); return source.slice(a, b + 2); }
const enumLine = source.match(/enum JpgResult[^\n]*\n/)[0];
if (!/validJpg/.test(source) || !/r != JPG_OK/.test(source)) throw Error('upload deve tratar JpgResult');
fs.writeFileSync(out, `#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <new>
#include <string>
#include <algorithm>
using std::max;
using String = std::string;
#define tr(pt, en, es) (pt)
static bool g_alloc = true, g_open = true, g_decode = true; static uint32_t g_maxBlock = 100000;
static int g_decodes = 0;
struct { uint32_t getMaxFreeBlockSize() { return g_maxBlock; } } ESP;
enum { RGB565_LITTLE_ENDIAN = 1, JPEG_SCALE_HALF = 2, JPEG_SCALE_QUARTER = 4, JPEG_SCALE_EIGHTH = 8 };
struct JPEGDEC {
  uint8_t pad[18000];
  static void *operator new(size_t n, const std::nothrow_t &) noexcept { return g_alloc ? malloc(n) : nullptr; }
  static void operator delete(void *p) { free(p); }
  template <class... A> bool open(A...) { return g_open; }
  int getWidth() { return 100; } int getHeight() { return 100; }
  void setPixelType(int) {}
  bool decode(int, int, int) { g_decodes++; return g_decode; }
  void close() {}
};
struct JPEGDRAW; int mediaOpen, mediaClose, jpgRead, jpgSeek, jpgDraw;
enum { GIF_PALETTE_RGB565_LE = 1 };
struct AnimatedGIF {
  void *operator new(size_t n, const std::nothrow_t &) noexcept { return malloc(n); }
  void begin(int) {} template <class... A> bool open(A...) { return true; } void close() {}
  int getCanvasWidth() { return 1; } int getCanvasHeight() { return 1; }
};
AnimatedGIF *gif = nullptr; static int gifOffX, gifOffY; static uint32_t nextFrameAt; int gifRead, gifSeek, gifDraw;
static std::string lastErr; static bool inAlbum = false; static String curFile;
void uiError(const char *m) { lastErr = m; }
void uiInvalidate() {} void stopAlbum() {} void setLcdView(bool) {}
struct { void fillScreen(int) {} } tft;
struct { bool exists(const String &) { return true; } } LittleFS;
bool isGif(const String &) { return false; }
bool validGif(const String &) { return true; }
const int MAX_JPEG_DIM = 1920;
${fn('bool jpgDimensionsOk(')}
${enumLine}${fn('JPEGDEC *jpgAlloc() {')}
${fn('JpgResult drawJpg(')}
${fn('bool showAlbumItem(')}
#define CHECK(c) do { if (!(c)) { printf("FALHA linha %d: %s (msg='%s')\\n", __LINE__, #c, lastErr.c_str()); return 1; } } while (0)
static void reset() { g_alloc = g_open = g_decode = true; g_maxBlock = 100000; lastErr.clear(); g_decodes = 0; }
int main() {
  reset(); CHECK(showAlbumItem("/a.jpg") && lastErr.empty() && g_decodes == 1);                    // arquivo bom: sem aviso
  reset(); g_alloc = false; CHECK(drawJpg("/a.jpg") == JPG_NOMEM);                                // new nothrow nulo
  reset(); g_alloc = false; CHECK(showAlbumItem("/a.jpg") && lastErr == "Sem memoria p/ foto" && g_decodes == 0);
  reset(); g_maxBlock = 4000; CHECK(drawJpg("/a.jpg") == JPG_NOMEM && g_decodes == 0);            // maior bloco livre pequeno
  reset(); g_maxBlock = 4000; CHECK(showAlbumItem("/a.jpg") && lastErr == "Sem memoria p/ foto");
  reset(); g_maxBlock = sizeof(JPEGDEC) + 1024; CHECK(drawJpg("/a.jpg") == JPG_OK);              // limite exato basta
  reset(); g_maxBlock = sizeof(JPEGDEC) + 1023; CHECK(drawJpg("/a.jpg") == JPG_NOMEM);
  reset(); g_open = false; CHECK(drawJpg("/b.jpg") == JPG_BAD);                                   // arquivo recusado ao abrir
  reset(); g_open = false; CHECK(showAlbumItem("/b.jpg") && lastErr == "JPG invalido");
  reset(); g_decode = false; CHECK(showAlbumItem("/b.jpg") && lastErr == "JPG invalido");         // recusado ao decodificar
  // a falta de memória some sozinha: o próximo item, com memória, mostra a foto sem nenhum aviso
  reset(); g_alloc = false; showAlbumItem("/a.jpg"); lastErr.clear(); g_alloc = true;
  CHECK(showAlbumItem("/a.jpg") && lastErr.empty() && g_decodes == 1);
  puts("jpg_nomem: ok"); return 0;
}
`);
