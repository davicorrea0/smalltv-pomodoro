#!/usr/bin/env python3
"""Gera sim/apps-seeds/tokensmall-arcade.json: o pacote "Uso de IA arcade" com Claude e Codex animados.

Arte:
- Claude: o caranguejo Clawd do Clawd Quest (https://github.com/ImTBY/clawd-quest, MIT, (c) 2026 ImTBY; licenca em
  sim/apps-seeds/LICENSE-clawd-quest.txt). Corpo, bracos, pernas, olhos, bone e coroa de festa saem dos SVGs dele
  (stations/coding, error, sleeping e celebration.svg), que ja sao uma grade de 4 unidades: 1 celula aqui = 4 unidades
  do SVG, ampliada 2x na tela. O quarto (mesa de madeira, monitor com codigo rolando e cursor) usa as cores dele.
- Codex: a nuvem lilas do TokenTV (https://github.com/click6067-ship-it/token-tv, pixel/codex-pixel.png, MIT,
  (c) 2026 click6067-ship-it; licenca em sim/apps-seeds/LICENSE-token-tv.txt), reduzida de 32 x 30 para 24 x 19, com
  o ">_" redesenhado como rosto (pisca, feliz, X, dormindo).
Nada do Clawdmeter (sem licenca) foi usado.

Como o movimento cabe no teto de 8192 B de sprites e 12 sprites:
- o balanco do corpo (sobe 2 px e volta em 0,6 s, como o "bob" do Clawd Quest), o pulo de comemoracao e a respiracao
  dormindo sao "motion" da camada (0 B de sprite), com passos de 1 px entre as chaves;
- os quadros do sprite so trocam bracos e olhos; a piscada do Claude e um sprite pequeno (so os olhos) com 16 quadros,
  um deles fechado (piscada curta a cada 4 s), andando junto com o corpo;
- os humores sao sprites opacos do mesmo tamanho (fundo = parede): o de cima cobre o de baixo, sem camada de "tapar";
- o quarto e feito de retangulos (0 B) e o monitor tem um sprite de codigo rolando, igual nos dois quartos.

Uso: python3 sim/gerar_tokensmall_arcade.py   (reescreve sim/apps-seeds/tokensmall-arcade.json)
"""
import json
import os

BW, BH = 32, 25  # caixa de cada humor (celulas); 2x na tela = 64 x 50 px


class Grade:
    def __init__(self, w, h, fundo="W"):
        self.w, self.h = w, h
        self.g = [[fundo] * w for _ in range(h)]

    def p(self, x, y, c):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.g[y][x] = c

    def r(self, x, y, w, h, c):
        for j in range(h):
            for i in range(w):
                self.p(x + i, y + j, c)

    def linhas(self):
        return ["".join(l) for l in self.g]


# ------------------------------------------------------------------ Claude: Clawd (Clawd Quest, MIT)
# coordenadas do SVG / 4, deslocadas 2 celulas para a esquerda (o braco esquerdo comeca em x = 8 no SVG)
# corpo x 4..27 (24), linhas 10..21 (12); bracos 4 x 3 nas linhas 16..18; pernas 2 x 3; olhos 2 x 3 nas linhas 13..15
CORPO_Y = 10


def clawd(g, olhos="abertos", corpo="o", brac=(0, 0), chapeu="bone"):
    y = CORPO_Y
    g.r(4, y, 24, 12, corpo)
    g.r(0, y + 6 + brac[0], 4, 3, corpo)
    g.r(28, y + 6 + brac[1], 4, 3, corpo)
    for x in (6, 10, 20, 24):
        g.r(x, y + 12, 2, 3, corpo)
    if olhos == "abertos":
        g.r(8, y + 3, 2, 3, "k"); g.r(22, y + 3, 2, 3, "k")
    elif olhos == "fechados":  # SVG: traço de 14 x 3 na altura do meio do olho
        g.r(7, y + 4, 4, 1, "k"); g.r(21, y + 4, 4, 1, "k")
    elif olhos == "feliz":  # SVG: "^" de traço 3
        for ex in (8, 22):
            g.p(ex - 1, y + 5, "k"); g.p(ex, y + 4, "k"); g.p(ex + 1, y + 4, "k"); g.p(ex + 2, y + 5, "k")
        g.r(5, y + 6, 3, 1, "p"); g.r(25, y + 6, 3, 1, "p")  # bochechas #F2A0A0
    elif olhos == "xis":
        for ex in (8, 22):
            for d in range(3):
                g.p(ex - 1 + d + 0, y + 3 + d, "k")
                g.p(ex + 1 - d + 0, y + 3 + d, "k")
    if chapeu == "bone":  # bone azul do Clawd Quest (hat), SVG y -36..4 -> linhas 1..10
        for (x, yy, w, h, c) in [
            (17, 1, 2, 2, "c"), (15, 7, 2, 2, "c"),                      # detalhe #F2EFE6
            (13, 3, 10, 2, "b"), (11, 5, 12, 2, "b"), (9, 7, 6, 2, "b"), (17, 7, 8, 2, "b"), (11, 9, 16, 1, "b"),
            (23, 5, 2, 2, "B"), (25, 7, 2, 2, "B"), (27, 9, 2, 1, "B"),  # sombra #2C539A
            (5, 9, 6, 1, "n"),                                            # aba #24407A
        ]:
            g.r(x, yy, w, h, c)
        g.r(5, 9, 6, 1, "N")  # borda clara da aba #5A86D6
    elif chapeu == "coroa":  # coroa de festa da comemoracao (celebration.svg), mais baixa para caber
        g.r(5, 8, 26, 2, "y")                       # faixa dourada
        for x in (5, 15, 25):
            g.r(x, 5, 2, 3, "w"); g.p(x, 4, "g")      # pontas claras com estrelinha
        g.r(9, 6, 2, 2, "v"); g.r(21, 6, 2, 2, "t")  # pedras roxa e ciano
    return g


def claude_trab(q):
    # braços alternam (tap com meio ciclo de defasagem): esquerdo desce, os dois no meio, direito desce, os dois no meio
    b = [(1, 0), (0, 0), (0, 1), (0, 0)][q]
    return clawd(Grade(BW, BH), brac=b).linhas()


def claude_desc(q):  # comemora: olhos ^ ^, bochechas, coroa, braços para cima e para baixo
    return clawd(Grade(BW, BH), "feliz", brac=(-3, -3) if q == 0 else (-1, -1), chapeu="coroa").linhas()


def claude_esgo(q):  # erro: corpo #C8553D, olhos X
    return clawd(Grade(BW, BH), "xis", corpo="R").linhas()


def claude_dorm(q):
    return clawd(Grade(BW, BH), "fechados").linhas()


def claude_olhos(q):
    """So os olhos (16 x 3, transparente): 15 quadros abertos e 1 piscando (como o blink de 3,8 s do Clawd Quest)."""
    g = Grade(16, 3, ".")
    if q == 15:
        g.r(0, 0, 2, 3, "o"); g.r(14, 0, 2, 3, "o")
        g.r(0, 1, 2, 1, "k"); g.r(14, 1, 2, 1, "k")
    else:
        g.r(0, 0, 2, 3, "k"); g.r(14, 0, 2, 3, "k")
    return g.linhas()


# ------------------------------------------------------------------ Codex: nuvem lilas do TokenTV (MIT)
# codex-pixel.png do TokenTV (32 x 30, MIT) reduzido para 24 x 19 (vizinho mais proximo) com as 5 cores lilas dele;
# os pixels escuros do ">_" viraram nuvem clara (o rosto e redesenhado em codex()). "W" = fundo.
NUVEM = [
    "WWWWWWWWssssWWWWWWWWWWWW",
    "WWWWWWWLLLLLLWWWWWWWWWWW",
    "WWWWWWLLLLLLLLWsssWWWWWW",
    "WWWWWsLLLLLLLLLLLLssWWWW",
    "WWWWllllLLLLLLLLLLLLssWW",
    "WWsslllllLLLLLLLLLLLlmmW",
    "WllllllllllllllllllllllW",
    "WllllllllllllllllllllmmW",
    "mllllllmmllllllllllllmmW",
    "sllllllmmmlllllllllllllW",
    "WMMMMlllmlmlllllllllllls",
    "WMMMMMMllMMMMlllllllllms",
    "WWsMMMMlMMMMMlllllMMMMMs",
    "WssssssMsMMMMMMMMMMMMMMW",
    "WsssssssssssMMMMMMMMssWW",
    "WWWWsssssssssssssssWWWWW",
    "WWWWWssssssssssssssWWWWW",
    "WWWWWWsssWssssssssWWWWWW",
    "WWWWWWWWWWWWsssssWWWWWWW",
]


def codex(g, rosto="normal", brac=(0, 0), cursor=True):
    ox, oy = 4, 3  # nuvem em x 4..27, linhas 3..21
    # bracinhos lilas dos lados (para digitar), atrás da nuvem
    g.r(1, 15 + brac[0], 4, 3, "M")
    g.r(27, 15 + brac[1], 4, 3, "M")
    for j, l in enumerate(NUVEM):
        for i, c in enumerate(l):
            if c != "W":
                g.p(ox + i, oy + j, c)
    fx, fy = 10, 9  # rosto, na mesma posicao do ">_" do original
    if rosto == "normal":
        g.p(fx, fy, "k"); g.p(fx + 1, fy + 1, "k"); g.p(fx + 2, fy + 2, "k"); g.p(fx + 1, fy + 3, "k"); g.p(fx, fy + 4, "k")
        if cursor:
            g.r(fx + 5, fy + 4, 5, 1, "k")
    elif rosto == "feliz":
        for ex in (fx, fx + 7):
            g.p(ex, fy + 2, "k"); g.p(ex + 1, fy + 1, "k"); g.p(ex + 2, fy + 2, "k")
        g.r(fx + 3, fy + 4, 4, 1, "k")
        g.r(fx - 3, fy + 4, 2, 1, "p"); g.r(fx + 11, fy + 4, 2, 1, "p")
    elif rosto == "xis":
        for ex in (fx, fx + 7):
            for d in range(3):
                g.p(ex + d, fy + d, "k"); g.p(ex + 2 - d, fy + d, "k")
        g.r(fx + 3, fy + 4, 4, 1, "k")
    elif rosto == "dormindo":
        g.r(fx, fy + 2, 3, 1, "k"); g.r(fx + 7, fy + 2, 3, 1, "k")
        g.r(fx + 4, fy + 4, 2, 1, "k")
    return g


def codex_trab(q):
    # 4 quadros: braços alternam a cada quadro e o "_" pisca a cada 2 (cursor de terminal)
    b = [(1, 0), (0, 0), (0, 1), (0, 0)][q]
    return codex(Grade(BW, BH), "normal", brac=b, cursor=q < 2).linhas()


def codex_desc(q):
    return codex(Grade(BW, BH), "feliz", brac=(-3, -3) if q == 0 else (-1, -1)).linhas()


def codex_esgo(q):
    g = codex(Grade(BW, BH), "xis")
    return g.linhas()


def codex_dorm(q):
    return codex(Grade(BW, BH), "dormindo", brac=(2, 2)).linhas()


# ------------------------------------------------------------------ sprites dos dois quartos
CODIGO = [  # (recuo, largura, cor) das linhas do monitor, nas cores do Clawd Quest; 4 linhas em volta, 1 por quadro
    (0, 8, "o"), (2, 11, "g"), (2, 6, "y"), (0, 4, "u"),
]


def tela(q):
    """Monitor 20 x 12: 5 linhas de codigo que sobem 1 linha por quadro, cursor piscando na ultima."""
    g = Grade(20, 12, "S")
    for i in range(5):
        rec, w, c = CODIGO[(q + i) % len(CODIGO)]
        g.r(1 + rec, 1 + 2 * i, w, 1, c)
    if q % 2 == 0:
        rec, w, c = CODIGO[(q + 4) % len(CODIGO)]
        g.r(2 + rec + w, 9, 1, 2, "o")
    return g.linhas()


def zz(q):
    g = Grade(4, 5, ".")
    g.r(0, 0, 4, 1, "Z"); g.p(2, 1, "Z"); g.p(1, 2, "Z"); g.p(0, 3, "Z"); g.r(0, 4, 4, 1, "Z")
    return g.linhas()


def fumaca(q):
    g = Grade(5, 4, ".")
    if q == 0:
        g.r(1, 0, 2, 2, "u"); g.r(0, 2, 4, 2, "u")
    else:
        g.r(0, 0, 3, 2, "u"); g.r(1, 2, 4, 2, "u")
    return g.linhas()


CORES = {
    "claude": {"W": "#262624", "o": "#D97757", "R": "#C8553D", "k": "#1F1E1D", "p": "#F2A0A0", "c": "#F2EFE6",
               "b": "#3B6FC8", "B": "#2C539A", "n": "#24407A", "N": "#5A86D6",
               "y": "#C99A2E", "w": "#E8EEF8", "g": "#F6D365", "v": "#8A6CE0", "t": "#4FD6E8"},
    "codex": {"W": "#24232e", "L": "#e4e1ff", "l": "#d0ccff", "m": "#bcb9ff", "M": "#a9aaff", "s": "#979cff",
              "k": "#222878", "p": "#F2A7C3"},
    "tela": {"S": "#141413", "o": "#D97757", "u": "#7B9BC9", "g": "#8FB07A", "y": "#E2B84A"},
    "fx": {"Z": "#7B9BC9", "u": "#8A8A82"},
}


def sprite(paleta, fn, n, fps=None):
    frames = [fn(q) for q in range(n)]
    usados = sorted({c for q in frames for l in q for c in l if c != "."})
    assert len(usados) <= 15, (fn.__name__, usados)
    pal = {c: CORES[paleta][c] for c in usados}
    if n == 1:
        return {"pal": pal, "px": frames[0]}
    return {"fps": fps, "pal": pal, "frames": frames}


def mov(period, *keys):
    return {"period": period, "keys": [{"t": t, "x": x, "y": y} for t, x, y in keys]}


def cena(t, p):
    c, nome = p["prefixo"], p["nome"]
    v5, v5r, v7 = "x.tk_%s5" % c, "x.tk_%s5r" % c, "x.tk_%s7" % c
    bx, by = 18, t + 6  # caixa do mascote (64 x 50), parada
    L = [
        {"type": "rect", "x": 8, "y": t, "w": 224, "h": 112, "r": 12, "color": p["parede"]},
        {"type": "rect", "x": 12, "y": t + 56, "w": 216, "h": 8, "color": "#4E3526"},  # chao de madeira
        # trabalhando (sempre embaixo): balanca 2 px em 0,6 s
        {"type": "sprite", "sprite": c + "trab", "size": 2, "motion": mov(600, (0, bx, by), (300, bx, by - 2), (600, bx, by))},
    ]
    if p["olhos"]:  # piscada do Claude, andando junto com o corpo
        ox, oy = bx + 16, by + 26
        L.append({"type": "sprite", "sprite": "colhos", "size": 2, "motion": mov(600, (0, ox, oy), (300, ox, oy - 2), (600, ox, oy))})
    L += [
        # descansado (janela reiniciou): comemora pulando
        {"type": "sprite", "sprite": c + "desc", "size": 2, "when": v5 + "=0",
         "motion": mov(900, (0, bx, by), (150, bx, by - 6), (300, bx, by), (900, bx, by))},
        {"type": "sprite", "sprite": c + "esgo", "x": bx, "y": by, "size": 2, "when": v5 + "=100"},
        # sem dados: dorme respirando devagar
        {"type": "sprite", "sprite": c + "dorm", "size": 2, "when": "!" + v5r,
         "motion": mov(2400, (0, bx, by), (1200, bx, by - 2), (2400, bx, by))},  # sobe: descer pintaria o chão
        {"type": "sprite", "sprite": "zz", "size": 2, "when": "!" + v5r,
         "motion": mov(2400, (0, bx + 54, by + 18), (2400, bx + 66, by - 2))},
        {"type": "sprite", "sprite": "fumaca", "size": 2, "when": v5 + "=100",
         "motion": mov(1800, (0, bx + 6, by + 14), (1800, bx, by - 4))},
    ]
    # gota de suor a 90 % ou mais: 3 barras de 1 px com max 90 (só enchem quando o percentual chega a 90)
    for dx, dy, h in ((0, 4, 4), (1, 0, 9), (2, 4, 4)):
        L.append({"type": "bar", "x": bx + 59 + dx, "y": by + 16 + dy, "w": 1, "h": h, "value": v5, "max": 90, "color": "#8fdcff"})
    L += [
        # mesa na frente (o mascote senta atrás) e monitor com código rolando, nas madeiras do Clawd Quest
        {"type": "rect", "x": 12, "y": t + 46, "w": 140, "h": 4, "color": "#8A6248"},
        {"type": "rect", "x": 16, "y": t + 50, "w": 132, "h": 6, "color": "#6B4A35"},
        {"type": "rect", "x": 94, "y": t + 16, "w": 46, "h": 30, "r": 2, "color": "#5A5A55"},
        {"type": "sprite", "sprite": "tela", "x": 97, "y": t + 19, "size": 2},
        {"type": "text", "x": 222, "y": t + 10, "font": "pixel", "size": 2, "color": p["nomecor"], "text": nome, "align": "right"},
        # dados: barra de 5 h no rodapé do quarto, percentual grande, tempo até reiniciar e 7 dias
        {"type": "bar", "x": 12, "y": t + 64, "w": 216, "h": 4, "value": v5, "max": 100, "color": p["barra"], "track": "#3a3936"},
        {"type": "text", "x": 62, "y": t + 73, "font": "bebas", "size": 32, "color": "#F2EFE6", "text": "{%s}" % v5, "align": "right", "bg": p["parede"]},
        {"type": "text", "x": 64, "y": t + 89, "font": "pixel", "size": 2, "color": "#F2EFE6", "text": "%", "when": v5r},
        # rótulo e valor no mesmo texto: "5H 2h13" e "7D 71% 3d" (o pior caso, "7D 100% 5h59", vai de x 78 a 222)
        {"type": "text", "x": 222, "y": t + 73, "font": "pixel", "size": 2, "color": "#F2EFE6", "text": "5H {%s}" % v5r, "align": "right", "when": v5r},
        {"type": "text", "x": 222, "y": t + 92, "font": "pixel", "size": 2, "color": "#F2EFE6", "text": "7D {%s}" % v7, "align": "right"},
        {"type": "text", "x": 222, "y": t + 73, "font": "pixel", "size": 2, "color": "#E2B84A",
         "text": {"pt": "SEM DADOS", "en": "NO DATA", "es": "SIN DATOS"}, "align": "right", "when": "!" + v5r},
    ]
    return L


def construir():
    sprites = {
        "ctrab": sprite("claude", claude_trab, 4, 10),
        "colhos": sprite("claude", claude_olhos, 16, 4),
        "cdesc": sprite("claude", claude_desc, 2, 3),
        "cesgo": sprite("claude", claude_esgo, 1),
        "cdorm": sprite("claude", claude_dorm, 1),
        "xtrab": sprite("codex", codex_trab, 4, 8),
        "xdesc": sprite("codex", codex_desc, 2, 3),
        "xesgo": sprite("codex", codex_esgo, 1),
        "xdorm": sprite("codex", codex_dorm, 1),
        "tela": sprite("tela", tela, 4, 2),
        "zz": sprite("fx", zz, 1),
        "fumaca": sprite("fx", fumaca, 2, 3),
    }
    claude = dict(prefixo="c", nome="CLAUDE", parede="#262624", nomecor="#D97757", barra="#D97757", olhos=True)
    codex_ = dict(prefixo="x", nome="CODEX", parede="#24232e", nomecor="#bcb9ff", barra="#979cff", olhos=False)
    layers = cena(4, claude) + cena(120, codex_)
    assert len(layers) <= 48, len(layers)
    assert sum(1 for l in layers if "motion" in l) <= 12
    cru = sum(4 + 30 + len(s.get("frames") or [s["px"]]) * ((len((s.get("frames") or [s["px"]])[0][0]) *
              len((s.get("frames") or [s["px"]])[0]) + 1) // 2) for s in sprites.values())
    assert cru <= 8192, cru
    pack = {
        "format": 2, "author": "davicorrea0", "id": "tokensmall-arcade",
        "name": "Uso de IA arcade", "desc": "Clawd e a nuvem do Codex reagem ao limite de 5 h (arte MIT: Clawd Quest, TokenTV)",
        "name_en": "AI usage arcade", "desc_en": "Clawd and the Codex cloud react to the 5 h limit (MIT art: Clawd Quest, TokenTV)",
        "name_es": "Uso de IA arcade", "desc_es": "Clawd y la nube de Codex reaccionan al limite de 5 h (arte MIT)",
        "bg": "#17141f",
    }
    destino = os.path.join(os.path.dirname(os.path.abspath(__file__)), "apps-seeds", "tokensmall-arcade.json")

    def compacto(o):
        return json.dumps(o, ensure_ascii=False, separators=(",", ":"))
    out = ["{"]
    for k, v in pack.items():
        out.append("  %s: %s," % (json.dumps(k), compacto(v)))
    out.append('  "sprites": {')
    nomes = list(sprites)
    for i, nm in enumerate(nomes):
        sp, fim = sprites[nm], ("," if i < len(nomes) - 1 else "")
        if "px" in sp:
            out.append("    %s: %s%s" % (json.dumps(nm), compacto(sp), fim))
            continue
        out.append('    %s: {"fps":%d,"pal":%s,"frames":[' % (json.dumps(nm), sp["fps"], compacto(sp["pal"])))
        for j, q in enumerate(sp["frames"]):
            out.append("      " + compacto(q) + ("," if j < len(sp["frames"]) - 1 else ""))
        out.append("    ]}" + fim)
    out.append("  },")
    out.append('  "layers": [')
    for i, l in enumerate(layers):
        out.append("    " + compacto(l) + ("," if i < len(layers) - 1 else ""))
    out.append("  ]")
    out.append("}")
    with open(destino, "w", encoding="utf8", newline="\n") as f:
        f.write("\n".join(out) + "\n")
    print("%s: %d camadas, %d sprites, %d B de sprites (crus), %d com motion" % (
        destino, len(layers), len(sprites), cru, sum(1 for l in layers if "motion" in l)))


if __name__ == "__main__":
    construir()
