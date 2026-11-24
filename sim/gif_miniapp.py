#!/usr/bin/env python3
"""Monta PNG e GIF animado a partir dos quadros de sim/previa_animada.cpp (RGB565, 240x240).
Uso:
  gif_miniapp.py png <q000.raw> <saida.png>
  gif_miniapp.py gif <saida.gif> <ms por quadro> <escala> <x,y,w,h ou -> <pasta com q000.raw...> [<outra pasta>...]
      (as pastas em sequencia viram um GIF so; x,y,w,h recorta a tela antes de ampliar)
  gif_miniapp.py tira <saida.png> <q.raw> <q.raw> ...   (quadros lado a lado, 240 px cada)
"""
import glob
import os
import sys

from PIL import Image


def carregar(arq):
    b = open(arq, "rb").read()
    im = Image.new("RGB", (240, 240))
    px = im.load()
    for i in range(240 * 240):
        v = b[2 * i] | (b[2 * i + 1] << 8)
        px[i % 240, i // 240] = (((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31)
    return im


def main():
    modo = sys.argv[1]
    if modo == "png":
        carregar(sys.argv[2]).save(sys.argv[3], optimize=True)
    elif modo == "gif":
        saida, ms, escala, corte, pastas = sys.argv[2], int(sys.argv[3]), int(sys.argv[4]), sys.argv[5], sys.argv[6:]

        def preparar(im):
            if corte != "-":
                x, y, w, h = (int(v) for v in corte.split(","))
                im = im.crop((x, y, x + w, y + h))
            return im.resize((im.width * escala, im.height * escala), Image.NEAREST) if escala > 1 else im
        grupos = [[preparar(carregar(a)) for a in sorted(glob.glob(os.path.join(pasta, "q*.raw")))] for pasta in pastas]
        qs = [q for g in grupos for q in g]
        # uma paleta so para o GIF inteiro (sem tremida de cor entre os quadros): amostra de cada pasta, lado a lado
        amostra = Image.new("RGB", (qs[0].width, qs[0].height * len(grupos)))
        for i, g in enumerate(grupos):
            amostra.paste(g[0], (0, qs[0].height * i))
        base = amostra.quantize(colors=128, method=Image.MEDIANCUT)
        qs = [q.quantize(palette=base, dither=Image.NONE) for q in qs]
        qs[0].save(saida, save_all=True, append_images=qs[1:], duration=ms, loop=0, optimize=True, disposal=1)
    elif modo == "tira":
        saida, arqs = sys.argv[2], sys.argv[3:]
        tira = Image.new("RGB", (240 * len(arqs), 240))
        for i, a in enumerate(arqs):
            tira.paste(carregar(a), (240 * i, 0))
        tira.save(saida, optimize=True)


main()
