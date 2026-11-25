#!/usr/bin/env python3
"""Gera mais 3 pacotes do tokensmall em sim/apps-seeds/:

- tokensmall-clawd-mesa.json: o Clawd grande atras de uma mesa comprida, digitando, com monitor de codigo rolando e caneca
  soltando vapor (estacao "coding" do Clawd Quest, com o personagem em destaque). Le tk_c5, tk_c5r, tk_c7.
- tokensmall-clawd-rosto.json: close no rosto do Clawd: o corpo laranja ocupa a tela e os olhos grandes reagem (olham,
  piscam, viram X, ^ ^ ou dormem). A ideia de um rosto grande que reage vem do Clawdmeter; o desenho e proprio (olhos do
  Clawd Quest ampliados), nada do Clawdmeter foi copiado. Le tk_c5, tk_c5r, tk_c7.
- tokensmall-trio.json: Claude, Codex e Antigravity ao mesmo tempo, um cartao por assistente com mascote medio animado,
  percentual de 5 h, tempo ate zerar e barra. Le tk_c5, tk_c5r, tk_x5, tk_x5r, tk_a5, tk_a5r. Os campos tk_a* ainda nao
  existem no companheiro (dependem de APPDATA_FIELDS 18 -> 21): ate la o Antigravity aparece dormindo, com "--".

Arte: Clawd, bone, olhos e poses do Clawd Quest (MIT, sim/apps-seeds/LICENSE-clawd-quest.txt); nuvem do Codex e
Antigravity (o "A" com gradiente de arco-iris, olhos, bochechas e sorriso) do TokenTV (pixel/codex-pixel.png e
antigravity-pixel.png, MIT, sim/apps-seeds/LICENSE-token-tv.txt). O Antigravity foi redesenhado aqui numa grade de 18 x 16
a partir do antigravity-pixel.png (32 x 32 com gradiente suave, que nao cabe em 15 cores). Nada do Clawdmeter foi usado.

Uso: python3 sim/gerar_tokensmall_variacoes.py
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gerar_tokensmall_grande as G   # noqa: E402
import gerar_tokensmall_arcade as A   # noqa: E402

Grade, mov, S = G.Grade, G.mov, G.S
G.CORES["W"] = "#262624"  # fundo opaco dos bracos do rosto (parede do Clawd)


# ====================================================================== 1. Clawd na mesa
DT = 128  # y do tampo da mesa


def mesa_trab(q):  # so o que aparece acima da mesa (corpo e bracos), 11 linhas
    return G.clawd_trab(q)[:11]


def mesa_dorme(q):
    return G.clawd_dorme(q)[:11]


def mesa_erro(q):  # 4 quadros: as 4 poses, com a boca aberta em 2 delas
    return G.clawd_erro([0, 5, 2, 7][q])


def tela(q):  # monitor do Clawd Quest (o mesmo do arcade)
    return A.tela(q)


def tela_erro(q):
    """Tela vermelha com "!" piscando (no limite)."""
    g = Grade(20, 12, "S")
    g.r(1, 1, 18, 10, "R")
    if q == 0:
        g.r(9, 2, 2, 5, "w"); g.r(9, 8, 2, 2, "w")
    return g.linhas()


def vapor(q):
    g = Grade(5, 6)
    for (x, y) in [((1, 4), (2, 2), (1, 0)), ((2, 4), (1, 2), (2, 0)), ((1, 5), (2, 3), (3, 1))][q]:
        g.r(x, y, 1, 2, "u")
    return g.linhas()


CORES_TELA = dict(A.CORES["tela"], R="#C8553D", w="#F2EFE6")


def sprite_tela(fn, n, fps):
    frames = [fn(q) for q in range(n)]
    usados = sorted({c for f in frames for l in f for c in l if c != "."})
    return {"fps": fps, "pal": {c: CORES_TELA[c] for c in usados}, "frames": frames}


def pacote_mesa():
    v5, v5r, v7 = "x.tk_c5", "x.tk_c5r", "x.tk_c7"
    parede = "#262624"
    mx = 96                           # Clawd de x 96 a 192 (corpo de 108 a 180)
    by = DT - 9 * S                   # bracos (linhas 6..8) logo acima do tampo
    cx, cy = mx + 5 * S, by - 9 * S   # bone
    L = [
        # monitor na ponta esquerda da mesa (fica de lado para o Clawd) e o pe dele
        {"type": "rect", "x": 38, "y": DT - 6, "w": 12, "h": 6, "color": "#4A4A46"},
        {"type": "rect", "x": 16, "y": DT - 42, "w": 56, "h": 36, "r": 3, "color": "#5A5A55"},
        {"type": "sprite", "sprite": "tela", "x": 24, "y": DT - 36, "size": 2},
        {"type": "sprite", "sprite": "telaerro", "x": 24, "y": DT - 36, "size": 2, "when": v5 + "=100"},
        # trabalhando
        {"type": "sprite", "sprite": "trab", "size": S, "when": v5, "motion": G.balanco(mx, by)},
        {"type": "sprite", "sprite": "olhos", "size": S, "when": v5, "motion": G.balanco(mx + 7 * S, by + 3 * S)},
        {"type": "sprite", "sprite": "bone", "size": S, "when": v5, "motion": G.balanco(cx, cy)},
    ]
    L += G.suor(v5, mx + 33 * S, by - 4)
    L += [
        {"type": "rect", "x": mx - 18, "y": cy - 6, "w": 96 + 36, "h": DT - cy + 6, "color": parede, "when": v5 + "=100"},
        {"type": "sprite", "sprite": "erro", "size": S, "when": v5 + "=100", "motion": G.tremor(mx, by - 2 * S)},
        {"type": "sprite", "sprite": "bone", "size": S, "when": v5 + "=100", "motion": G.tremor(cx, cy)},
        {"type": "sprite", "sprite": "fumaca", "size": S, "when": v5 + "=100",
         "motion": mov(1500, (0, mx + 22 * S, cy + 6), (1500, mx + 26 * S, cy - 30))},
        {"type": "sprite", "sprite": "confete", "size": S, "when": v5 + "=0", "motion": mov(1000, (0, mx, 32), (1000, mx, 38))},
        {"type": "sprite", "sprite": "festa", "size": S, "when": v5 + "=0", "motion": G.pulo(mx, by - 3 * S)},
        {"type": "sprite", "sprite": "bone", "size": S, "when": v5 + "=0",
         "motion": mov(1000, (0, cx, cy), (125, cx, cy), (500, cx, cy - 48), (875, cx, cy), (1000, cx, cy))},
        # sem dados: cochila apoiado na mesa (mais baixo), respirando
        {"type": "sprite", "sprite": "dorme", "size": S, "when": "!" + v5r, "motion": G.respira(mx, by + 6)},
        {"type": "sprite", "sprite": "bone", "size": S, "when": "!" + v5r, "motion": G.respira(cx, cy + 6)},
        {"type": "sprite", "sprite": "zz", "size": S, "when": "!" + v5r,
         "motion": mov(2400, (0, mx + 27 * S, by), (2400, mx + 32 * S, by - 44))},
        # mesa na frente (cobre as pernas), teclado e caneca com vapor
        {"type": "rect", "x": 8, "y": DT, "w": 224, "h": 6, "color": "#8A6248"},
        {"type": "rect", "x": 14, "y": DT + 6, "w": 212, "h": 20, "color": "#6B4A35"},
        {"type": "rect", "x": 14, "y": DT + 14, "w": 212, "h": 2, "color": "#4E3526"},
        {"type": "rect", "x": 112, "y": DT - 3, "w": 64, "h": 3, "color": "#3a3936"},
        {"type": "rect", "x": 200, "y": DT - 14, "w": 12, "h": 14, "r": 2, "color": "#E8EEF8"},
        {"type": "rect", "x": 212, "y": DT - 11, "w": 4, "h": 7, "color": "#E8EEF8"},
        {"type": "sprite", "sprite": "vapor", "x": 201, "y": DT - 28, "size": 2},
    ]
    L += G.textos(v5, v5r, v7, "#D97757", parede, y0=168)
    sprites = {
        "trab": G.sprite(mesa_trab, 8, 8),
        "olhos": G.sprite(G.clawd_olhos, 16, 4),
        "bone": G.sprite(G.bone, 1),
        "erro": G.sprite(mesa_erro, 4, 6),
        "festa": G.sprite(G.clawd_festa, 8, 8),
        "dorme": G.sprite(mesa_dorme, 1),
        "zz": G.sprite(G.zz, 1),
        "fumaca": G.sprite(G.fumaca, 3, 5),
        "confete": G.sprite(G.confete, 4, 6),
        "tela": sprite_tela(tela, 4, 2),
        "telaerro": sprite_tela(tela_erro, 2, 2),
        "vapor": G.sprite(vapor, 3, 3),
    }
    meta = {
        "format": 2, "author": "davicorrea0", "id": "tokensmall-clawd-mesa",
        "name": "Uso de IA: Clawd na mesa", "desc": "Clawd digitando na mesa, reage ao limite de 5 h (arte MIT: Clawd Quest)",
        "name_en": "AI usage: Clawd at the desk", "desc_en": "Clawd typing at his desk, reacting to the 5 h limit (MIT art: Clawd Quest)",
        "name_es": "Uso de IA: Clawd en la mesa", "desc_es": "Clawd tecleando en la mesa, reacciona al limite de 5 h (arte MIT)",
        "bg": parede,
    }
    return meta, sprites, L


# ====================================================================== 2. rosto grande do Clawd
# olhos: sprite opaco 24 x 8 (fundo = rosto); cada olho 3 x 6 celulas (os 2 x 3 do Clawd Quest
# ampliados na mesma proporcao); size 6 = 144 x 48 px
OE, OD, OY = 4, 17, 1


def olho_par(g, tipo, dx=0, dy=0, cor="k"):
    for ex in (OE + dx, OD + dx):
        if tipo == "aberto":
            g.r(ex, OY + dy, 3, 6, cor)
        elif tipo == "atento":     # olhos um pouco apertados (concentrado digitando)
            g.r(ex, OY + 1 + dy, 3, 4, cor)
        elif tipo == "fechado":    # piscada
            g.r(ex - 1, OY + 3 + dy, 5, 1, cor)
        elif tipo == "dorme":      # fechado curvado para baixo
            g.r(ex - 1, OY + 3, 5, 1, cor); g.p(ex - 2, OY + 2, cor); g.p(ex + 4, OY + 2, cor)
        elif tipo == "feliz":      # ^
            g.p(ex - 1, OY + 4 + dy, cor); g.p(ex, OY + 3 + dy, cor); g.p(ex + 1, OY + 2 + dy, cor)
            g.p(ex + 2, OY + 3 + dy, cor); g.p(ex + 3, OY + 4 + dy, cor)
            g.r(ex - 1, OY + 6, 5, 1, "p")  # bochecha
        elif tipo == "xis":
            for d in range(5):
                g.p(ex - 1 + d, OY + d + dy, cor); g.p(ex + 3 - d, OY + d + dy, cor)


def rosto_trab(q):
    """16 quadros a 4 fps (4 s): olha a tela, aperta os olhos concentrado, pisca, olha para os lados, pisca duas vezes."""
    g = Grade(24, 8, "o")
    m = "ccccaaBcRRccLLcB"[q]
    if m == "B":
        olho_par(g, "fechado")
    elif m == "a":
        olho_par(g, "atento")
    else:
        olho_par(g, "aberto", {"c": 0, "R": 2, "L": -2}[m])
    return g.linhas()


def rosto_erro(q):
    """26 x 10: uma celula de folga em volta, para cobrir os olhos de trabalho mesmo tremendo."""
    g = Grade(24, 8, "R")
    olho_par(g, "xis", 0, 1 if q % 2 else 0)
    return ["RR" * 13] + ["R" + l + "R" for l in g.linhas()] + ["RR" * 13]


def rosto_festa(q):
    g = Grade(24, 8, "o")
    olho_par(g, "feliz", 0, [0, -1, 0, 1][q])
    return g.linhas()


def rosto_dorme(q):
    g = Grade(24, 8, "o")
    olho_par(g, "dorme")
    return g.linhas()


def braco(lado, cor):
    """Braco 4 x 3 numa caixa opaca 4 x 6 (fundo = parede): digitando (laranja) ou se debatendo (vermelho, no limite);
    lado 0/1 = meio ciclo de defasagem entre os dois."""
    def fn(q):
        g = Grade(4, 6, "W")
        y = (1, 2, 1, 0)[(q + 2 * lado) % 4] if cor == "o" else (0, 3, 1, 2)[(q + lado) % 4]
        g.r(0, y, 4, 3, cor)
        return g.linhas()
    fn.__name__ = "braco%d%s" % (lado, cor)
    return fn


def pacote_rosto():
    v5, v5r, v7 = "x.tk_c5", "x.tk_c5r", "x.tk_c7"
    parede = "#262624"
    fx, fy, fw, fh = 24, 40, 192, 110     # rosto (corpo visto de perto)
    ox, oy = 48, 72                       # olhos (144 x 48)
    L = [
        {"type": "rect", "x": fx, "y": fy, "w": fw, "h": fh, "color": "#D97757"},
        {"type": "rect", "x": fx, "y": fy, "w": fw, "h": fh, "color": "#C8553D", "when": v5 + "=100"},
        # aba do bone na testa (o resto do bone fica fora do quadro, de tao perto)
        {"type": "rect", "x": fx, "y": fy, "w": fw, "h": 10, "color": "#3B6FC8"},
        {"type": "rect", "x": fx, "y": fy + 10, "w": fw, "h": 3, "color": "#24407A"},
        # bracos dos lados: parados embaixo de tudo; digitando quando ha leitura; se debatendo no limite
        {"type": "rect", "x": 4, "y": fy + 66, "w": 20, "h": 15, "color": "#D97757"},
        {"type": "rect", "x": 216, "y": fy + 66, "w": 20, "h": 15, "color": "#D97757"},
        {"type": "sprite", "sprite": "be", "x": 4, "y": fy + 56, "size": 5, "when": v5},
        {"type": "sprite", "sprite": "bd", "x": 216, "y": fy + 56, "size": 5, "when": v5},
        {"type": "sprite", "sprite": "bre", "x": 4, "y": fy + 56, "size": 5, "when": v5 + "=100"},
        {"type": "sprite", "sprite": "brd", "x": 216, "y": fy + 56, "size": 5, "when": v5 + "=100"},
        # olhos (sprites opacos: o de cima cobre o de baixo)
        {"type": "sprite", "sprite": "otrab", "size": 6, "when": v5, "motion": G.balanco(ox, oy, 2, 1200)},
        {"type": "sprite", "sprite": "oerro", "size": 6, "when": v5 + "=100", "motion": G.tremor(ox - 6, oy - 6)},
        {"type": "sprite", "sprite": "ofesta", "x": ox, "y": oy, "size": 6, "when": v5 + "=0"},
        {"type": "sprite", "sprite": "odorme", "size": 6, "when": "!" + v5r, "motion": G.respira(ox, oy + 4)},
        {"type": "sprite", "sprite": "zz", "size": S, "when": "!" + v5r,
         "motion": mov(2400, (0, 186, fy + 46), (2400, 200, fy + 16))},
        {"type": "sprite", "sprite": "confete", "size": S, "when": v5 + "=0", "motion": mov(1000, (0, 72, fy + 16), (1000, 72, fy + 22))},
        {"type": "sprite", "sprite": "fumaca", "size": S, "when": v5 + "=100",
         "motion": mov(1500, (0, 170, fy + 20), (1500, 186, fy - 10))},
    ]
    L += G.suor(v5, 190, fy + 34)
    L += G.suor(v5, 44, fy + 40)
    L += G.textos(v5, v5r, v7, "#D97757", parede, y0=160)
    sprites = {
        "otrab": G.sprite(rosto_trab, 16, 4),
        "oerro": G.sprite(rosto_erro, 2, 8),
        "ofesta": G.sprite(rosto_festa, 4, 6),
        "odorme": G.sprite(rosto_dorme, 1),
        "be": G.sprite(braco(0, "o"), 4, 8),
        "bd": G.sprite(braco(1, "o"), 4, 8),
        "bre": G.sprite(braco(0, "R"), 4, 8),
        "brd": G.sprite(braco(1, "R"), 4, 8),
        "zz": G.sprite(G.zz, 1),
        "confete": G.sprite(G.confete, 4, 6),
        "fumaca": G.sprite(G.fumaca, 3, 5),
    }
    meta = {
        "format": 2, "author": "davicorrea0", "id": "tokensmall-clawd-rosto",
        "name": "Uso de IA: rosto do Clawd", "desc": "Rosto grande do Clawd que reage ao limite de 5 h (arte MIT: Clawd Quest)",
        "name_en": "AI usage: Clawd face", "desc_en": "Big Clawd face reacting to the 5 h limit (MIT art: Clawd Quest)",
        "name_es": "Uso de IA: cara de Clawd", "desc_es": "Cara grande de Clawd que reacciona al limite de 5 h (arte MIT)",
        "bg": parede,
    }
    return meta, sprites, L


# ====================================================================== 3. trio: Claude, Codex e Antigravity
# Antigravity: o "A" do antigravity-pixel.png (TokenTV) redesenhado em 18 x 16: cabeca nas linhas 0..5, pernas abrindo
# ate a linha 15; o gradiente vai do verde (perna esquerda) ao laranja e vermelho (topo) e ao azul (perna direita).
AW, AH = 18, 16
GRAD = ["C", "T", "G", "Y", "O", "O", "R", "M", "P", "B"]  # da ponta da perna esquerda ate a da direita


def anti_forma():
    cel = {}
    cab = [(7, 10), (6, 11), (5, 12), (5, 12), (4, 13), (4, 13)]
    for y, (a, b) in enumerate(cab):
        for x in range(a, b + 1):
            cel[(x, y)] = None
    for y in range(6, AH):
        lx = 4 - (y - 6) * 4 // 9
        for x in range(lx, lx + 3):
            cel[(x, y)] = None
            cel[(AW - 1 - x, y)] = None
    # cor: posicao ao longo do "A" (esquerda embaixo = 0, topo = meio, direita embaixo = fim)
    for (x, y) in cel:
        lado = (x + 0.5) / AW  # 0..1 da esquerda para a direita
        alt = 1 - y / (AH - 1)  # 1 no topo
        t = lado * 0.6 + (0.5 + (lado - 0.5) * 0.4) * 0.4 if y < 6 else (alt * 0.45 if lado < 0.5 else 1 - alt * 0.45)
        cel[(x, y)] = GRAD[min(len(GRAD) - 1, int(t * len(GRAD)))]
    return cel


def anti(tipo, piscar=False, perna=0, troca=None):
    g = Grade(AW, AH, "W")
    for (x, y), c in anti_forma().items():
        if perna and y >= 13:  # pernas balancando (gravidade zero)
            x += perna if x < AW // 2 else -perna
        g.p(x, y, troca.get(c, c) if troca else c)
    if tipo == "xis":
        for ex in (6, 10):
            g.p(ex, 2, "k"); g.p(ex + 2, 2, "k"); g.p(ex + 1, 3, "k"); g.p(ex, 4, "k"); g.p(ex + 2, 4, "k")
    elif tipo == "dorme" or piscar:
        g.r(6, 3, 2, 1, "k"); g.r(10, 3, 2, 1, "k")
        if tipo == "dorme":
            g.r(8, 5, 2, 1, "k")
    else:
        for ex in (6, 10):
            g.r(ex, 2, 2, 2, "k"); g.p(ex, 2, "w")
        g.p(5, 4, "p"); g.p(12, 4, "p")       # bochechas
        g.r(8, 4, 2, 1, "k"); g.p(7, 3, "O")  # sorriso
    return g.linhas()


ANTI_VERM = {"C": "h", "T": "h", "G": "g", "Y": "f", "O": "e", "R": "f", "M": "g", "P": "h", "B": "i"}
CORES_ANTI = {"W": "#1d2230", "C": "#73B2E2", "T": "#6FB08A", "G": "#8CC062", "Y": "#C9B455", "O": "#E8964A",
              "R": "#E06A48", "M": "#B45E78", "P": "#8F6FC3", "B": "#5486EA", "k": "#0E101E", "w": "#F5F5F5",
              "p": "#FF7896", "e": "#ffd6cf", "f": "#f4a493", "g": "#e8806b", "h": "#d4634f", "i": "#b84a3a"}


def sprite_anti(fn, n, fps=None):
    frames = [fn(q) for q in range(n)]
    usados = sorted({c for f in frames for l in f for c in l if c != "."})
    assert len(usados) <= 15, usados
    pal = {c: CORES_ANTI[c] for c in usados}
    return {"pal": pal, "px": frames[0]} if n == 1 else {"fps": fps, "pal": pal, "frames": frames}


def anti_trab(q):  # 4 quadros a 4 fps: pernas balancando e uma piscada
    return anti("normal", piscar=q == 3, perna=(0, 1, 1, 0)[q])


def anti_esgo(q):
    return anti("xis", troca=ANTI_VERM)


def anti_dorm(q):
    return anti("dorme")


def cartao(t, pre, nome, parede, cor, barra, spr, caixa, mov_trab, alto):
    """Um cartao de 228 x 74 em y = t: mascote opaco a esquerda (trabalhando, dormindo e no limite um sobre o outro)."""
    v5, v5r = "x.tk_%s5" % pre, "x.tk_%s5r" % pre
    bx, by = caixa
    L = [
        {"type": "rect", "x": 6, "y": t, "w": 228, "h": 74, "r": 10, "color": parede},
        {"type": "sprite", "sprite": spr + "t", "size": alto, "motion": mov_trab(bx, by)},
        {"type": "sprite", "sprite": spr + "d", "size": alto, "when": "!" + v5, "motion": G.respira(bx, by)},
        {"type": "sprite", "sprite": spr + "e", "x": bx, "y": by, "size": alto, "when": v5 + "=100"},
        {"type": "sprite", "sprite": "zz", "size": 2, "when": "!" + v5, "motion": mov(2400, (0, 62, t + 40), (2400, 68, t + 14))},
    ]
    for dx, dy, h in ((0, 4, 4), (1, 0, 9), (2, 4, 4)):  # gota de suor a 90 % ou mais
        L.append({"type": "bar", "x": 70 + dx, "y": t + 20 + dy, "w": 1, "h": h, "value": v5, "max": 90,
                  "color": "#8fdcff", "when": "!" + v5 + "=100"})
    L += [
        {"type": "text", "x": 84, "y": t + 8, "font": "pixel", "size": 2, "color": cor, "text": nome},
        {"type": "text", "x": 146, "y": t + 26, "font": "bebas", "size": 36, "color": "#F2EFE6", "text": "{%s}" % v5,
         "align": "right", "bg": parede},
        {"type": "text", "x": 148, "y": t + 44, "font": "pixel", "size": 2, "color": "#F2EFE6", "text": "%", "when": v5},
        {"type": "text", "x": 226, "y": t + 26, "font": "pixel", "size": 2, "color": "#8A8A82",
         "text": {"pt": "ZERA", "en": "RESET", "es": "VUELVE"}, "align": "right"},
        {"type": "text", "x": 226, "y": t + 44, "font": "pixel", "size": 2, "color": "#F2EFE6", "text": "{%s}" % v5r, "align": "right"},
        {"type": "bar", "x": 84, "y": t + 64, "w": 142, "h": 4, "r": 2, "value": v5, "max": 100, "color": barra, "track": "#3a3936"},
    ]
    return L


def pacote_trio():
    bob = lambda x, y: G.balanco(x, y, 2, 600)
    flutua = lambda x, y: mov(3000, (0, x, y), (750, x + 2, y - 3), (1500, x, y - 5), (2250, x - 2, y - 3), (3000, x, y))
    L = []
    L += cartao(6, "c", "CLAUDE", "#262624", "#D97757", "#D97757", "c", (12, 6 + 12), bob, 2)
    L += cartao(83, "x", "CODEX", "#24232e", "#bcb9ff", "#979cff", "x", (12, 83 + 12), bob, 2)
    L += cartao(160, "a", "ANTIGRAVITY", "#1d2230", "#73B2E2", "#5486EA", "a", (17, 160 + 18), flutua, 3)
    sprites = {
        "ct": A.sprite("claude", A.claude_trab, 4, 8),
        "cd": A.sprite("claude", A.claude_dorm, 1),
        "ce": A.sprite("claude", A.claude_esgo, 1),
        "xt": A.sprite("codex", A.codex_trab, 4, 8),
        "xd": A.sprite("codex", A.codex_dorm, 1),
        "xe": A.sprite("codex", A.codex_esgo, 1),
        "at": sprite_anti(anti_trab, 4, 4),
        "ad": sprite_anti(anti_dorm, 1),
        "ae": sprite_anti(anti_esgo, 1),
        "zz": G.sprite(G.zz, 1),
    }
    meta = {
        "format": 2, "author": "davicorrea0", "id": "tokensmall-trio",
        "name": "Uso de IA: trio", "desc": "Claude, Codex e Antigravity com mascotes e limite de 5 h (arte MIT: Clawd Quest, TokenTV)",
        "name_en": "AI usage: trio", "desc_en": "Claude, Codex and Antigravity mascots with the 5 h limit (MIT art)",
        "name_es": "Uso de IA: trio", "desc_es": "Claude, Codex y Antigravity con mascotas y limite de 5 h (arte MIT)",
        "bg": "#17141f",
    }
    return meta, sprites, L


if __name__ == "__main__":
    G.escrever("tokensmall-clawd-mesa.json", *pacote_mesa())
    G.escrever("tokensmall-clawd-rosto.json", *pacote_rosto())
    G.escrever("tokensmall-trio.json", *pacote_trio())
