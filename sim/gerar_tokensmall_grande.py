#!/usr/bin/env python3
"""Gera sim/apps-seeds/tokensmall-clawd.json e tokensmall-codex.json: um mascote grande por pacote, que reage ao limite de 5 h.

Um pacote por assistente: o mascote ocupa o meio da tela (96 px de largura, sprites de 32 celulas em "size" 3), com o
chao embaixo e os numeros na faixa de baixo. Cada pacote le so os 3 campos do seu assistente (tk_c5, tk_c5r, tk_c7 ou
tk_x5, tk_x5r, tk_x7), que o companheiro ja manda (#168): firmware e companheiro nao mudam.

Arte:
- Clawd: o caranguejo do Clawd Quest (https://github.com/ImTBY/clawd-quest, MIT, (c) 2026 ImTBY; licenca em
  sim/apps-seeds/LICENSE-clawd-quest.txt). Corpo, bracos, pernas, olhos, bone azul e cores vem dos SVGs dele
  (stations/coding, error, sleeping e celebration.svg), na grade de 4 unidades do SVG (1 celula = 3 px na tela). As poses
  esticada e amassada da comemoracao sao variacoes desenhadas aqui sobre o mesmo corpo.
- Codex: a nuvem lilas do TokenTV (https://github.com/click6067-ship-it/token-tv, pixel/codex-pixel.png, MIT,
  (c) 2026 click6067-ship-it; licenca em sim/apps-seeds/LICENSE-token-tv.txt), reduzida para 24 x 19; o rosto (">_"
  digitando, X, ^ ^, dormindo), os bracinhos e as versoes esticada e amassada sao redesenhados aqui.
- Cena (mascote grande no meio, chao simples, barra de uso embaixo): o arranjo do TokenTV.
Nada do Clawdmeter (sem licenca) foi usado.

Como cabe no teto (8192 B crus de sprites, contados a 4 bits por celula; 12 sprites; 12 camadas com "motion"):
- balanco, pulo, tremor e respiracao sao "motion" da camada (0 B), em passos de 1 px;
- trabalhando = corpo com 8 quadros (bracos e pe) + olhos/rosto com 16 quadros (piscar, olhar, digitar), que andam
  juntos: 16 poses diferentes por ciclo pelo preco de 8;
- comemorando = 8 quadros com squash-and-stretch (amassa, estica no ar, amassa ao cair), alinhados com o pulo do motion;
- o bone do Clawd e um sprite so, com um motion por humor (no pulo ele e jogado para cima e cai de volta na cabeca);
- a gota de suor (90 % ou mais) e de barras de 1 px com "max" 90, que so enchem a partir de 90.

Uso: python3 sim/gerar_tokensmall_grande.py   (reescreve os dois .json)
"""
import json
import os

S = 3                 # celula -> 3 px
MX = 72               # x do sprite de 32 celulas (96 px, centrado)
CHAO = 148            # y do chao


class Grade:
    def __init__(self, w, h, fundo="."):
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


# ====================================================================== Clawd (Clawd Quest, MIT)
# Corpo normal (grade do SVG / 4): 24 x 12, bracos 4 x 3 na linha 6 do corpo, 4 pernas 2 x 3, olhos 2 x 3 na linha 3.
CH = 15  # altura do sprite do corpo (12 de corpo + 3 de pernas)


def corpo(g, x, y, w=24, h=12, cor="o", bracos="lado", brac=(0, 0), pernas=3, pe=None):
    """Corpo de w x h com o canto em (x, y); pernas embaixo (pernas = altura), bracos conforme a pose."""
    g.r(x, y, w, h, cor)
    for i, dx in enumerate((2, 6, 16, 20)):
        px = x + round(dx * w / 24)
        ph = pernas - (1 if pe == i else 0)  # pe levantado: perna 1 celula mais curta
        g.r(px, y + h, 2, ph, cor)
    if bracos == "lado":  # 4 x 3 dos lados, na linha 6 do corpo (+ deslocamento de cada braco)
        ay = y + round(6 * h / 12)
        g.r(x - 4, ay + brac[0], 4, 3, cor)
        g.r(x + w, ay + brac[1], 4, 3, cor)
    elif bracos == "cima":  # bracos levantados: 3 x 5 colados ao corpo, subindo acima da cabeca
        g.r(x - 3, y - 2 + brac[0], 3, 5, cor)
        g.r(x + w, y - 2 + brac[1], 3, 5, cor)
    elif bracos == "aberto":  # amassado: bracos esticados para fora, finos
        ay = y + h // 2 - 1
        g.r(x - 3, ay, 3, 2, cor)
        g.r(x + w, ay, 3, 2, cor)


def olhos_em(g, x, y, w, h, tipo):
    """Olhos sobre o corpo (x, y, w, h): posicoes 4 e 18 de 24 escaladas."""
    ey = y + round(3 * h / 12)
    for ex in (x + round(4 * w / 24), x + round(18 * w / 24)):
        if tipo == "abertos":
            g.r(ex, ey, 2, 3, "k")
        elif tipo == "fechados":
            g.r(ex - 1, ey + 1, 4, 1, "k")
        elif tipo == "feliz":  # ^ do celebration.svg
            g.p(ex - 1, ey + 2, "k"); g.p(ex, ey + 1, "k"); g.p(ex + 1, ey + 1, "k"); g.p(ex + 2, ey + 2, "k")
        elif tipo == "xis":
            for d in range(3):
                g.p(ex - 1 + d, ey + d, "k"); g.p(ex + 1 - d, ey + d, "k")
    if tipo == "feliz":  # bochechas #F2A0A0
        g.r(x + round(1 * w / 24), ey + 3, 3, 1, "p"); g.r(x + w - round(1 * w / 24) - 3, ey + 3, 3, 1, "p")


def clawd_trab(q):
    """8 quadros a 8 fps: os bracos digitam alternados (meio ciclo de defasagem) e um pe bate no 3 e no 7."""
    g = Grade(32, CH)
    brac = [(1, 0), (0, 0), (0, 1), (0, 0)][q % 4]
    corpo(g, 4, 0, brac=brac, pe=3 if q in (2, 6) else (0 if q == 4 else None))
    return g.linhas()


def clawd_olhos(q):
    """16 quadros a 4 fps (4 s), so os olhos (18 x 3): olha a tela, pisca, olha para os lados, pisca duas vezes."""
    g = Grade(18, 3)
    seq = "cccccBcRRRccLLcB"
    m = seq[q]
    if m == "B":
        g.r(0, 1, 4, 1, "k"); g.r(14, 1, 4, 1, "k")
    else:
        dx = {"c": 1, "R": 2, "L": 0}[m]
        g.r(dx, 0, 2, 3, "k"); g.r(dx + 14, 0, 2, 3, "k")
    return g.linhas()


def clawd_erro(q):
    """No limite (pose error.svg): corpo #C8553D, olhos X, bracos se debatendo; 8 quadros a 8 fps."""
    g = Grade(32, CH + 2)  # 2 linhas a mais em cima para os bracos levantados
    pose = q % 4
    if pose in (0, 2):
        corpo(g, 4, 2, cor="R", bracos="cima", brac=(0, 2) if pose == 0 else (2, 0))
    else:
        corpo(g, 4, 2, cor="R", brac=(-2, -2) if pose == 1 else (-1, -1))
    olhos_em(g, 4, 2, 24, 12, "xis")
    # boca aberta de susto nos quadros 4 a 7
    if q >= 4:
        g.r(15, 2 + 8, 2, 2, "k")
    return g.linhas()


# comemoracao: caixa de 32 x 18 (3 linhas a mais em cima para esticar); o corpo normal fica nas linhas 3..17
FESTA = [  # (largura, altura, pernas, bracos, brac) de cada quadro; a base (pes) fica sempre na linha 17
    (24, 12, 3, "lado", (0, 0)),    # 0 parado
    (26, 9, 2, "aberto", None),     # 1 amassa (antecipa o pulo)
    (20, 14, 3, "cima", (0, 0)),    # 2 estica (sobe)
    (20, 14, 3, "cima", (1, 0)),    # 3 no ar, acenando
    (22, 13, 3, "cima", (0, 1)),    # 4 caindo
    (24, 12, 3, "cima", (0, 0)),    # 5 toca o chao
    (26, 9, 2, "aberto", None),     # 6 amassa (cai)
    (24, 12, 3, "lado", (-2, -2)),  # 7 volta
]


def clawd_festa(q):
    g = Grade(32, 18)
    w, h, pern, br, brac = FESTA[q]
    x, y = 16 - w // 2, 18 - pern - h
    corpo(g, x, y, w, h, bracos=br, brac=brac or (0, 0), pernas=pern)
    olhos_em(g, x, y, w, h, "feliz")
    return g.linhas()


def clawd_dorme(q):
    g = Grade(32, CH)
    corpo(g, 4, 0, brac=(2, 2))
    olhos_em(g, 4, 0, 24, 12, "fechados")
    return g.linhas()


def bone(q):
    """Bone azul do Clawd Quest (hat do coding.svg), linhas 1..9 do SVG/4, x 5..28 -> sprite 24 x 9."""
    g = Grade(24, 9)
    for (x, y, w, h, c) in [
        (17, 1, 2, 2, "c"), (15, 7, 2, 2, "c"),
        (13, 3, 10, 2, "b"), (11, 5, 12, 2, "b"), (9, 7, 6, 2, "b"), (17, 7, 8, 2, "b"), (11, 9, 16, 1, "b"),
        (23, 5, 2, 2, "B"), (25, 7, 2, 2, "B"), (27, 9, 2, 1, "B"),
        (5, 9, 6, 1, "N"),
    ]:
        g.r(x - 5, y - 1, w, h, c)
    return g.linhas()


# ====================================================================== Codex (nuvem do TokenTV, MIT)
NUVEM = [  # codex-pixel.png (32 x 30) reduzido para 24 x 19; o ">_" escuro virou nuvem clara (o rosto e outra camada)
    "........ssss............",
    ".......LLLLLL...........",
    "......LLLLLLLL.sss......",
    ".....sLLLLLLLLLLLLss....",
    "....llllLLLLLLLLLLLLss..",
    "..sslllllLLLLLLLLLLLlmm.",
    ".llllllllllllllllllllll.",
    ".llllllllllllllllllllmm.",
    "mllllllmmllllllllllllmm.",
    "sllllllmmmlllllllllllll.",
    ".MMMMlllmlmlllllllllllls",
    ".MMMMMMllMMMMlllllllllms",
    "..sMMMMlMMMMMlllllMMMMMs",
    ".ssssssMsMMMMMMMMMMMMMM.",
    ".sssssssssssMMMMMMMMss..",
    "....sssssssssssssss.....",
    ".....ssssssssssssss.....",
    "......sss.ssssssss......",
    "............sssss.......",
]
VERMELHO = {"L": "e", "l": "f", "m": "g", "M": "h", "s": "i"}  # a mesma nuvem em tons de vermelho (no limite)
NH = 19
FX, FY = 6, 6  # canto do rosto na nuvem (onde ficava o ">_")


def nuvem(g, x, y, w=24, h=NH, troca=None):
    """Nuvem reamostrada (vizinho mais proximo) para w x h, com o canto em (x, y)."""
    for j in range(h):
        for i in range(w):
            c = NUVEM[min(NH - 1, j * NH // h)][min(23, i * 24 // w)]
            if c != ".":
                g.p(x + i, y + j, troca.get(c, c) if troca else c)


def rosto(g, x, y, tipo):
    if tipo == "xis":
        for ex in (x, x + 7):
            for d in range(3):
                g.p(ex + d, y + d, "k"); g.p(ex + 2 - d, y + d, "k")
        g.r(x + 3, y + 4, 4, 1, "k")
    elif tipo == "feliz":
        for ex in (x, x + 7):
            g.p(ex, y + 2, "k"); g.p(ex + 1, y + 1, "k"); g.p(ex + 2, y + 2, "k")
        g.r(x + 3, y + 4, 4, 1, "k")
        g.r(x - 3, y + 4, 2, 1, "p"); g.r(x + 11, y + 4, 2, 1, "p")
    elif tipo == "dormindo":
        g.r(x, y + 2, 3, 1, "k"); g.r(x + 7, y + 2, 3, 1, "k")
        g.r(x + 4, y + 4, 2, 1, "k")


def bracinhos(g, y, brac, cor="M", cima=False):
    if cima:
        g.r(1, y - 6 + brac[0], 3, 5, cor); g.r(28, y - 6 + brac[1], 3, 5, cor)
    else:
        g.r(0, y + brac[0], 4, 3, cor); g.r(28, y + brac[1], 4, 3, cor)


def codex_trab(q):
    """4 quadros a 8 fps: bracinhos digitando alternados; o rosto e a camada de cima."""
    g = Grade(32, NH)
    bracinhos(g, 9, [(1, 0), (0, 0), (0, 1), (0, 0)][q])
    nuvem(g, 4, 0)
    return g.linhas()


def codex_rosto(q):
    """16 quadros a 4 fps (4 s), 14 x 5: o prompt ">" e um comando sendo digitado, com o cursor "_" piscando."""
    g = Grade(14, 5)
    for (x, y) in ((0, 0), (1, 1), (2, 2), (1, 3), (0, 4)):
        g.p(x, y, "k")
    k = [0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 4, 4, 0, 0, 0, 0][q]  # letras digitadas
    for i in range(k):
        g.r(4 + 2 * i, 3, 1, 2, "k")
    if q % 2 == 0 or k == 0:
        g.r(4 + 2 * k, 4, 2 if k == 4 else 3, 1, "k")
    return g.linhas()


def codex_erro(q):
    g = Grade(32, NH)
    pose = q % 4
    if pose in (0, 2):
        bracinhos(g, 9, (0, 2) if pose == 0 else (2, 0), "h", cima=True)
    else:
        bracinhos(g, 9, (-2, -2) if pose == 1 else (-1, -1), "h")
    nuvem(g, 4, 0, troca=VERMELHO)
    rosto(g, 4 + FX, FY, "xis")
    if q >= 4:
        g.r(4 + FX + 4, FY + 3, 2, 2, "k")
    return g.linhas()


CFESTA = [  # (largura, altura, bracos para cima) por quadro; a base fica na linha 20
    (24, 19, False), (28, 15, False), (20, 21, True), (20, 21, True),
    (22, 20, True), (24, 19, True), (28, 15, False), (24, 19, False),
]


def codex_festa(q):
    g = Grade(32, 21)
    w, h, cima = CFESTA[q]
    x, y = 16 - w // 2, 21 - h
    bracinhos(g, y + round(9 * h / NH), (1, 0) if q == 3 else (0, 0), cima=cima)
    nuvem(g, x, y, w, h)
    rosto(g, x + round(FX * w / 24), y + round(FY * h / NH), "feliz")
    return g.linhas()


def codex_dorme(q):
    g = Grade(32, NH)
    bracinhos(g, 9, (2, 2))
    nuvem(g, 4, 0)
    rosto(g, 4 + FX, FY, "dormindo")
    return g.linhas()


# ====================================================================== efeitos (iguais nos dois)
def zz(q):
    g = Grade(5, 5)
    g.r(0, 0, 5, 1, "Z"); g.p(3, 1, "Z"); g.p(2, 2, "Z"); g.p(1, 3, "Z"); g.r(0, 4, 5, 1, "Z")
    return g.linhas()


def fumaca(q):
    g = Grade(8, 6)
    for (x, y, w, h) in [((1, 3, 3, 3), (4, 1, 3, 3)), ((0, 2, 4, 3), (4, 0, 3, 3)), ((2, 3, 3, 3), (3, 0, 4, 3))][q]:
        g.r(x, y, w, h, "u")
    return g.linhas()


CONF = [(2, "y"), (7, "t"), (12, "v"), (17, "w"), (22, "p"), (27, "y"), (30, "t"), (5, "w"), (15, "o"), (25, "v")]


def confete(q):
    """32 x 9, 4 quadros: papeizinhos caindo 1 celula por quadro (cada um numa fase)."""
    g = Grade(32, 9)
    for i, (x, c) in enumerate(CONF):
        y = (i * 3 + q * 2) % 9
        g.p(x, y, c)
        if (i + q) % 2:
            g.p(x + 1, y, c)
    return g.linhas()


CORES = {"o": "#D97757", "R": "#C8553D", "k": "#1F1E1D", "p": "#F2A0A0", "c": "#F2EFE6",
         "b": "#3B6FC8", "B": "#2C539A", "N": "#5A86D6",
         "L": "#e4e1ff", "l": "#d0ccff", "m": "#bcb9ff", "M": "#a9aaff", "s": "#979cff",
         "e": "#ffd6cf", "f": "#f4a493", "g": "#e8806b", "h": "#d4634f", "i": "#b84a3a",
         "Z": "#7B9BC9", "u": "#8A8A82", "y": "#F6D365", "t": "#4FD6E8", "v": "#8A6CE0", "w": "#E8EEF8"}
CODEX_K = "#222878"  # rosto escuro do TokenTV


def sprite(fn, n, fps=None, k=None):
    frames = [fn(q) for q in range(n)]
    usados = sorted({c for q in frames for l in q for c in l if c != "."})
    assert len(usados) <= 15, (fn.__name__, usados)
    pal = {c: (k if c == "k" and k else CORES[c]) for c in usados}
    if n == 1:
        return {"pal": pal, "px": frames[0]}
    return {"fps": fps, "pal": pal, "frames": frames}


def cru(sprites):
    t = 0
    for s in sprites.values():
        fr = s.get("frames") or [s["px"]]
        t += 4 + 30 + len(fr) * ((len(fr[0][0]) * len(fr[0]) + 1) // 2)
    return t


def mov(period, *keys):
    return {"period": period, "keys": [{"t": t, "x": x, "y": y} for t, x, y in keys]}


def balanco(x, y, d=3, period=600):  # sobe d px e volta (o "bob" do Clawd Quest: 4 px em 0,6 s)
    return mov(period, (0, x, y), (period // 2, x, y - d), (period, x, y))


def tremor(x, y):
    return mov(300, (0, x, y), (75, x + 2, y), (150, x, y), (225, x - 2, y), (300, x, y))


def respira(x, y):
    return mov(2400, (0, x, y), (1200, x, y - 2), (2400, x, y))


# pulo da comemoracao: 1 s, quadros de 125 ms (8 fps): amassa no 1 (125-250), sobe de 250 a 450, cai ate 700,
# amassa no 6 (750-875)
def pulo(x, y, alto=21):
    return mov(1000, (0, x, y), (250, x, y), (450, x, y - alto), (700, x, y), (1000, x, y))


def textos(v5, v5r, v7, barra, parede):
    return [
        {"type": "text", "x": 12, "y": 12, "font": "pixel", "size": 2, "color": "#8A8A82", "text": "7D {%s}" % v7},
        {"type": "bar", "x": 12, "y": 160, "w": 216, "h": 6, "r": 3, "value": v5, "max": 100, "color": barra, "track": "#3a3936"},
        {"type": "text", "x": 92, "y": 174, "font": "bebas", "size": 52, "color": "#F2EFE6", "text": "{%s}" % v5, "align": "right", "bg": parede},
        {"type": "text", "x": 96, "y": 202, "font": "pixel", "size": 3, "color": "#F2EFE6", "text": "%", "when": v5r},
        {"type": "text", "x": 228, "y": 180, "font": "pixel", "size": 2, "color": "#8A8A82", "text": {"pt": "REINICIA", "en": "RESETS", "es": "REINICIA"}, "align": "right", "when": v5r},
        {"type": "text", "x": 228, "y": 200, "font": "pixel", "size": 3, "color": "#F2EFE6", "text": "{%s}" % v5r, "align": "right", "when": v5r},
        {"type": "text", "x": 228, "y": 190, "font": "pixel", "size": 2, "color": "#E2B84A",
         "text": {"pt": "SEM DADOS", "en": "NO DATA", "es": "SIN DATOS"}, "align": "right", "when": "!" + v5r},
    ]


def suor(v5, x, y):
    """Gota de suor a 90 % ou mais: colunas de 1 px com max 90 (so enchem a partir de 90); escondida no limite."""
    L = []
    for dx, dy, h in ((0, 6, 5), (1, 3, 9), (2, 0, 13), (3, 3, 9), (4, 6, 5)):
        L.append({"type": "bar", "x": x + dx, "y": y + dy, "w": 1, "h": h, "value": v5, "max": 90, "color": "#8fdcff",
                  "when": "!" + v5 + "=100"})
    return L


def chao(parede, sombra_x, sombra_w):
    return [
        {"type": "rect", "x": 0, "y": CHAO, "w": 240, "h": 2, "color": "#3a3936"},
        {"type": "rect", "x": sombra_x, "y": CHAO + 2, "w": sombra_w, "h": 3, "r": 1, "color": "#151514"},
    ]


def pacote_clawd():
    v5, v5r, v7 = "x.tk_c5", "x.tk_c5r", "x.tk_c7"
    parede = "#262624"
    by = CHAO - CH * S        # topo do corpo (103)
    cx, cy = MX + 5 * S, by - 9 * S  # bone: x 5 do corpo, 9 linhas acima do topo
    trab = v5                 # trabalhando: leitura de 1 a 100 (no 100 o "tapa" abaixo cobre)
    L = chao(parede, MX + 12, 72)
    L += [
        {"type": "sprite", "sprite": "trab", "size": S, "when": trab, "motion": balanco(MX, by)},
        {"type": "sprite", "sprite": "olhos", "size": S, "when": trab, "motion": balanco(MX + 7 * S, by + 3 * S)},
        {"type": "sprite", "sprite": "bone", "size": S, "when": trab, "motion": balanco(cx, cy)},
    ]
    L += suor(v5, MX + 33 * S, by - 4)
    L += suor(v5, MX - 4 * S, by + 2)
    L += [
        # no limite: tapa o mascote de trabalho (x.tk_c5 = "100" tambem e verdadeiro)
        {"type": "rect", "x": MX - 18, "y": cy - 6, "w": 96 + 36, "h": CHAO - cy + 6, "color": parede, "when": v5 + "=100"},
        {"type": "sprite", "sprite": "erro", "size": S, "when": v5 + "=100", "motion": tremor(MX, by - 2 * S)},
        {"type": "sprite", "sprite": "bone", "size": S, "when": v5 + "=100", "motion": tremor(cx, cy)},
        {"type": "sprite", "sprite": "fumaca", "size": S, "when": v5 + "=100",
         "motion": mov(1500, (0, MX + 22 * S, cy + 6), (1500, MX + 26 * S, cy - 30))},
        # descansado: comemora pulando, joga o bone para cima e pega de volta
        {"type": "sprite", "sprite": "confete", "size": S, "when": v5 + "=0",
         "motion": mov(1000, (0, MX, 32), (1000, MX, 38))},
        {"type": "sprite", "sprite": "festa", "size": S, "when": v5 + "=0", "motion": pulo(MX, by - 3 * S)},
        {"type": "sprite", "sprite": "bone", "size": S, "when": v5 + "=0",
         "motion": mov(1000, (0, cx, cy), (125, cx, cy), (500, cx, cy - 48), (875, cx, cy), (1000, cx, cy))},
        # sem dados: dorme respirando, com Z subindo
        {"type": "sprite", "sprite": "dorme", "size": S, "when": "!" + v5r, "motion": respira(MX, by)},
        {"type": "sprite", "sprite": "bone", "size": S, "when": "!" + v5r, "motion": respira(cx, cy)},
        {"type": "sprite", "sprite": "zz", "size": S, "when": "!" + v5r,
         "motion": mov(2400, (0, MX + 27 * S, by - 6), (2400, MX + 32 * S, by - 50))},
    ]
    L += textos(v5, v5r, v7, "#D97757", parede)
    sprites = {
        "trab": sprite(clawd_trab, 8, 8),
        "olhos": sprite(clawd_olhos, 16, 4),
        "bone": sprite(bone, 1),
        "erro": sprite(clawd_erro, 8, 8),
        "festa": sprite(clawd_festa, 8, 8),
        "dorme": sprite(clawd_dorme, 1),
        "zz": sprite(zz, 1),
        "fumaca": sprite(fumaca, 3, 5),
        "confete": sprite(confete, 4, 6),
    }
    meta = {
        "format": 2, "author": "davicorrea0", "id": "tokensmall-clawd",
        "name": "Uso de IA: Clawd", "desc": "Clawd grande que reage ao limite de 5 h do Claude (arte MIT: Clawd Quest)",
        "name_en": "AI usage: Clawd", "desc_en": "Big Clawd reacting to Claude's 5 h limit (MIT art: Clawd Quest)",
        "name_es": "Uso de IA: Clawd", "desc_es": "Clawd grande que reacciona al limite de 5 h de Claude (arte MIT)",
        "bg": parede,
    }
    return meta, sprites, L


def pacote_codex():
    v5, v5r, v7 = "x.tk_x5", "x.tk_x5r", "x.tk_x7"
    parede = "#24232e"
    by = CHAO - 8 - NH * S    # a nuvem flutua 8 px acima do chao (85)
    L = chao(parede, MX + 18, 60)
    L += [
        {"type": "sprite", "sprite": "trab", "size": S, "when": v5, "motion": balanco(MX, by, 4, 1200)},
        {"type": "sprite", "sprite": "rosto", "size": S, "when": v5,
         "motion": balanco(MX + (4 + FX) * S, by + FY * S, 4, 1200)},
    ]
    L += suor(v5, MX + 30 * S, by)
    L += suor(v5, MX - 3 * S, by + 6)
    L += [
        {"type": "rect", "x": MX - 18, "y": by - 24, "w": 96 + 36, "h": CHAO - by + 24, "color": parede, "when": v5 + "=100"},
        {"type": "sprite", "sprite": "erro", "size": S, "when": v5 + "=100", "motion": tremor(MX, by)},
        {"type": "sprite", "sprite": "fumaca", "size": S, "when": v5 + "=100",
         "motion": mov(1500, (0, MX + 18 * S, by - 6), (1500, MX + 22 * S, by - 40))},
        {"type": "sprite", "sprite": "confete", "size": S, "when": v5 + "=0",
         "motion": mov(1000, (0, MX, 28), (1000, MX, 34))},
        {"type": "sprite", "sprite": "festa", "size": S, "when": v5 + "=0", "motion": pulo(MX, by - S, 18)},
        {"type": "sprite", "sprite": "dorme", "size": S, "when": "!" + v5r, "motion": respira(MX, by + 6)},
        {"type": "sprite", "sprite": "zz", "size": S, "when": "!" + v5r,
         "motion": mov(2400, (0, MX + 26 * S, by + 4), (2400, MX + 31 * S, by - 40))},
    ]
    L += textos(v5, v5r, v7, "#979cff", parede)
    sprites = {
        "trab": sprite(codex_trab, 4, 8),
        "rosto": sprite(codex_rosto, 16, 4, CODEX_K),
        "erro": sprite(codex_erro, 8, 8, CODEX_K),
        "festa": sprite(codex_festa, 8, 8, CODEX_K),
        "dorme": sprite(codex_dorme, 1, k=CODEX_K),
        "zz": sprite(zz, 1),
        "fumaca": sprite(fumaca, 3, 5),
        "confete": sprite(confete, 4, 6),
    }
    meta = {
        "format": 2, "author": "davicorrea0", "id": "tokensmall-codex",
        "name": "Uso de IA: Codex", "desc": "Nuvem do Codex grande que reage ao limite de 5 h (arte MIT: TokenTV)",
        "name_en": "AI usage: Codex", "desc_en": "Big Codex cloud reacting to the 5 h limit (MIT art: TokenTV)",
        "name_es": "Uso de IA: Codex", "desc_es": "Nube de Codex grande que reacciona al limite de 5 h (arte MIT)",
        "bg": parede,
    }
    return meta, sprites, L


def escrever(nome, meta, sprites, layers):
    assert len(layers) <= 48, len(layers)
    nm = sum(1 for l in layers if "motion" in l)
    assert nm <= 12, nm
    assert len(sprites) <= 12
    c = cru(sprites)
    assert c <= 8192, c
    destino = os.path.join(os.path.dirname(os.path.abspath(__file__)), "apps-seeds", nome)

    def compacto(o):
        return json.dumps(o, ensure_ascii=False, separators=(",", ":"))
    out = ["{"]
    for k, v in meta.items():
        out.append("  %s: %s," % (json.dumps(k), compacto(v)))
    out.append('  "sprites": {')
    nomes = list(sprites)
    for i, n in enumerate(nomes):
        sp, fim = sprites[n], ("," if i < len(nomes) - 1 else "")
        if "px" in sp:
            out.append("    %s: %s%s" % (json.dumps(n), compacto(sp), fim))
            continue
        out.append('    %s: {"fps":%d,"pal":%s,"frames":[' % (json.dumps(n), sp["fps"], compacto(sp["pal"])))
        for j, q in enumerate(sp["frames"]):
            out.append("      " + compacto(q) + ("," if j < len(sp["frames"]) - 1 else ""))
        out.append("    ]}" + fim)
    out.append("  },")
    out.append('  "layers": [')
    for i, l in enumerate(layers):
        out.append("    " + compacto(l) + ("," if i < len(layers) - 1 else ""))
    out.append("  ]")
    out.append("}")
    txt = "\n".join(out) + "\n"
    with open(destino, "w", encoding="utf8", newline="\n") as f:
        f.write(txt)
    tam = len(txt.encode("utf8"))
    assert tam <= 24576, tam
    print("%s: %d camadas, %d sprites, %d B de sprites (crus), %d com motion, %d B" % (
        nome, len(layers), len(sprites), c, nm, tam))


if __name__ == "__main__":
    escrever("tokensmall-clawd.json", *pacote_clawd())
    escrever("tokensmall-codex.json", *pacote_codex())
