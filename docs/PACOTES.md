# Pacotes de mostrador (instalar sem atualizar o firmware)

Um **pacote** é um arquivo `.json` pequeno (poucos KB) que descreve um mostrador em camadas. Ele é instalado pelo painel do relógio (*Relógio → Mais mostradores*) e fica guardado na memória de arquivos, junto com as fotos. Não é preciso regravar o firmware.

Exemplos prontos: [`faces/`](../faces) (`bebas-gigante.json`, `cafe-pixel.json`, `radar-analogico.json`).

```json
{
  "format": 1,
  "id": "meu-relogio",
  "name": "Meu Relógio",
  "author": "seu-nome",
  "desc": "Uma frase sobre o mostrador",
  "bg": "#000000",
  "sprites": { },
  "layers": [
    { "type": "text", "x": 120, "y": 60, "font": "bebas", "size": 96, "align": "center", "color": "accent", "text": "{HH}:{MM}" },
    { "type": "text", "x": 120, "y": 180, "font": "pixel", "size": 2, "align": "center", "color": "muted", "text": "{WD} {DD} {MON}" }
  ]
}
```

| Campo | |
|---|---|
| `format` | `1`, ou `2` para usar movimento, texto por cima do fundo, mais de 8 quadros ou mais de 6 KB de sprites (ver [Formato 2](#formato-2-firmware-1260)) |
| `id` | nome do arquivo no relógio (minúsculas, números, `-` e `_`, até 23). Reinstalar com o mesmo `id` substitui |
| `name`, `author`, `desc` | aparecem na galeria |
| `bg` | cor de fundo |
| `sprites` | desenhos em pixel art usados pelas camadas `sprite` |
| `layers` | camadas, desenhadas na ordem (a primeira fica embaixo) |

A tela tem **240 × 240** pixels; `x` cresce para a direita e `y` para baixo.

## Cores

`"#rrggbb"` ou um nome da paleta: `accent` (a cor escolhida no painel), `text`, `muted`, `dim`, `card`, `card2`, `bg`, `work` (vermelho-tomate), `short` (verde), `long` (azul), `sun`, `cloud`, `rain`, ou `none` (não desenha).

## Variáveis de texto

| | | | |
|---|---|---|---|
| `{HH}` hora (2 dígitos, respeita 12/24 h) | `{H}` hora sem zero | `{MM}` minutos | `{SS}` segundos |
| `{WD}` dia da semana (`TER`) | `{DD}` dia | `{MON}` mês (`OUT`) | `{MO}` mês (`10`) |
| `{YYYY}` ano | `{AMPM}` | `{TEMP}` temperatura | `{TMIN}` `{TMAX}` |
| `{HUM}` umidade | `{WX}` descrição do clima | `{CITY}` cidade | `{IP}` |
| `{FOCOS}` focos de hoje | `{META}` meta diária | `{STREAK}` dias seguidos na meta | `{BEST}` recorde de dias seguidos |
| `{UNIT}` `C` ou `F` (1.19.0+) | `{RAIN}` `chuva 15 min` / `chuva logo`, vazio sem chuva na próxima hora (1.19.0+) | | |

`{TEMP}`, `{TMIN}` e `{TMAX}` já vêm na unidade escolhida no painel (°C ou °F).

`°` pode ser escrito direto no texto (`"{TEMP}°"`).

**Texto em 3 idiomas** (firmware 1.10.0+): em vez de uma string, `text` aceita um objeto com uma versão por idioma, e o relógio mostra a do idioma escolhido no painel. `en` e `es` são opcionais (sem eles, vale o `pt`):

```json
{ "type": "text", "x": 16, "y": 140, "text": { "pt": "FOCOS", "en": "FOCUS", "es": "FOCOS" } }
```

## Camadas

Toda camada aceita `"when"` para aparecer só em certas condições: `day`, `night`, `rain`, `dry`, `weather` (clima disponível), `noweather`, `goal` (meta do dia batida), `nogoal`, `streak` (sequência de pelo menos 1 dia).

| `type` | Campos |
|---|---|
| `rect` | `x`, `y`, `w`, `h`, `r` (cantos), `color` (preenchimento), `stroke` (contorno) |
| `circle` | `x`, `y` (centro), `r`, `color`, `stroke` |
| `line` | `x`, `y`, `x2`, `y2`, `size` (espessura), `color` |
| `text` | `x`, `y`, `text`, `font`, `size`, `align` (`left`/`center`/`right`; `x` é o ponto de alinhamento), `color`, e conforme a fonte `off` ou `bg`; opcionais `glow`, `pulse`, `spread`, `base` (veja "Brilho pulsante") |
| `sprite` | `x`, `y`, `sprite` (nome em `sprites`), `size` (ampliação inteira) |
| `mascot` | o tomate do projeto: `x`, `y`, `size`, `mood`: `auto` (dorme à noite, guarda-chuva na chuva, comemora a meta, cachecol no frio, óculos no calor), `feliz`, `dormindo`, `focado`, `relax`, `comemorando`, `guarda-chuva`, `cachecol`, `oculos` |
| `plant` | a planta do tomate, que cresce com a sequência (semente, broto, flor, tomate verde, maduro, dourado com 7+ dias): `x`, `y`, `size` (ampliação do desenho de 16×16) (requer firmware 1.8.0+) |
| `weather` | ícone do clima: `x`, `y` (centro), `size` (10 = ~40 px) |
| `bar` | `x`, `y`, `w`, `h`, `r`, `value` (`sec`, `min`, `hour`, `focos`, `hum`), `max` (opcional), `color`, `track` |
| `hand` | ponteiro: `x`, `y` (centro), `value` (`hour`, `min`, `sec`), `len`, `tail`, `size` (espessura), `color` |
| `image` | JPEG em `/faces/` no relógio: `x`, `y`, `w`, `h`, `file` |
| `arc` | anel de progresso com pontas redondas, do topo no sentido horário: `x`, `y` (centro), `r` (raio externo), `size` (espessura), `value`, `max` (opcional), `color`, `track` (requer firmware 1.6.6+) |
| `ticks` | marcações de mostrador analógico: `x`, `y` (centro), `r` (raio externo), `len`, `count` (12 = horas, 60 = minutos), `size`, `color` (requer firmware 1.6.6+) |

### Fontes (`text`)

| `font` | `size` | Observações |
|---|---|---|
| `pixel` | escala da fonte 6×8 (1, 2, 3…) | qualquer texto (acentos viram a letra sem acento) |
| `dm` | lado de cada "pixel" do dígito (5×7) | dígitos, `:`, `-`, `.`; `off` pinta os pixels apagados |
| `geo` | altura em px | dígitos e `:`; `w` muda a espessura do traço |
| `dseg` | altura em px | 7 segmentos de LCD; `off` mostra os segmentos apagados |
| `bebas` | altura em px | dígitos e `:` suavizados; `bg` é a cor de fundo da suavização (padrão: `bg` do pacote) |
| `outfit` | altura em px | geométrica limpa, estilo widget (Outfit SemiBold); igual a `bebas` no resto (requer firmware 1.6.5+) |
| `righteous` | altura em px | geométrica arredondada, estilo Bauhaus/art déco (Righteous); igual a `bebas` (requer firmware 1.6.5+) |

## Sprites

```json
"sprites": {
  "xicara": {
    "pal": { "o": "#3a2a20", "w": "#f5f5f5", "c": "#6f4e37" },
    "px": [
      ".oooooooo.",
      ".occcccco.",
      ".owwwwwwoo",
      "..oooooo.."
    ]
  }
}
```

Cada caractere de `px` é um pixel; `.` é transparente e os demais vêm de `pal` (até 15 cores, um caractere cada). Até 32×32 e até 12 sprites por pacote.

### Sprites animados

Em vez de `px`, use `frames`: uma lista de quadros, cada um no mesmo formato de `px` e com a mesma `pal`. `fps` (1 a 10, padrão 4) é a velocidade.

```json
"sprites": {
  "pisca": {
    "fps": 2,
    "pal": { "o": "#3a2a20", "y": "#ffd166" },
    "frames": [
      ["..oooo..", ".oyyyyo.", ".oyooyo.", ".oyyyyo.", "..oooo.."],
      ["..oooo..", ".oyyyyo.", ".oyyyyo.", ".oyyyyo.", "..oooo.."]
    ]
  }
}
```

O quadro vem do relógio do aparelho, então a animação anda sozinha e só a área do sprite é redesenhada. Um sprite com `px` continua parado. Limites: todos os quadros com o mesmo tamanho (até 32×32), no máximo **8 quadros** por sprite e **6 KB** de sprites no pacote (no [formato 2](#formato-2-firmware-1260): **16 quadros** e **8 KB**) (cada quadro de 32×32 ocupa 512 bytes; um de 16×16, 128). Veja um exemplo completo em [`faces/tomate-pulando.json`](../faces/tomate-pulando.json); outros animados: [`fogueira`](../faces/fogueira.json) (chama de 6 quadros), [`inverno`](../faces/inverno.json) (um sprite de neve repetido em duas escalas), [`gato-soneca`](../faces/gato-soneca.json), [`pausa-cafe`](../faces/pausa-cafe.json), [`aquario`](../faces/aquario.json) e [`janela`](../faces/janela.json) (chuva).

### Brilho pulsante

Uma camada `text` pode ter um halo que respira atrás das letras, como um tubo nixie (requer firmware 1.22.0+; antes disso o campo é ignorado e o pacote instala sem brilho):

```json
{ "type": "text", "x": 66, "y": 78, "font": "geo", "size": 58, "w": 8, "color": "#4a2d1b", "text": "{HH}", "align": "center",
  "glow": "#ff8a2a", "base": "#1c1e1a", "spread": 3, "pulse": 2400 }
```

- `glow`: cor do halo (ativa o recurso);
- `spread`: alcance de 1 a 3 px (padrão 2);
- `pulse`: período em ms, de 400 a 10000 (sem ele, o brilho fica fixo);
- `base`: cor da superfície sob o halo (padrão: `bg` do pacote). O halo é uma mistura de `base` com `glow`, então use a cor real do que está atrás do texto.

A intensidade varia em 8 níveis e só a área do texto é redesenhada a cada mudança. Em `geo` o halo é o próprio traço mais grosso (barato); nas outras fontes custa 8 desenhos por pixel de alcance, então prefira textos pequenos. Exemplo: [`faces/nixie.json`](../faces/nixie.json).

## Formato 2 (firmware 1.26.0)

Com `"format": 2` o pacote pode usar os recursos abaixo. O firmware 1.26.0 aceita os formatos 1 e 2; um pacote no formato 1 que use qualquer um deles é recusado com "use \"format\": 2". Versões anteriores recusam o formato 2 ("formato desconhecido"), por isso a galeria marca esses mostradores como "requer v1.26.0".

- sprites com até **16 quadros** e até **8 KB** de sprites no pacote (a conta é a mesma: 4 + 30 + quadros × (largura × altura + 1) / 2 por sprite); `fps` continua de 1 a 10, porque o relógio desenha 10 vezes por segundo;
- arquivo de até **24 KB**;
- `motion` (sprite que anda) e `overlay` (hora com borda suave por cima do fundo e dos sprites), descritos a seguir.

### Sprite que anda (`motion`)

Uma camada `sprite` pode seguir um caminho em volta. As posições são absolutas (o `x`/`y` da camada não é usado) e o sprite vai em linha reta de um ponto ao seguinte:

```json
{ "type": "sprite", "sprite": "nave", "size": 5,
  "motion": { "period": 12000, "keys": [
    { "t": 0, "x": -75, "y": 70 }, { "t": 6000, "x": 120, "y": 60 }, { "t": 12000, "x": -75, "y": 70 } ] } }
```

- `period` de 100 a 60000 ms; de 2 a 6 pontos em `keys`;
- `t` começa em 0, sempre cresce e o último ponto tem `t` = `period` (para a volta não pular, repita nele a posição do primeiro);
- `x` e `y` de −480 a 480 (fora da tela vale, para o sprite entrar e sair);
- no máximo **12 camadas** com `motion` por pacote.

Exemplos: [`cena-espacial`](../faces/cena-espacial.json), [`odisseia-estelar`](../faces/odisseia-estelar.json) e [`globo`](../faces/globo.json) (a Terra de 16 quadros girando, com a lua em `motion`).

### Hora por cima do fundo (`overlay`)

Em texto com fonte `bebas`, `outfit` ou `righteous`, `"overlay": true` desenha os dígitos sem fundo, com a borda misturada às cores que estão atrás (fundo, formas e sprites). Sem isso, cada dígito leva um retângulo da cor `bg` (ou `color2`). Nas outras fontes `overlay` é recusado.

### Memória e desempenho

Com `overlay` o relógio desenha a tela inteira em faixas fora da tela, como nas áreas que mudam. O tamanho do arquivo não diz quanta memória a carga usa: confira no relógio (Ajustes → Sistema, `pack` e maior bloco livre) antes de publicar. Um pacote com `"type": "world3d"` (cena 3D, só existiu nas primeiras versões de teste da 1.26.0) é recusado com "tipo desconhecido".

## Limites

- arquivo de até **16 KB** (formato 2: **24 KB**), até **48 camadas**, cada texto com até **47 bytes** e até **1,5 KB** de textos no pacote;
- o relógio redesenha só o que muda: camadas com `{SS}` ou ponteiro de segundos são redesenhadas a cada segundo — prefira áreas pequenas para elas.

## Testar e publicar

1. Coloque o `.json` em [`faces/`](../faces) e rode `sim/build.sh`: o simulador desenha o pacote, confere o redesenho incremental (`pack-<id>  ok`), grava a prévia `faces/<id>.png` e atualiza `faces/catalog.json`.
2. Para entrar na galeria online, abra um pull request com o `.json`, a prévia e o catálogo atualizado. Mostradores `quartinho` e `globo` são bons pontos de partida: o primeiro muda sozinho com dia, noite e chuva (`when`), o segundo usa os 16 quadros e `motion`.
