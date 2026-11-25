#pragma once

#include "src/AnimatedGIF/AnimatedGIF.h"

struct AlbumGifRenderState {
  uint8_t prevDisp = 0;
  bool clearTrans = false;
  int16_t prevX = 0, prevY = 0, prevW = 0, prevH = 0;
  int16_t oldX0 = 0, oldY0 = 0, oldX1 = 0, oldY1 = 0;
  bool writing = false;
  bool blockWindow = false;
  int rowsSeen = 0;
};

static inline int albumGifClip240(int v) { return v < 0 ? 0 : v > 240 ? 240 : v; }
static inline uint16_t albumGifSwap16(uint16_t v) { return (uint16_t)((v << 8) | (v >> 8)); }

template <class Display>
static void albumGifDraw(AlbumGifRenderState &st, Display &out, uint16_t *buf,
                         int offX, int offY, GIFDRAW *p) {
  if (p->y == 0) {
    if (st.writing) {
      out.endWrite();
      st.writing = false;
    }
    int fx0 = albumGifClip240(p->iX + offX), fy0 = albumGifClip240(p->iY + offY);
    int fx1 = albumGifClip240(p->iX + offX + p->iWidth);
    int fy1 = albumGifClip240(p->iY + offY + p->iHeight);
    st.clearTrans = st.prevDisp == 2 && st.prevW > 0 && st.prevH > 0;
    if (st.clearTrans) {
      st.oldX0 = st.prevX; st.oldY0 = st.prevY;
      st.oldX1 = st.prevX + st.prevW; st.oldY1 = st.prevY + st.prevH;
      uint16_t bg = 0;  // como os navegadores: disposal 2 limpa para "nada" (preto na tela), não para a cor de fundo do arquivo
      if (st.oldX1 <= fx0 || fx1 <= st.oldX0 || st.oldY1 <= fy0 || fy1 <= st.oldY0) {
        out.fillRect(st.oldX0, st.oldY0, st.oldX1 - st.oldX0, st.oldY1 - st.oldY0, bg);
      } else {
        int cy0 = st.oldY0 > fy0 ? st.oldY0 : fy0;
        int cy1 = st.oldY1 < fy1 ? st.oldY1 : fy1;
        if (st.oldY0 < cy0) out.fillRect(st.oldX0, st.oldY0, st.oldX1 - st.oldX0, cy0 - st.oldY0, bg);
        if (cy1 < st.oldY1) out.fillRect(st.oldX0, cy1, st.oldX1 - st.oldX0, st.oldY1 - cy1, bg);
        if (cy0 < cy1 && st.oldX0 < fx0) out.fillRect(st.oldX0, cy0, fx0 - st.oldX0, cy1 - cy0, bg);
        if (cy0 < cy1 && fx1 < st.oldX1) out.fillRect(fx1, cy0, st.oldX1 - fx1, cy1 - cy0, bg);
      }
    }
    st.prevDisp = p->ucDisposalMethod;
    st.prevX = (int16_t)fx0; st.prevY = (int16_t)fy0;
    st.prevW = (int16_t)(fx1 > fx0 ? fx1 - fx0 : 0);
    st.prevH = (int16_t)(fy1 > fy0 ? fy1 - fy0 : 0);
    st.rowsSeen = 0;
    st.blockWindow = !p->ucHasTransparency && !p->ucIsInterlaced && fx1 > fx0 && fy1 > fy0;
    if (fx1 > fx0 && fy1 > fy0) {
      out.startWrite();
      st.writing = true;
      if (st.blockWindow) out.setAddrWindow(fx0, fy0, fx1 - fx0, fy1 - fy0);
    }
  }

  st.rowsSeen++;
  int y = p->iY + p->y + offY;
  uint8_t *s = p->pPixels;
  int x0 = p->iX + offX, w = p->iWidth;
  if (x0 < 0) { s -= x0; w += x0; x0 = 0; }
  if (x0 + w > 240) w = 240 - x0;
  if (w > 0 && y >= 0 && y < 240) {
    uint16_t *pal = p->pPalette;
    bool rowRestore = st.clearTrans && y >= st.oldY0 && y < st.oldY1;
    uint8_t tr = p->ucTransparent;
    if (!p->ucHasTransparency || (rowRestore && x0 >= st.oldX0 && x0 + w <= st.oldX1)) {
      int i = 0;
      for (; i + 3 < w; i += 4) {
        uint8_t a = s[i], b = s[i + 1], c = s[i + 2], d = s[i + 3];
        buf[i] = (p->ucHasTransparency && a == tr) ? 0 : pal[a];
        buf[i + 1] = (p->ucHasTransparency && b == tr) ? 0 : pal[b];
        buf[i + 2] = (p->ucHasTransparency && c == tr) ? 0 : pal[c];
        buf[i + 3] = (p->ucHasTransparency && d == tr) ? 0 : pal[d];
      }
      for (; i < w; i++) {
        uint8_t px = s[i];
        buf[i] = (p->ucHasTransparency && px == tr) ? 0 : pal[px];
      }
      if (!st.blockWindow) out.setAddrWindow(x0, y, w, 1);
      out.writePixelsBE(buf, w);
    } else {
      auto drawPx = [&](int x) {
        return s[x] != tr || (rowRestore && x0 + x >= st.oldX0 && x0 + x < st.oldX1);
      };
      int x = 0;
      while (x < w && !drawPx(x)) x++;
      while (x < w) {
        int start = x, n = 0;
        while (x < w && drawPx(x)) {
          uint8_t px = s[x++];
          buf[n++] = px == tr ? 0 : pal[px];
        }
        out.setAddrWindow(x0 + start, y, n, 1);
        out.writePixelsBE(buf, n);
        while (x < w && !drawPx(x)) x++;
      }
    }
  }

  if (st.writing && st.rowsSeen >= p->iHeight) {
    out.endWrite();
    st.writing = false;
  }
}

