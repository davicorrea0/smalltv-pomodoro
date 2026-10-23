// gifcheck.h — conferência da estrutura de um GIF antes de entregá-lo ao decodificador.
//
// A AnimatedGIF tem falhas de estouro de memória com GIFs montados de propósito (CVE-2026-30141,
// ainda reproduzível na 2.2.3). Este verificador lê só a estrutura, sem decodificar nem alocar, e
// recusa o que um GIF de verdade não tem: medidas fora do limite, quadro saindo da tela lógica,
// tamanho de código LZW fora de 2..8 e blocos cortados ou desconhecidos. Roda no relógio (envio
// e exibição) e no teste de robustez do PC (sim/gifcheck_test), que usa este mesmo arquivo.
#pragma once
#include <stdint.h>
#include <string.h>

// lê até n bytes em sequência para buf; devolve quantos leu
typedef int (*GifCheckRead)(void *ctx, uint8_t *buf, int n);

static bool gcRead(GifCheckRead rd, void *c, uint8_t *b, int n) { return rd(c, b, n) == n; }

static bool gcSkip(GifCheckRead rd, void *c, int n) {
  uint8_t b[64];
  while (n > 0) {
    int k = n > (int)sizeof b ? (int)sizeof b : n;
    if (!gcRead(rd, c, b, k)) return false;
    n -= k;
  }
  return true;
}

// sub-blocos: tamanho (1 byte) + dados, até um tamanho 0
static bool gcSkipSub(GifCheckRead rd, void *c) {
  for (;;) {
    uint8_t n;
    if (!gcRead(rd, c, &n, 1)) return false;
    if (!n) return true;
    if (!gcSkip(rd, c, n)) return false;
  }
}

// nullptr = GIF aceito; senão, o motivo da recusa
static const char *gifCheck(GifCheckRead rd, void *c, int maxW, int maxH) {
  uint8_t b[13];
  if (!gcRead(rd, c, b, 13) || memcmp(b, "GIF8", 4) || (b[4] != '7' && b[4] != '9') || b[5] != 'a') return "nao e GIF";
  const int W = b[6] | b[7] << 8, H = b[8] | b[9] << 8;
  if (!W || !H || W > maxW || H > maxH) return "GIF grande demais";
  const bool globalPalette = b[10] & 0x80;
  if ((b[10] & 0x80) && !gcSkip(rd, c, 3 << ((b[10] & 7) + 1))) return "GIF cortado";
  int frames = 0;
  for (;;) {
    uint8_t t;
    if (!gcRead(rd, c, &t, 1)) return "GIF cortado";
    if (t == 0x3B) return frames ? nullptr : "GIF sem imagem";
    if (t == 0x21) {  // extensão: rótulo + sub-blocos
      uint8_t label;
      if (!gcRead(rd, c, &label, 1)) return "GIF cortado";
      if (label == 0xf9) {
        uint8_t gce[6];
        if (!gcRead(rd, c, gce, 6)) return "GIF cortado";
        if (gce[0] != 4 || gce[5] != 0) return "extensao invalida";
      } else {
        if (label != 0xff && label != 0xfe && label != 1) return "extensao invalida";
        if (!gcSkipSub(rd, c)) return "GIF cortado";
      }
    } else if (t == 0x2C) {  // quadro
      uint8_t d[9];
      if (!gcRead(rd, c, d, 9)) return "GIF cortado";
      const int x = d[0] | d[1] << 8, y = d[2] | d[3] << 8, w = d[4] | d[5] << 8, h = d[6] | d[7] << 8;
      if (!w || !h || x + w > W || y + h > H) return "quadro fora da imagem";
      if (!globalPalette && !(d[8] & 0x80)) return "GIF sem paleta";
      if ((d[8] & 0x80) && !gcSkip(rd, c, 3 << ((d[8] & 7) + 1))) return "GIF cortado";
      uint8_t lzw;
      if (!gcRead(rd, c, &lzw, 1)) return "GIF cortado";
      if (lzw < 2 || lzw > 8) return "compressao invalida";
      if (!gcSkipSub(rd, c)) return "GIF cortado";
      if (++frames > 2000) return "quadros demais";
    } else {
      return "bloco invalido";
    }
  }
}
