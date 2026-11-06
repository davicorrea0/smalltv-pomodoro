// Foto sem memória × foto estragada: compila drawJpg/jpgAlloc/showAlbumItem reais do firmware (sim/fonte.cjs) com um JPEGDEC
// de mentira e confere que a falta de memória (alocação nula ou maior bloco livre pequeno) mostra
// "Sem memoria p/ foto" e NÃO "JPG invalido", e que só o decodificador recusando o arquivo dá "JPG invalido".
// Uso: node sim/jpg_nomem_test.cjs saida.cpp [fonte.cpp]; depois g++ -std=c++17 saida.cpp.
const fs = require('fs'), path = require('path');
const source = (process.argv[3] ? fs.readFileSync(process.argv[3], 'utf8') : require('./fonte.cjs'));
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
static bool g_alloc = true, g_open = true, g_decode = true; static int g_err = 0; static uint32_t g_maxBlock = 100000;
static int g_decodes = 0, g_unloads = 0, g_loads = 0; static bool g_packLoaded = false, g_packAtAlloc = false, g_loadOk = true, g_isGif = false, g_preview = false;
static uint32_t g_now = 0;
uint32_t millis() { return g_now; }
bool previewActive() { return g_preview; }
enum { THEME_MATRIX = 0, THEME_PACK = 200 };
struct { int theme = THEME_MATRIX; char facePkg[24] = "koi"; } cfg;
bool facePackLoaded() { return g_packLoaded; }
void facePackUnload() { if (g_packLoaded) { g_packLoaded = false; g_unloads++; g_maxBlock += 6627; } }  // libera ~6,6 KB de bloco
bool loadPack(const String &) { g_loads++; g_packLoaded = g_loadOk; return g_loadOk; }
struct { uint32_t getMaxFreeBlockSize() { return g_maxBlock; } } ESP;
enum { JPEG_ERROR_MEMORY = 5, JPEG_DECODE_ERROR = 2 };
enum { RGB565_LITTLE_ENDIAN = 1, JPEG_SCALE_HALF = 2, JPEG_SCALE_QUARTER = 4, JPEG_SCALE_EIGHTH = 8 };
struct JPEGDEC {
  uint8_t pad[18000];
  static void *operator new(size_t n, const std::nothrow_t &) noexcept { g_packAtAlloc = g_packLoaded; return g_alloc ? malloc(n) : nullptr; }
  static void operator delete(void *p) { free(p); }
  template <class... A> bool open(A...) { return g_open; }
  int getWidth() { return 100; } int getHeight() { return 100; }
  void setPixelType(int) {}
  bool decode(int, int, int) { g_decodes++; return g_decode; }
  int getLastError() { return g_decode ? 0 : g_err; }
  void close() {}
};
struct JPEGDRAW; int mediaOpen, mediaClose, jpgRead, jpgSeek, jpgDraw;
enum { GIF_PALETTE_RGB565_LE = 1 };
struct AnimatedGIF {
  uint8_t pad[24000];
  void *operator new(size_t n, const std::nothrow_t &) noexcept { g_packAtAlloc = g_packLoaded; return g_alloc && g_maxBlock >= n ? malloc(n) : nullptr; }
  void begin(int) {} template <class... A> bool open(A...) { return true; } void close() {}
  int getCanvasWidth() { return 1; } int getCanvasHeight() { return 1; }
};
AnimatedGIF *gif = nullptr; static int gifOffX, gifOffY; static uint32_t nextFrameAt; int gifRead, gifSeek, gifDraw;
static std::string lastErr; static bool inAlbum = false; static String curFile;
void uiError(const char *m) { lastErr = m; }
void uiInvalidate() {} void stopAlbum() {} void setLcdView(bool) {}
struct { void fillScreen(int) {} } tft;
struct { bool exists(const String &) { return true; } } LittleFS;
bool isGif(const String &) { return g_isGif; }
bool validGif(const String &) { return true; }
const int MAX_JPEG_DIM = 1920;
${fn('bool jpgDimensionsOk(')}
static int jpgCheckDraw(JPEGDRAW *) { return 1; }
${enumLine}${fn('JPEGDEC *jpgAlloc() {')}
${fn('JpgResult validJpg(')}
${fn('static void albumMakeRoom(')}
static uint32_t packRetryAt;
${fn('JpgResult drawJpg(')}
${fn('bool showAlbumItem(')}
${fn('void packEnsureLoaded(')}
#define CHECK(c) do { if (!(c)) { printf("FALHA linha %d: %s (msg='%s')\\n", __LINE__, #c, lastErr.c_str()); return 1; } } while (0)
static void reset() { g_err = 0; g_unloads = g_loads = 0; g_packLoaded = g_packAtAlloc = g_isGif = g_preview = false; g_loadOk = true; g_now = 100000; packRetryAt = 0; cfg.theme = THEME_MATRIX; g_alloc = g_open = g_decode = true; g_maxBlock = 100000; lastErr.clear(); g_decodes = 0; }
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
  // tabelas do decodificador sem memória (DecodeJPEG devolve JPEG_ERROR_MEMORY): é falta de RAM, não arquivo ruim
  reset(); g_decode = false; g_err = JPEG_ERROR_MEMORY; CHECK(drawJpg("/a.jpg") == JPG_NOMEM && validJpg("/a.jpg") == JPG_NOMEM);
  reset(); g_decode = false; g_err = JPEG_ERROR_MEMORY; CHECK(showAlbumItem("/a.jpg") && lastErr == "Sem memoria p/ foto");
  reset(); g_decode = false; g_err = JPEG_DECODE_ERROR; CHECK(drawJpg("/b.jpg") == JPG_BAD && validJpg("/b.jpg") == JPG_BAD);  // estragado continua BAD
  reset(); CHECK(validJpg("/a.jpg") == JPG_OK);
  reset(); g_open = false; CHECK(validJpg("/b.jpg") == JPG_BAD);
  reset(); g_alloc = false; CHECK(validJpg("/a.jpg") == JPG_NOMEM);
  // a falta de memória some sozinha: o próximo item, com memória, mostra a foto sem nenhum aviso
  reset(); g_alloc = false; showAlbumItem("/a.jpg"); lastErr.clear(); g_alloc = true;
  CHECK(showAlbumItem("/a.jpg") && lastErr.empty() && g_decodes == 1);
  // ---- pacote de mostrador × álbum: só sai da RAM sob pressão, e a ordem importa (liberar ANTES de alocar) ----
  const uint32_t JPG_NEED = sizeof(JPEGDEC) + 1024 + 8192, GIF_NEED = sizeof(AnimatedGIF) + 1024;
  reset(); g_packLoaded = true; g_maxBlock = JPG_NEED; CHECK(showAlbumItem("/a.jpg") && g_unloads == 0 && g_packLoaded && lastErr.empty());  // folga: o pacote fica
  reset(); g_packLoaded = true; g_maxBlock = JPG_NEED - 1; CHECK(showAlbumItem("/a.jpg") && g_unloads == 1 && !g_packLoaded && !g_packAtAlloc && lastErr.empty()); // aperto: libera antes do decodificador
  reset(); g_packLoaded = true; g_maxBlock = sizeof(JPEGDEC) + 1024; CHECK(showAlbumItem("/a.jpg") && !g_packLoaded && lastErr.empty());  // só cabia o decodificador: com o pacote fora, a foto aparece
  reset(); g_maxBlock = JPG_NEED - 1; CHECK(showAlbumItem("/a.jpg") && g_unloads == 0);                       // sem pacote carregado, nada a liberar
  reset(); g_isGif = true; g_packLoaded = true; g_maxBlock = GIF_NEED; CHECK(showAlbumItem("/a.gif") && g_unloads == 0 && g_packLoaded);
  reset(); g_isGif = true; g_packLoaded = true; g_maxBlock = GIF_NEED - 1; CHECK(showAlbumItem("/a.gif") && g_unloads == 1 && !g_packAtAlloc && lastErr.empty() && gif != nullptr);
  reset(); g_isGif = true; g_packLoaded = true; g_maxBlock = 100; CHECK(showAlbumItem("/a.gif") && lastErr == "Sem memoria p/ GIF");  // nem sem o pacote: aviso, sem travar
  // ---- volta ao relógio: recarrega sem mexer no tema, com nova tentativa em 2 s ----
  reset(); cfg.theme = THEME_MATRIX; packEnsureLoaded(); CHECK(g_loads == 0);                                  // mostrador embutido: nada a recarregar
  reset(); cfg.theme = THEME_PACK; g_packLoaded = true; packEnsureLoaded(); CHECK(g_loads == 0);               // já carregado
  reset(); cfg.theme = THEME_PACK; g_preview = true; packEnsureLoaded(); CHECK(g_loads == 0);                  // prévia manda no motor
  reset(); cfg.theme = THEME_PACK; packEnsureLoaded(); CHECK(g_loads == 1 && g_packLoaded && cfg.theme == THEME_PACK);
  reset(); cfg.theme = THEME_PACK; g_loadOk = false; packEnsureLoaded(); CHECK(g_loads == 1 && cfg.theme == THEME_PACK);  // falha: tema intacto
  g_now += 1999; packEnsureLoaded(); CHECK(g_loads == 1);                                                      // antes de 2 s não tenta de novo
  g_now += 1; g_loadOk = true; packEnsureLoaded(); CHECK(g_loads == 2 && g_packLoaded && cfg.theme == THEME_PACK);
  packEnsureLoaded(); CHECK(g_loads == 2);                                                                     // já carregado: não relê
  puts("jpg_nomem: ok"); return 0;
}
`);
