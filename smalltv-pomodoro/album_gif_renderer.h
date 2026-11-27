#pragma once

#include <stdlib.h>
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
  int paletteChanges = 0;  // quase pretos na paleta do quadro: liga o filtro de ponto isolado
  // R, G e B de 8 bits de cada cor da paleta (768 B), montado uma vez por quadro: o filtro compara pela tabela em vez
  // de converter três cores RGB565 por pixel. Mora no heap só enquanto o GIF toca (albumGifRenderFree solta);
  // sem memória, o quadro sai sem o filtro.
  uint8_t *rgb = nullptr;
};

static inline void albumGifRenderFree(AlbumGifRenderState &st) {
  free(st.rgb);
  st.rgb = nullptr;
}

static inline int albumGifClip240(int v) { return v < 0 ? 0 : v > 240 ? 240 : v; }
static inline uint16_t albumGifSwap16(uint16_t v) { return (uint16_t)((v << 8) | (v >> 8)); }

static inline uint8_t albumGifExpand5(uint16_t v) { return (uint8_t)((v << 3) | (v >> 2)); }
static inline uint8_t albumGifExpand6(uint16_t v) { return (uint8_t)((v << 2) | (v >> 4)); }

// Uma passada pela paleta por quadro: conta e zera os quase pretos (ruído da compressão com perda) e, só quando há
// quase pretos (filtro ativo) e a tabela está alocada, guarda o R, G, B de 8 bits de cada cor já limpa.
static int albumGifPreparePalette(uint16_t *palette, uint8_t *rgb) {
  static_assert(GIF_LIMIAR_PRETO == 12, "mascara RGB565 BE abaixo assume limiar 12 (r5<=1, g6<=3, b5<=1)");
  int nearBlack = 0;
  for (int i = 0; i < 256; i++) {
    uint16_t p = palette[i];
    if (p && (p & 0x9EF7u) == 0) {
      nearBlack++;
#if GIF_LIMPEZA_PALETA
      palette[i] = 0;
#endif
    }
  }
  if (nearBlack && rgb) {
    for (int i = 0; i < 256; i++) {
      uint16_t v = albumGifSwap16(palette[i]);
      rgb[3 * i] = albumGifExpand5((v >> 11) & 31);
      rgb[3 * i + 1] = albumGifExpand6((v >> 5) & 63);
      rgb[3 * i + 2] = albumGifExpand5(v & 31);
    }
  }
  return nearBlack;
}

static inline int albumGifAbs(int value) { return value < 0 ? -value : value; }

// diferença grande (>= 64 em algum canal) e cores parecidas (<= 16 em todos), em R, G, B de 8 bits
static inline bool albumGifFar(const uint8_t *a, const uint8_t *b) {
  return albumGifAbs(a[0] - b[0]) >= 64 || albumGifAbs(a[1] - b[1]) >= 64 || albumGifAbs(a[2] - b[2]) >= 64;
}
static inline bool albumGifNear(const uint8_t *a, const uint8_t *b) {
  return albumGifAbs(a[0] - b[0]) <= 16 && albumGifAbs(a[1] - b[1]) <= 16 && albumGifAbs(a[2] - b[2]) <= 16;
}

// Linha sem transparência, com o filtro de ponto isolado: um pixel muito diferente dos dois vizinhos, que são
// parecidos entre si, vira a cor do vizinho da esquerda. Atalho: índice igual a um vizinho = mesma cor, nada a filtrar
// (a maior parte dos pixels); só os que diferem dos dois consultam a tabela. Cada cor sai repetida `scale` vezes.
static inline int albumGifRowFiltered(uint16_t *buf, const uint16_t *pal, const uint8_t *rgb, const uint8_t *row,
                                      int width, int src0, int src1, int scale) {
  int n = 0;
  for (int x = src0; x < src1; x++) {
    uint8_t c = row[x];
    uint16_t color = pal[c];
#if GIF_FILTRO_HORIZONTAL
    if (rgb && x > 0 && x + 1 < width) {
      uint8_t l = row[x - 1], r = row[x + 1];
      if (c != l && c != r) {
        const uint8_t *C = rgb + 3 * c, *L = rgb + 3 * l, *R = rgb + 3 * r;
        if ((l == r || albumGifNear(L, R)) && albumGifFar(C, L) && albumGifFar(C, R)) color = pal[l];
      }
    }
#else
    (void)rgb; (void)width;
#endif
    if (scale == 1) buf[n++] = color;
    else for (int k = 0; k < scale; k++) buf[n++] = color;
  }
  return n;
}

// fim da linha: depois da última linha do quadro, fecha a transação SPI
template <class Display>
static inline void albumGifRowDone(AlbumGifRenderState &st, Display &out, GIFDRAW *p) {
  if (st.writing && st.rowsSeen >= p->iHeight) {
    out.endWrite();
    st.writing = false;
  }
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
    st.paletteChanges = albumGifPreparePalette(p->pPalette, st.rgb);
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
  if (!p->ucHasTransparency) {
    // sem transparência: a linha (com filtro e ampliação) é calculada uma vez e enviada `scale` vezes; o envio
    // big-endian não altera o buffer
    if (w <= 0) return albumGifRowDone(st, out, p);
    // o filtro só liga quando a paleta tem quase pretos, a marca da compressão com perda: GIF limpo (pixel art,
    // pontos de 1 px de propósito) sai como veio
    albumGifRowFiltered(buf, p->pPalette, st.paletteChanges ? st.rgb : nullptr, p->pPixels, p->iWidth, src0, src1, scale);
    for (int repeatY = 0; repeatY < scale; repeatY++) {
      int y = y0 + repeatY;
      if (y < 0 || y >= 240) continue;
      if (!st.blockWindow) out.setAddrWindow(x0, y, w, 1);
      out.writePixelsBE(buf, w);
    }
    return albumGifRowDone(st, out, p);
  }
  if (w <= 0) return albumGifRowDone(st, out, p);
  int ry0 = y0 < 0 ? 0 : y0;
  int ry1 = y0 + scale > 240 ? 240 : y0 + scale;
  if (ry0 >= ry1) return albumGifRowDone(st, out, p);
  uint16_t *pal = p->pPalette;
  uint8_t tr = p->ucTransparent;
  int srcW = src1 - src0;
  // Quando todas as linhas repetidas por `scale` têm o mesmo comportamento de transparência (disposal 0/1 ou
  // restauração cobrindo o bloco inteiro), monta os pixels uma única vez e envia em uma janela SPI de altura ry1-ry0.
  bool fullRestore = st.clearTrans && ry0 >= st.oldY0 && ry1 <= st.oldY1 && x0 >= st.oldX0 && x0 + w <= st.oldX1;
  if (fullRestore) {
    int n = 0;
    for (int i = 0; i < srcW; i++) {
      uint8_t px = s[i];
      uint16_t color = px == tr ? 0 : pal[px];
      if (scale == 1) buf[n++] = color;
      else for (int repeatX = 0; repeatX < scale; repeatX++) buf[n++] = color;
    }
    out.setAddrWindow(x0, ry0, w, ry1 - ry0);
    for (int y = ry0; y < ry1; y++) out.writePixelsBE(buf, w);
  } else if (!st.clearTrans || ry1 <= st.oldY0 || ry0 >= st.oldY1) {
    int x = 0;
    while (x < srcW && s[x] == tr) x++;
    while (x < srcW) {
      int start = x, n = 0;
      while (x < srcW && s[x] != tr) {
        uint16_t color = pal[s[x++]];
        if (scale == 1) buf[n++] = color;
        else for (int repeatX = 0; repeatX < scale; repeatX++) buf[n++] = color;
      }
      out.setAddrWindow(x0 + start * scale, ry0, n, ry1 - ry0);
      for (int y = ry0; y < ry1; y++) out.writePixelsBE(buf, n);
      while (x < srcW && s[x] == tr) x++;
    }
  } else {
    for (int y = ry0; y < ry1; y++) {
      bool rowRestore = st.clearTrans && y >= st.oldY0 && y < st.oldY1;
      if (rowRestore && x0 >= st.oldX0 && x0 + w <= st.oldX1) {
        int n = 0;
        for (int i = 0; i < srcW; i++) {
          uint8_t px = s[i];
          uint16_t color = px == tr ? 0 : pal[px];
          for (int repeatX = 0; repeatX < scale; repeatX++) buf[n++] = color;
        }
        out.setAddrWindow(x0, y, w, 1);
        out.writePixelsBE(buf, w);
      } else {
        auto drawPx = [&](int x) {
          int pxX = x0 + x * scale;
          return s[x] != tr || (rowRestore && pxX >= st.oldX0 && pxX < st.oldX1);
        };
        int x = 0;
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
  }

  albumGifRowDone(st, out, p);
}
