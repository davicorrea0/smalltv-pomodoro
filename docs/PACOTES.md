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
| `format` | sempre `1` |
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
| `{FOCOS}` focos de hoje | `{META}` meta diária | | |

`°` pode ser escrito direto no texto (`"{TEMP}°"`).

## Camadas

Toda camada aceita `"when"` para aparecer só em certas condições: `day`, `night`, `rain`, `dry`, `weather` (clima disponível), `noweather`, `goal` (meta do dia batida), `nogoal`.

| `type` | Campos |
|---|---|
| `rect` | `x`, `y`, `w`, `h`, `r` (cantos), `color` (preenchimento), `stroke` (contorno) |
| `circle` | `x`, `y` (centro), `r`, `color`, `stroke` |
| `line` | `x`, `y`, `x2`, `y2`, `size` (espessura), `color` |
| `text` | `x`, `y`, `text`, `font`, `size`, `align` (`left`/`center`/`right`; `x` é o ponto de alinhamento), `color`, e conforme a fonte `off` ou `bg` |
| `sprite` | `x`, `y`, `sprite` (nome em `sprites`), `size` (ampliação inteira) |
| `mascot` | o tomate do projeto: `x`, `y`, `size`, `mood`: `auto` (dorme à noite, guarda-chuva na chuva, comemora a meta, cachecol no frio, óculos no calor), `feliz`, `dormindo`, `focado`, `relax`, `comemorando`, `guarda-chuva`, `cachecol`, `oculos` |
| `weather` | ícone do clima: `x`, `y` (centro), `size` (10 = ~40 px) |
| `bar` | `x`, `y`, `w`, `h`, `r`, `value` (`sec`, `min`, `hour`, `focos`, `hum`), `max` (opcional), `color`, `track` |
| `hand` | ponteiro: `x`, `y` (centro), `value` (`hour`, `min`, `sec`), `len`, `tail`, `size` (espessura), `color` |
| `image` | JPEG em `/faces/` no relógio: `x`, `y`, `w`, `h`, `file` |

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

## Limites

- arquivo de até **16 KB**, até **48 camadas**;
- o relógio redesenha só o que muda: camadas com `{SS}` ou ponteiro de segundos são redesenhadas a cada segundo — prefira áreas pequenas para elas.

## Testar e publicar

1. Coloque o `.json` em [`faces/`](../faces) e rode `sim/build.sh`: o simulador desenha o pacote, confere o redesenho incremental (`pack-<id>  ok`), grava a prévia `faces/<id>.png` e atualiza `faces/catalog.json`.
2. Para entrar na galeria online, abra um pull request com o `.json`, a prévia e o catálogo atualizado.
