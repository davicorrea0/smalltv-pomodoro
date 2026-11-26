// Render real do álbum contra um ST7789 falso de 240x240.
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#ifndef __LINUX__
#define __LINUX__
#endif
#include "../smalltv-pomodoro/src/AnimatedGIF/AnimatedGIF.cpp"
#include "../smalltv-pomodoro/album_gif_renderer.h"

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

struct MemoryFile { const uint8_t *data; int size, pos; };
static MemoryFile input;

static void *mediaOpen(const char *, int32_t *size) {
  input.pos = 0;
  *size = input.size;
  return &input;
}
static void mediaClose(void *) {}
static int32_t gifRead(GIFFILE *f, uint8_t *buf, int32_t len) {
  auto *m = static_cast<MemoryFile *>(f->fHandle);
  if (len < 0 || m->pos < 0 || m->pos > m->size) return 0;
  len = std::min(len, m->size - m->pos);
  if (len) std::memcpy(buf, m->data + m->pos, (size_t)len);
  m->pos += len;
  return f->iPos = m->pos, len;
}
static int32_t gifSeek(GIFFILE *f, int32_t pos) {
  auto *m = static_cast<MemoryFile *>(f->fHandle);
  if (pos < 0 || pos > m->size) return -1;
  return f->iPos = m->pos = pos;
}

struct FakeTft {
  uint16_t pixels[240 * 240]{};
  int wx = 0, wy = 0, ww = 0, wh = 0, cursor = 0;
  uint64_t starts = 0, ends = 0, windows = 0, bytes = 0;

  void resetMetrics() { starts = ends = windows = bytes = 0; }
  void startWrite() { starts++; }
  void endWrite() { ends++; }
  void setAddrWindow(int x, int y, int w, int h) {
    windows++;
    wx = x; wy = y; ww = w; wh = h; cursor = 0;
  }
  void writePixelsBE(uint16_t *src, int n) {
    bytes += (uint64_t)n * 2;
    for (int i = 0; i < n; i++, cursor++) {
      int x = wx + cursor % ww, y = wy + cursor / ww;
      if (x >= 0 && x < 240 && y >= 0 && y < 240 && cursor < ww * wh)
        pixels[y * 240 + x] = albumGifSwap16(src[i]);
    }
  }
  void fillRect(int x, int y, int w, int h, uint16_t color) {
    if (w <= 0 || h <= 0) return;
    starts++; ends++; windows++; bytes += (uint64_t)w * h * 2;
    for (int yy = std::max(0, y); yy < std::min(240, y + h); yy++)
      for (int xx = std::max(0, x); xx < std::min(240, x + w); xx++) pixels[yy * 240 + xx] = color;
  }
};

static FakeTft screen;
static AlbumGifRenderState renderState;
static uint16_t rowBuf[240];
static int offX, offY, scale;
static bool legacy;
static double drawUs;
static uint8_t oldPrevDisp;
static bool oldClearTrans;
static int16_t oldPrevX, oldPrevY, oldPrevW, oldPrevH, oldX0, oldY0, oldX1, oldY1;

static int clip240(int v) { return v < 0 ? 0 : v > 240 ? 240 : v; }

// Cópia do gifDraw anterior, usada só para medir o antes.
static void legacyDraw(GIFDRAW *p) {
  if (p->y == 0) {
    int fx0 = clip240(p->iX + offX), fy0 = clip240(p->iY + offY);
    int fx1 = clip240(p->iX + offX + p->iWidth), fy1 = clip240(p->iY + offY + p->iHeight);
    oldClearTrans = oldPrevDisp == 2 && oldPrevW > 0 && oldPrevH > 0;
    if (oldClearTrans) {
      oldX0 = oldPrevX; oldY0 = oldPrevY; oldX1 = oldPrevX + oldPrevW; oldY1 = oldPrevY + oldPrevH;
      if (oldX1 <= fx0 || fx1 <= oldX0 || oldY1 <= fy0 || fy1 <= oldY0) screen.fillRect(oldX0, oldY0, oldX1-oldX0, oldY1-oldY0, 0);
      else {
        int cy0 = std::max<int>(oldY0, fy0), cy1 = std::min<int>(oldY1, fy1);
        if (oldY0 < cy0) screen.fillRect(oldX0, oldY0, oldX1-oldX0, cy0-oldY0, 0);
        if (cy1 < oldY1) screen.fillRect(oldX0, cy1, oldX1-oldX0, oldY1-cy1, 0);
        if (cy0 < cy1 && oldX0 < fx0) screen.fillRect(oldX0, cy0, fx0-oldX0, cy1-cy0, 0);
        if (cy0 < cy1 && fx1 < oldX1) screen.fillRect(fx1, cy0, oldX1-fx1, cy1-cy0, 0);
      }
    }
    oldPrevDisp = p->ucDisposalMethod;
    oldPrevX = fx0; oldPrevY = fy0; oldPrevW = std::max(0, fx1-fx0); oldPrevH = std::max(0, fy1-fy0);
  }
  int y = p->iY + p->y + offY;
  if (y < 0 || y >= 240) return;
  uint8_t *s = p->pPixels;
  int x0 = p->iX + offX, w = p->iWidth;
  if (x0 < 0) { s -= x0; w += x0; x0 = 0; }
  if (x0 + w > 240) w = 240 - x0;
  if (w <= 0) return;
  uint16_t *pal = p->pPalette;
  bool restore = oldClearTrans && y >= oldY0 && y < oldY1;
  uint8_t tr = p->ucTransparent;
  if (!p->ucHasTransparency || (restore && x0 >= oldX0 && x0 + w <= oldX1)) {
    for (int i=0;i<w;i++) rowBuf[i] = p->ucHasTransparency && s[i] == tr ? 0 : pal[s[i]];
    screen.startWrite(); screen.setAddrWindow(x0,y,w,1); screen.writePixelsBE(rowBuf,w); screen.endWrite();
    return;
  }
  auto drawPx = [&](int x) { return s[x] != tr || (restore && x0+x >= oldX0 && x0+x < oldX1); };
  int x=0; while (x<w && !drawPx(x)) x++;
  if (x >= w) return;
  screen.startWrite();
  while (x<w) {
    int start=x,n=0;
    while (x<w && drawPx(x)) { uint8_t px=s[x++]; rowBuf[n++]=px==tr?0:pal[px]; }
    screen.setAddrWindow(x0+start,y,n,1); screen.writePixelsBE(rowBuf,n);
    while (x<w && !drawPx(x)) x++;
  }
  screen.endWrite();
}

static void gifDraw(GIFDRAW *p) {
  auto t0 = Clock::now();
  if (legacy) legacyDraw(p);
  else albumGifDraw(renderState, screen, rowBuf, offX, offY, scale, p);
  drawUs += std::chrono::duration<double, std::micro>(Clock::now() - t0).count();
}

static std::vector<uint8_t> readFile(const fs::path &path) {
  std::ifstream f(path, std::ios::binary);
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}

static void writeFrame(const fs::path &path) {
  std::ofstream f(path, std::ios::binary);
  for (uint16_t p : screen.pixels) { f.put((char)(p & 255)); f.put((char)(p >> 8)); }
}

int main(int argc, char **argv) {
  fs::path out = "pesquisas/gif-qualidade/firmware";
  std::vector<fs::path> files;
  for (int i=1;i<argc;i++) {
    std::string a=argv[i];
    if (a=="--out" && i+1<argc) out=argv[++i];
    else if (a=="--legacy") legacy=true;
    else files.emplace_back(a);
  }
  if (files.empty()) for (auto &e:fs::directory_iterator("sim/gif-album")) if (e.path().extension()==".gif") files.push_back(e.path());
  std::sort(files.begin(),files.end());
  fs::create_directories(out);
  std::ofstream metrics(out / "metricas.tsv");
  metrics << "arquivo\tquadro\tstartWrite\tendWrite\tsetAddrWindow\tbytes\tdraw_us\tframe_us\n";
  int errors=0;
  for (const auto &path:files) {
    auto data=readFile(path); input={data.data(),(int)data.size(),0};
    std::memset(&screen,0,sizeof(screen)); albumGifRenderFree(renderState); renderState={}; renderState.rgb=(uint8_t*)malloc(256*3);  // como o album.cpp: tabela alocada ao abrir
    oldPrevDisp=0; oldClearTrans=false; oldPrevX=oldPrevY=oldPrevW=oldPrevH=oldX0=oldY0=oldX1=oldY1=0;
    AnimatedGIF gif; gif.begin(GIF_PALETTE_RGB565_BE);
    if (!gif.open("album.gif",mediaOpen,mediaClose,gifRead,gifSeek,gifDraw)) { std::fprintf(stderr,"open: %s erro=%d\n",path.string().c_str(),gif.getLastError()); errors++; continue; }
    scale=std::min(240/gif.getCanvasWidth(),240/gif.getCanvasHeight());
    if (scale<2) scale=1;
    offX=std::max(0,(240-gif.getCanvasWidth()*scale)/2); offY=std::max(0,(240-gif.getCanvasHeight()*scale)/2);
    fs::path dir=out/path.stem(); fs::create_directories(dir);
    for (int frame=0;frame<10000;frame++) {
      screen.resetMetrics(); drawUs=0;
      auto t0=Clock::now(); int delay=0; int r=gif.playFrame(false,&delay);
      double frameUs=std::chrono::duration<double,std::micro>(Clock::now()-t0).count();
      if (r<0) { std::fprintf(stderr,"decode: %s q=%d erro=%d\n",path.string().c_str(),frame,gif.getLastError()); errors++; break; }
      char name[32]; std::snprintf(name,sizeof(name),"quadro-%04d.raw",frame); writeFrame(dir/name);
      metrics << path.filename().string() << '\t' << frame << '\t' << screen.starts << '\t' << screen.ends << '\t'
              << screen.windows << '\t' << screen.bytes << '\t' << drawUs << '\t' << frameUs << '\n';
      if (r==0) break;
    }
    gif.close();
  }
  return errors ? 1 : 0;
}
