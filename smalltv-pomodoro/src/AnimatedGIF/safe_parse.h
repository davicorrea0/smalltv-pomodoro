// SmallTV changes to AnimatedGIF 2.2.3, 2026-10-22 (Apache-2.0).
// Sequential, checked reads: no parsing beyond the bytes actually received.
static bool GIFReadExact(GIFIMAGE *g, uint8_t *dst, int n) {
    if (n < 0 || g->GIFFile.iPos < 0 || n > g->GIFFile.iSize - g->GIFFile.iPos ||
        g->pfnRead(&g->GIFFile, dst, n) != n) {
        g->iError = GIF_EARLY_EOF;
        return false;
    }
    return true;
}

static bool GIFSkipExact(GIFIMAGE *g, int n) {
    if (n < 0 || n > g->GIFFile.iSize - g->GIFFile.iPos) {
        g->iError = GIF_EARLY_EOF;
        return false;
    }
    int pos = g->GIFFile.iPos + n;
    if (g->pfnSeek(&g->GIFFile, pos) != pos) {
        g->iError = GIF_EARLY_EOF;
        return false;
    }
    return true;
}

static bool GIFReadPalette(GIFIMAGE *g, uint16_t *pal, int colors) {
    uint8_t *p = g->ucFileBuf;
    if (!GIFReadExact(g, p, colors * 3)) return false;
    for (int i = 0; i < colors; i++) {
        uint8_t *rgb = p + i * 3;
        if (g->ucPaletteType == GIF_PALETTE_RGB565_LE || g->ucPaletteType == GIF_PALETTE_RGB565_BE) {
            uint16_t color = ((rgb[0] >> 3) << 11) | ((rgb[1] >> 2) << 5) | (rgb[2] >> 3);
            pal[i] = g->ucPaletteType == GIF_PALETTE_RGB565_BE ? __builtin_bswap16(color) : color;
        } else if (g->ucPaletteType == GIF_PALETTE_1BPP || g->ucPaletteType == GIF_PALETTE_1BPP_OLED) {
            ((uint8_t *)pal)[i] = (rgb[0] + 2 * rgb[1] + rgb[2]) >= 512;
        }
    }
    if (g->ucPaletteType == GIF_PALETTE_RGB888 || g->ucPaletteType == GIF_PALETTE_RGB8888)
        memcpy(pal, p, colors * 3);
    return true;
}

static int GIFParseInfo(GIFIMAGE *g, int infoOnly) {
    uint8_t b[13];
    g->iError = GIF_SUCCESS;
    g->bUseLocalPalette = g->bEndOfFrame = g->ucGIFBits = 0;
    g->iFrameDelay = 0;
    if (g->GIFFile.iPos == 0) {
        if (!GIFReadExact(g, b, 13)) return 0;
        if (memcmp(b, "GIF87a", 6) && memcmp(b, "GIF89a", 6)) goto bad;
        g->iCanvasWidth = GIF_INTELSHORT(b + 6);
        g->iCanvasHeight = GIF_INTELSHORT(b + 8);
        if (!g->iCanvasWidth || !g->iCanvasHeight || g->iCanvasWidth > MAX_WIDTH || g->iCanvasHeight > 480) goto bad;
        g->iGlobalPalSize = 0;
        g->ucBackground = b[11];
        g->iRepeatCount = -1;
        if (b[10] & 0x80) {
            g->iGlobalPalSize = 2 << (b[10] & 7);
            if (!GIFReadPalette(g, g->pPalette, g->iGlobalPalSize)) return 0;
        }
    }
    for (;;) {
        if (!GIFReadExact(g, b, 1)) return 0;
        if (b[0] == 0x3b) { g->iError = GIF_EMPTY_FRAME; return 1; }
        if (b[0] == 0x2c) break;
        if (b[0] != 0x21 || !GIFReadExact(g, b, 1)) goto bad;
        uint8_t label = b[0];
        if (label == 0xf9) {
            if (!GIFReadExact(g, b, 6)) return 0;
            if (b[0] != 4 || b[5] != 0) goto bad;
            g->ucGIFBits = b[1];
            g->iFrameDelay = GIF_INTELSHORT(b + 2) * 10;
            if (g->iFrameDelay <= 1) g->iFrameDelay = 100;
            g->ucTransparent = b[4];
        } else {
            bool netscape = false;
            int block = 0;
            if (label != 0xff && label != 0xfe && label != 1) goto bad;
            for (;;) {
                if (!GIFReadExact(g, b, 1)) return 0;
                int n = b[0];
                if (!n) break;
                if (label == 0xff && block == 0 && n == 11) {
                    if (!GIFReadExact(g, b, n)) return 0;
                    netscape = !memcmp(b, "NETSCAPE2.0", 11);
                } else if (netscape && block == 1 && n == 3) {
                    if (!GIFReadExact(g, b, n)) return 0;
                    if (b[0] == 1) g->iRepeatCount = GIF_INTELSHORT(b + 1);
                } else {
                    if (label == 0xfe && !g->iCommentPos) {
                        g->iCommentPos = g->GIFFile.iPos;
                        g->sCommentLen = n;
                    }
                    if (!GIFSkipExact(g, n)) return 0;
                }
                block++;
            }
        }
    }
    if (!GIFReadExact(g, b, 9)) return 0;
    g->iX = GIF_INTELSHORT(b); g->iY = GIF_INTELSHORT(b + 2);
    g->iWidth = GIF_INTELSHORT(b + 4); g->iHeight = GIF_INTELSHORT(b + 6);
    g->ucMap = b[8];
    if (!g->iWidth || !g->iHeight || g->iWidth > MAX_WIDTH ||
        (int)g->iX + g->iWidth > g->iCanvasWidth || (int)g->iY + g->iHeight > g->iCanvasHeight) goto bad;
    if (g->ucMap & 0x80) {
        g->iLocalPalSize = 2 << (g->ucMap & 7);
        if (!GIFReadPalette(g, g->pLocalPalette, g->iLocalPalSize)) return 0;
        g->bUseLocalPalette = 1;
    } else if (!g->iGlobalPalSize) goto bad;
    if (!GIFReadExact(g, b, 1)) return 0;
    if (b[0] < 2 || b[0] > 8) goto bad;
    g->ucCodeStart = b[0];
    g->iBpp = cGIFBits[b[0]];
    g->iLZWSize = g->iLZWOff = 0;
    // GIFInit seeks back to zero; the normal parse starts at the first data sub-block.
    (void)infoOnly;
    return 1;
bad:
    g->iError = GIF_DECODE_ERROR;
    return 0;
}

static int GIFGetMoreData(GIFIMAGE *g) {
    int left = g->iLZWSize - g->iLZWOff;
    if (left < 0 || left > LZW_BUF_SIZE) { g->iError = GIF_DECODE_ERROR; return 0; }
    if (g->iLZWOff) memmove(g->ucLZW, g->ucLZW + g->iLZWOff, left);
    g->iLZWOff = 0;
    g->iLZWSize = left;
    while (!g->bEndOfFrame && g->iLZWSize <= LZW_BUF_SIZE - MAX_CHUNK_SIZE) {
        uint8_t n;
        if (!GIFReadExact(g, &n, 1)) return 0;
        if (!n) { g->bEndOfFrame = 1; break; }
        if (!GIFReadExact(g, g->ucLZW + g->iLZWSize, n)) return 0;
        g->iLZWSize += n;
    }
    return 1;
}

static bool GIFReadCode(GIFIMAGE *g, int &bitnum, int bits, unsigned short &code) {
    g->iLZWOff += bitnum >> 3;
    bitnum &= 7;
    if (g->iLZWSize - g->iLZWOff < 3 && !GIFGetMoreData(g)) return false;
    if ((g->iLZWSize - g->iLZWOff) * 8 - bitnum < bits) { g->iError = GIF_EARLY_EOF; return false; }
    uint32_t value = 0;
    for (int i = 0; i < 3 && g->iLZWOff + i < g->iLZWSize; i++)
        value |= (uint32_t)g->ucLZW[g->iLZWOff + i] << (8 * i);
    code = (value >> bitnum) & ((1 << bits) - 1);
    bitnum += bits;
    return true;
}
