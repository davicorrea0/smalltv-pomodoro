#pragma once

#include "src/AnimatedGIF/AnimatedGIF.h"

#ifndef GIF_LIMPEZA_PALETA
#define GIF_LIMPEZA_PALETA 1
#endif
#ifndef GIF_FILTRO_HORIZONTAL
#define GIF_FILTRO_HORIZONTAL 1
#endif

static constexpr uint8_t GIF_LIMIAR_PRETO = 12;

struct AlbumGifRenderState {
  uint8_t prevDisp = 0;
  bool clearTrans = false;
  int16_t prevX = 0, prevY = 0, prevW = 0, prevH = 0;
  int16_t oldX0 = 0, oldY0 = 0, oldX1 = 0, oldY1 = 0;
  bool writing = false;
  bool blockWindow = false;
  int rowsSeen = 0;
  int paletteChanges = 0;
};

static inline int albumGifClip240(int v) { return v < 0 ? 0 : v > 240 ? 240 : v; }
static inline uint16_t albumGifSwap16(uint16_t v) { return (uint16_t)((v << 8) | (v >> 8)); }

static inline uint8_t albumGifExpand5(uint16_t v) { return (uint8_t)((v << 3) | (v >> 2)); }
static inline uint8_t albumGifExpand6(uint16_t v) { return (uint8_t)((v << 2) | (v >> 4)); }

static int albumGifCleanPalette(uint16_t *palette) {
#if GIF_LIMPEZA_PALETA
  int changed = 0;
  for (int i = 0; i < 256; i++) {
    uint16_t rgb = albumGifSwap16(palette[i]);
    uint8_t r = albumGifExpand5((rgb >> 11) & 31);
    uint8_t g = albumGifExpand6((rgb >> 5) & 63);
    uint8_t b = albumGifExpand5(rgb & 31);
    if (palette[i] && r <= GIF_LIMIAR_PRETO && g <= GIF_LIMIAR_PRETO && b <= GIF_LIMIAR_PRETO) {
      palette[i] = 0;
      changed++;
    }
  }
  return changed;
#else
  (void)palette;
  return 0;
#endif
}

static inline int albumGifAbs(int value) { return value < 0 ? -value : value; }

static bool albumGifVeryDifferent(uint16_t a, uint16_t b) {
  a = albumGifSwap16(a); b = albumGifSwap16(b);
  int dr = albumGifAbs((int)albumGifExpand5((a >> 11) & 31) - albumGifExpand5((b >> 11) & 31));
  int dg = albumGifAbs((int)albumGifExpand6((a >> 5) & 63) - albumGifExpand6((b >> 5) & 63));
  int db = albumGifAbs((int)albumGifExpand5(a & 31) - albumGifExpand5(b & 31));
  return dr >= 64 || dg >= 64 || db >= 64;
}

static bool albumGifSimilar(uint16_t a, uint16_t b) {
  a = albumGifSwap16(a); b = albumGifSwap16(b);
  int dr = albumGifAbs((int)albumGifExpand5((a >> 11) & 31) - albumGifExpand5((b >> 11) & 31));
  int dg = albumGifAbs((int)albumGifExpand6((a >> 5) & 63) - albumGifExpand6((b >> 5) & 63));
  int db = albumGifAbs((int)albumGifExpand5(a & 31) - albumGifExpand5(b & 31));
  return dr <= 16 && dg <= 16 && db <= 16;
}

static inline uint16_t albumGifFilteredColor(uint16_t *pal, uint8_t *pixels, int x, int width) {
#if GIF_FILTRO_HORIZONTAL
  if (x > 0 && x + 1 < width) {
    uint16_t left = pal[pixels[x - 1]], color = pal[pixels[x]], right = pal[pixels[x + 1]];
    if (albumGifVeryDifferent(color, left) && albumGifVeryDifferent(color, right) && albumGifSimilar(left, right))
      return left;
  }
#endif
  return pal[pixels[x]];
}

template <class Display>
static void albumGifDraw(AlbumGifRenderState &st, Display &out, uint16_t *buf,
                         int offX, int offY, int scale, GIFDRAW *p) {
  if (p->y == 0) {
    if (st.writing) {
      out.endWrite();
      st.writing = false;
    }
    int fx0 = albumGifClip240(p->iX * scale + offX);
    int fy0 = albumGifClip240(p->iY * scale + offY);
    int fx1 = albumGifClip240((p->iX + p->iWidth) * scale + offX);
    int fy1 = albumGifClip240((p->iY + p->iHeight) * scale + offY);
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
    st.paletteChanges = albumGifCleanPalette(p->pPalette);
    st.blockWindow = !p->ucHasTransparency && !p->ucIsInterlaced && fx1 > fx0 && fy1 > fy0;
    if (fx1 > fx0 && fy1 > fy0) {
      out.startWrite();
      st.writing = true;
      if (st.blockWindow) out.setAddrWindow(fx0, fy0, fx1 - fx0, fy1 - fy0);
    }
  }

  st.rowsSeen++;
  int frameX = p->iX * scale + offX;
  int src0 = frameX < 0 ? (-frameX + scale - 1) / scale : 0;
  int src1 = p->iWidth;
  if (frameX + src1 * scale > 240) src1 = (240 - frameX) / scale;
  int x0 = frameX + src0 * scale, w = (src1 - src0) * scale;
  uint8_t *s = p->pPixels + src0;
  int y0 = (p->iY + p->y) * scale + offY;
  for (int repeatY = 0; repeatY < scale; repeatY++) {
    int y = y0 + repeatY;
    if (w <= 0 || y < 0 || y >= 240) continue;
    uint16_t *pal = p->pPalette;
    bool rowRestore = st.clearTrans && y >= st.oldY0 && y < st.oldY1;
    uint8_t tr = p->ucTransparent;
    if (!p->ucHasTransparency || (rowRestore && x0 >= st.oldX0 && x0 + w <= st.oldX1)) {
      int n = 0;
      for (int i = 0; i < src1 - src0; i++) {
        uint8_t px = s[i];
        uint16_t color = (p->ucHasTransparency && px == tr) ? 0 :
          (p->ucHasTransparency ? pal[px] : albumGifFilteredColor(pal, p->pPixels, src0 + i, p->iWidth));
        for (int repeatX = 0; repeatX < scale; repeatX++) buf[n++] = color;
      }
      if (!st.blockWindow) out.setAddrWindow(x0, y, w, 1);
      out.writePixelsBE(buf, w);
    } else {
      auto drawPx = [&](int x) {
        int pxX = x0 + x * scale;
        return s[x] != tr || (rowRestore && pxX >= st.oldX0 && pxX < st.oldX1);
      };
      int srcW = src1 - src0, x = 0;
      while (x < srcW && !drawPx(x)) x++;
      while (x < srcW) {
        int start = x, n = 0;
        while (x < srcW && drawPx(x)) {
          uint8_t px = s[x++];
          uint16_t color = px == tr ? 0 : pal[px];
          for (int repeatX = 0; repeatX < scale; repeatX++) buf[n++] = color;
        }
        out.setAddrWindow(x0 + start * scale, y, n, 1);
        out.writePixelsBE(buf, n);
        while (x < srcW && !drawPx(x)) x++;
      }
    }
  }

  if (st.writing && st.rowsSeen >= p->iHeight) {
    out.endWrite();
    st.writing = false;
  }
}
