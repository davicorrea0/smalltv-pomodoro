#!/usr/bin/env python3
"""Gera sim/apps-seeds/tokensmall-arcade.json: o pacote "Uso de IA, arcade" com dois mascotes de pixel art.

- Claude: o caranguejo Clawd, adaptado da arte do Clawd Quest (MIT, (c) 2026 ImTBY; licenca em
  sim/apps-seeds/LICENSE-clawd-quest.txt): formas e cores tiradas dos SVGs dele, redesenhadas em quadros de 32 x 22.
- Codex: nuvem com viseira escura e ">_", desenhada do zero para este projeto (ideia de cena do TokenTV, MIT).
Nenhuma arte do Clawdmeter (sem licenca) foi usada.

Cada assistente tem uma saleta (sprites opacos, a parede faz parte do quadro) com 4 humores, um sprite animado cada:
trabalhando (digita na mesa), descansando (limite zerado), esgotado (limite cheio) e dormindo (sem dados).
O humor de cima cobre o de baixo sem camada de "tapar".

Uso: python3 sim/gerar_tokensmall_arcade.py   (reescreve sim/apps-seeds/tokensmall-arcade.json)
"""
import json
import os

W, H = 24, 20


class Grade:
    def __init__(self, w=W, h=H, fundo="W"):
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


def sala(g, deco=None):
    """Chao: linha clara + 3 linhas de madeira (a camada 'chao' da tela continua o mesmo desenho)."""
    g.r(0, 16, W, 1, "f")
    g.r(0, 17, W, 3, "F")
    if deco:
        deco(g)


def mesa(g, teclas_baixas=()):
    """Mesa de frente: tampo claro, frente escura, teclado em cima."""
    g.r(0, 13, W, 1, "e")
    g.r(0, 14, W, 2, "d")
    g.r(1, 15, 2, 1, "D")
    g.r(21, 15, 2, 1, "D")
    g.r(4, 12, 9, 1, "b")  # teclado
    for i, x in enumerate(range(5, 12, 2)):
        g.p(x, 12, "e" if i in teclas_baixas else "b")


def monitor(g, quadro, cor_a="g", cor_b="h", cursor="h"):
    """Monitor de lado, com 3 linhas de codigo que mudam a cada quadro (alguem esta digitando)."""
    g.r(15, 4, 8, 8, "m")
    g.r(16, 5, 6, 6, "s")
    g.r(18, 12, 2, 1, "m")  # pe
    padroes = [(2, 4, 3), (3, 2, 4), (4, 3, 2), (2, 3, 4), (3, 4, 3), (4, 2, 3)]
    a, b, c = padroes[quadro % len(padroes)]
    g.r(17, 6, a, 1, cor_a)
    g.r(17, 8, b, 1, cor_b)
    g.r(17, 10, c, 1, cor_a)
    if quadro % 2 == 0:
        g.p(17 + c, 10, cursor)  # cursor


# ---------------------------------------------------------------- Claude: o caranguejo "Clawd" do Clawd Quest
# Arte do Clawd Quest (https://github.com/ImTBY/clawd-quest, MIT, (c) 2026 ImTBY; licenca em LICENSE-clawd-quest.txt).
# A forma e as cores vem dos SVGs dele (corpo #D97757 de 24 x 12 celulas, bracos de 4 x 3, 4 perninhas de 2 x 3, olhos de
# 2 x 3, tom de erro #C8553D, olhos fechados, bochechas rosa) e o corpo ja e uma grade de 4 px: aqui cada celula e 1 px
# de arte, ampliada 2x na tela. As poses (digitando, comemorando, erro, dormindo) seguem as estacoes dele, redesenhadas
# em quadros de 32 x 22 para os limites do facepack.cpp.
CW, CH = 32, 22


def crab(g, y, olhos="abertos", corpo="o", brac=(0, 0), pernas=0):
    """Clawd com o corpo de cima em y. brac = (dy esquerdo, dy direito) dos bracos; pernas alterna as 2 duplas (0/1)."""
    g.r(4, y, 24, 12, corpo)
    g.r(0, y + 6 + brac[0], 4, 3, corpo)
    g.r(28, y + 6 + brac[1], 4, 3, corpo)
    for i, x in enumerate((6, 10, 20, 24)):
        g.r(x, y + 12, 2, 3, corpo)
    e1, e2 = 8, 22
    if olhos == "abertos":
        g.r(e1, y + 3, 2, 3, "k"); g.r(e2, y + 3, 2, 3, "k")
    elif olhos == "piscando":
        g.r(e1, y + 5, 2, 1, "k"); g.r(e2, y + 5, 2, 1, "k")
    elif olhos == "fechados":
        g.r(7, y + 4, 4, 1, "k"); g.r(21, y + 4, 4, 1, "k")
    elif olhos == "feliz":
        for ex in (8, 22):
            g.p(ex, y + 5, "k"); g.p(ex + 1, y + 4, "k"); g.p(ex + 2, y + 5, "k")
        g.r(5, y + 7, 2, 1, "p"); g.r(25, y + 7, 2, 1, "p")
    elif olhos == "xis":
        for ex in (8, 22):
            g.p(ex, y + 3, "k"); g.p(ex + 2, y + 3, "k"); g.p(ex + 1, y + 4, "k"); g.p(ex, y + 5, "k"); g.p(ex + 2, y + 5, "k")


def sala_c(g, deco=None):
    g.r(0, 18, CW, 1, "f")
    g.r(0, 19, CW, 3, "F")
    if deco:
        deco(g)


def claude_trabalha(q):
    g = Grade(CW, CH)
    sala_c(g)
    bob = q in (2, 3)
    crab(g, 3 if bob else 4, "piscando" if q == 4 else "abertos", brac=(1, 0) if q % 2 == 0 else (0, 1))
    # mesa na frente (o caranguejo senta atras): teclado, tampo claro, frente escura e pes
    g.r(6, 16, 20, 1, "b")
    for x in range(7, 25, 3):
        g.p(x + (q % 3), 16, "B")
    g.r(0, 17, CW, 1, "e")
    g.r(0, 18, CW, 2, "d")
    g.r(2, 20, 2, 2, "D"); g.r(28, 20, 2, 2, "D")
    # tres pontinhos de "codando" no alto, o aceso anda
    for i in range(3):
        g.p(14 + i * 2, 1, "g" if i == q % 3 else "u")
    return g.linhas()


def claude_descansa(q):
    """Comemora (a janela reiniciou): olhos ^ ^, bochechas, pula e solta confete."""
    g = Grade(CW, CH)
    sala_c(g)
    y = 3 + (-1 if q == 1 else 0)
    crab(g, y, "feliz", brac=(-3, -3) if q == 1 else (0, 0))
    conf = [[(3, 1, "g"), (9, 0, "h"), (25, 1, "y"), (29, 0, "v")], [(5, 0, "y"), (12, 1, "g"), (21, 0, "v"), (27, 1, "h")],
            [(2, 0, "v"), (16, 1, "y"), (23, 1, "h"), (30, 1, "g")]][q % 3]
    for x, yy, c in conf:
        g.p(x, yy, c)
    return g.linhas()


def claude_esgotado(q):
    """Erro (limite cheio): corpo mais vermelho, olhos X, fumaca cinza subindo."""
    g = Grade(CW, CH)
    sala_c(g)
    crab(g, 3, "xis", corpo="R")
    fum = [[(8, 2), (9, 1)], [(10, 1), (9, 0), (22, 2)], [(11, 0), (22, 1), (23, 0)]][q % 3]
    for x, yy in fum:
        g.r(x, yy, 2, 1, "u")
    return g.linhas()


def claude_dorme(q):
    g = Grade(CW, CH)
    sala_c(g, lambda g: (g.r(2, 2, 2, 1, "l"), g.p(2, 3, "l"), g.r(2, 4, 2, 1, "l")))  # lua crescente
    crab(g, 3 + (1 if q == 1 else 0), "fechados")
    zx, zy = [(26, 2), (27, 1), (28, 0)][q % 3]
    g.r(zx, zy, 3, 1, "Z"); g.p(zx + 1, zy + 1, "Z"); g.r(zx, zy + 2, 3, 1, "Z")
    return g.linhas()


# ---------------------------------------------------------------- Codex (cabeca de nuvem, viseira escura com >_)
def codex_cabeca(g, x, y, visor="normal", cursor=True):
    """x,y: canto de cima a esquerda (10 de largura, 9 de altura). Viseira: normal | feliz | xis | dormindo."""
    g.r(x + 2, y, 2, 1, "c"); g.r(x + 6, y, 3, 1, "c")  # tufos da nuvem
    g.r(x + 1, y + 1, 8, 1, "c")
    g.r(x, y + 2, 10, 5, "c")
    g.r(x + 1, y + 7, 8, 1, "C")
    g.p(x + 9, y + 3, "C")
    # viseira
    g.r(x + 1, y + 2, 8, 5, "v")
    if visor == "normal":
        g.p(x + 2, y + 3, "g"); g.p(x + 3, y + 4, "g"); g.p(x + 2, y + 5, "g")  # >
        if cursor:
            g.r(x + 5, y + 5, 3, 1, "g")  # _
    elif visor == "feliz":
        g.p(x + 2, y + 5, "g"); g.p(x + 3, y + 4, "g"); g.p(x + 4, y + 5, "g")
        g.p(x + 5, y + 5, "g"); g.p(x + 6, y + 4, "g"); g.p(x + 7, y + 5, "g")
    elif visor == "xis":
        for ox in (2, 6):
            g.p(x + ox, y + 3, "g"); g.p(x + ox + 2, y + 3, "g"); g.p(x + ox + 1, y + 4, "g")
            g.p(x + ox, y + 5, "g"); g.p(x + ox + 2, y + 5, "g")
    elif visor == "dormindo":
        g.r(x + 2, y + 4, 2, 1, "j"); g.r(x + 6, y + 4, 2, 1, "j")


def codex_corpo(g, x, y):
    """Corpinho pequeno de baixo da cabeca: tronco + 2 bracos + 2 pes."""
    g.r(x + 2, y, 6, 3, "C")
    g.r(x + 2, y, 6, 1, "c")
    g.r(x, y + 1, 2, 2, "C"); g.r(x + 8, y + 1, 2, 2, "C")
    g.r(x + 2, y + 3, 2, 2, "j"); g.r(x + 6, y + 3, 2, 2, "j")


def codex_trabalha(q):
    g = Grade()
    sala(g)
    dy = -1 if q in (3, 4) else 0
    codex_cabeca(g, 3, 3 + dy, "normal", cursor=q not in (2, 3, 4))  # o cursor da viseira pisca devagar
    monitor(g, q, "g", "k", "c")
    mesa(g, teclas_baixas=(q % 4,))
    esq_baixo = q % 2 == 0
    g.r(3, 11 if esq_baixo else 10, 3, 2, "C")
    g.r(10, 10 if esq_baixo else 11, 3, 2, "C")
    return g.linhas()


def codex_descansa(q):
    g = Grade()
    sala(g, lambda g: (g.r(2, 2, 5, 4, "z"), g.r(3, 3, 3, 2, "a")))
    dy = 1 if q in (1, 2) else 0
    g.r(4, 16, 16, 1, "t")
    codex_cabeca(g, 7, 2 + dy, "feliz")
    codex_corpo(g, 7, 10 + dy)
    cx, cy = 18, 7 + (0 if q in (1, 2) else 2)
    g.r(17, cy + 1, 1, 2, "C")
    g.r(cx, cy, 3, 3, "w"); g.p(cx + 3, cy + 1, "w"); g.r(cx, cy, 3, 1, "q")
    vp = [(19, cy - 2), (20, cy - 3)] if q % 2 == 0 else [(20, cy - 2), (19, cy - 3)]
    for vx, vy in vp:
        g.p(vx, vy, "x")
    if q == 3:
        g.p(14, 1, "r"); g.p(13, 0, "r"); g.p(15, 0, "r")
    return g.linhas()


def codex_esgotado(q):
    g = Grade()
    sala(g, lambda g: (g.r(18, 2, 5, 4, "z"), g.r(19, 3, 3, 2, "n"), g.p(19, 3, "R")))
    y = 3 + (1 if q == 1 else 0)
    codex_cabeca(g, 7, y, "xis")
    codex_corpo(g, 7, y + 8)
    fum = [[(11, y - 2), (12, y - 3)], [(12, y - 3), (11, y - 4)], [(11, y - 4), (12, y - 5), (11, y - 6)]][q % 3]
    for vx, vy in fum:
        g.r(vx, vy, 2, 1, "u")
    return g.linhas()


def codex_dorme(q):
    g = Grade()
    sala(g, lambda g: (g.r(2, 2, 2, 1, "l"), g.p(2, 3, "l"), g.r(2, 4, 2, 1, "l"), g.p(8, 3, "S"), g.p(12, 2, "S"), g.p(6, 6, "S")))
    g.r(3, 16, 19, 1, "t")
    y = 8 + (1 if q == 1 else 0)
    codex_cabeca(g, 4, y, "dormindo")
    g.r(14, y + 4, 6, 3, "C"); g.r(14, y + 4, 6, 1, "c")   # corpinho deitado ao lado da cabeca
    g.r(20, y + 5, 2, 2, "j"); g.r(14, y + 7, 2, 1, "j")
    zs = [(19, 8), (19, 7), (20, 5)]
    zx, zy = zs[q % 3]
    if q % 3 == 0:
        g.r(zx, zy, 2, 1, "Z"); g.p(zx + 1, zy + 1, "Z"); g.r(zx, zy + 2, 2, 1, "Z")
    elif q % 3 == 1:
        g.r(zx, zy, 3, 1, "Z"); g.p(zx + 2, zy + 1, "Z"); g.p(zx + 1, zy + 2, "Z"); g.r(zx, zy + 3, 3, 1, "Z")
    else:
        g.r(zx - 1, zy, 3, 1, "Z"); g.p(zx + 1, zy + 1, "Z"); g.r(zx - 1, zy + 2, 3, 1, "Z")
        g.r(19, 10, 2, 1, "Z"); g.p(20, 11, "Z"); g.r(19, 12, 2, 1, "Z")
    return g.linhas()


# ---------------------------------------------------------------- paletas (1 caractere por cor, ate 15 por sprite)
CORES = {
    "claude": {
        "W": "#3b2a3f", "F": "#5a3a2a", "f": "#7a5238", "d": "#8a5a34", "e": "#b07a46", "D": "#5e3b20",
        "o": "#D97757", "R": "#C8553D", "k": "#1F1E1D", "p": "#F2A0A0", "b": "#2a2433", "B": "#6a5f80",
        "g": "#F6D365", "u": "#8A8A82", "l": "#fff0c8", "Z": "#7B9BC9", "h": "#E5677E", "y": "#4FD6E8", "v": "#8A6CE0",
    },
    "codex": {
        "W": "#16303a", "F": "#2b3f4a", "f": "#3d5866", "d": "#4a4f6a", "e": "#7380a8", "D": "#2f3350",
        "c": "#eef0ff", "C": "#b4b9e6", "v": "#141a3a", "g": "#7dffb0", "k": "#2aa56a",
        "m": "#0c1218", "s": "#0f2a2a", "b": "#1a2430", "j": "#6c72a8",
        "z": "#5a7088", "a": "#7dffb0", "q": "#2a1a10", "x": "#dfe8f4", "w": "#f4efe6", "r": "#ff7a9a",
        "t": "#2f5a6a", "n": "#0f2a2a", "R": "#e3262a", "u": "#9aa8b8", "l": "#fff0c8", "S": "#fff0c8", "Z": "#9fe8ff",
    },
}

HUMORES = {  # abreviacao do sprite -> (desenho, quadros, fps)
    "claude": {"trab": (claude_trabalha, 5, 3), "desc": (claude_descansa, 2, 3),
               "esgo": (claude_esgotado, 2, 2), "dorm": (claude_dorme, 3, 2)},
    "codex": {"trab": (codex_trabalha, 5, 4), "desc": (codex_descansa, 3, 3),
              "esgo": (codex_esgotado, 3, 2), "dorm": (codex_dorme, 3, 2)},
}


def sprite(quem, fn, n, fps):
    frames = [fn(q) for q in range(n)]
    usados = sorted({c for q in frames for l in q for c in l})
    assert len(usados) <= 15, (quem, fn.__name__, usados)
    faltam = [c for c in usados if c not in CORES[quem]]
    assert not faltam, (quem, fn.__name__, faltam)
    return {"fps": fps, "pal": {c: CORES[quem][c] for c in usados}, "frames": frames}


def gota(x0, y0, cor):
    """Gota de suor feita de 5 barras de 1 px de largura: uma barra com max 90 so enche 1 px quando o percentual chega a 90."""
    colunas = [(0, 5, 3), (1, 3, 6), (2, 0, 10), (3, 3, 6), (4, 5, 3)]
    return [{"type": "bar", "x": x0 + dx, "y": y0 + dy, "w": 1, "h": h, "value": "VALOR", "max": 90, "color": cor}
            for dx, dy, h in colunas]


def cena(t, p):
    c, nome = p["prefixo"], p["nome"]
    v5, v5r, v7 = "x.tk_%s5" % c, "x.tk_%s5r" % c, "tk_%s7" % c
    L = [
        {"type": "rect", "x": 8, "y": t, "w": 224, "h": 112, "r": 12, "color": p["card"]},
        {"type": "rect", "x": 12, "y": t + 8, "w": 80, "h": 70, "color": p["parede"]},
        {"type": "rect", "x": 12, "y": t + 78 - p["fh"], "w": 80, "h": p["fh"], "color": p["chao"]},
        {"type": "rect", "x": 12, "y": t + 78 - p["fh"], "w": 80, "h": p["fl"], "color": p["linha"]},
        {"type": "sprite", "sprite": c + "trab", "x": p["sx"], "y": t + 78 - p["sh"], "size": p["sz"]},
        {"type": "sprite", "sprite": c + "desc", "x": p["sx"], "y": t + 78 - p["sh"], "size": p["sz"], "when": v5 + "=0"},
        {"type": "sprite", "sprite": c + "esgo", "x": p["sx"], "y": t + 78 - p["sh"], "size": p["sz"], "when": v5 + "=100"},
        {"type": "sprite", "sprite": c + "dorm", "x": p["sx"], "y": t + 78 - p["sh"], "size": p["sz"], "when": "!" + v5r},
    ]
    for b in gota(p["gx"], t + p["gy"], "#8fdcff"):
        b["value"] = v5
        L.append(b)
    L += [
        {"type": "text", "x": 52, "y": t + 86, "font": "pixel", "size": 2, "color": p["nomecor"], "text": nome, "align": "center"},
        {"type": "sprite", "sprite": "relogio", "x": 98, "y": t + 15, "size": 2},
        {"type": "text", "x": 220, "y": t + 10, "font": "pixel", "size": 3, "color": "#fff6e6", "text": "{%s}" % v5r, "align": "right", "when": v5r},
        {"type": "text", "x": 188, "y": t + 34, "font": "bebas", "size": 40, "color": p["pct"], "text": "{%s}" % v5, "align": "right", "bg": p["card"]},
        {"type": "text", "x": 194, "y": t + 50, "font": "pixel", "size": 3, "color": p["pct"], "text": "%", "when": v5r},
        {"type": "bar", "x": 100, "y": t + 80, "w": 120, "h": 7, "r": 3, "value": v5, "max": 100, "color": p["barra"], "track": p["trilho"]},
        {"type": "sprite", "sprite": "semana", "x": 98, "y": t + 90, "size": 2},
        {"type": "text", "x": 222, "y": t + 92, "font": "pixel", "size": 2, "color": "#fff6e6", "text": "{x.%s}" % v7, "align": "right"},
        {"type": "text", "x": 160, "y": t + 46, "font": "pixel", "size": 2, "color": "#ffb020",
         "text": {"pt": "SEM DADOS", "en": "NO DATA", "es": "SIN DATOS"}, "align": "center", "when": "!" + v5r},
    ]
    return L


def construir():
    sprites = {}
    for quem, pre in (("claude", "c"), ("codex", "x")):
        for hum, (fn, n, fps) in HUMORES[quem].items():
            sprites[pre + hum] = sprite(quem, fn, n, fps)
    # icones de apoio (compartilhados): relogio = tempo ate reiniciar a janela de 5 h; calendario com 7 = janela de 7 dias
    sprites["relogio"] = {"pal": {"#": "#fff6e6"}, "px": ["..###..", ".#...#.", "#..#..#", "#..##.#", "#.....#", ".#...#.", "..###.."]}
    sprites["semana"] = {"pal": {"#": "#fff6e6"}, "px": ["#######", "#######", "#.....#", "#.###.#", "#...#.#", "#..#..#", "#..#..#", "#.....#", "#######"]}
    claude = dict(prefixo="c", fh=8, fl=2, sx=16, sh=44, sz=2, gx=14, gy=36, nome="CLAUDE", card="#2a1c2e", parede="#3b2a3f", chao="#5a3a2a", linha="#7a5238",
                  nomecor="#f8b27a", mudo="#b79aa8", pct="#ffd9b8", barra="#ef7d32", trilho="#4a3040")
    codex = dict(prefixo="x", fh=12, fl=3, sx=16, sh=60, sz=3, gx=18, gy=30, nome="CODEX", card="#0f2229", parede="#16303a", chao="#2b3f4a", linha="#3d5866",
                 nomecor="#9fe8ff", mudo="#7fa0a8", pct="#c9ffe6", barra="#38d392", trilho="#1d4a45")
    layers = cena(4, claude) + cena(120, codex)
    assert len(layers) <= 48, len(layers)
    pack = {
        "format": 2, "author": "davicorrea0", "id": "tokensmall-arcade",
        "name": "Uso de IA arcade", "desc": "Mascotes de pixel art que reagem ao limite de 5 h de Claude e Codex",
        "name_en": "AI usage arcade", "desc_en": "Pixel mascots that react to the 5 h limit of Claude and Codex",
        "name_es": "Uso de IA arcade", "desc_es": "Mascotas de pixel art que reaccionan al limite de 5 h de Claude y Codex",
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
        sp = sprites[nm]
        fim = "," if i < len(nomes) - 1 else ""
        if "px" in sp:  # sprite parado (icone)
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
    print("%s: %d camadas, %d sprites" % (destino, len(layers), len(sprites)))


if __name__ == "__main__":
    construir()
