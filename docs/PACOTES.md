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

## Diretrizes de design

Vale para qualquer mostrador, em qualquer formato. Os números de limite (camadas, sprites, bytes) ficam só em [Limites](#limites).

| Assunto | Diretriz |
|---|---|
| Tela | IPS de 240 × 240, pequena, vista de uma mesa ou estante. Projete para 1 a 2 m, não para um celular. |
| Área segura | Deixe o que precisa ser lido a pelo menos **12 px** das bordas: a moldura e os cantos da caixa comem a margem. Fundo e enfeites podem ir até a borda. |
| Tamanhos mínimos | Hora com **40 px de altura ou mais** (`bebas`, `outfit`, `righteous`, `geo`, `dseg` ≥ 40; `pixel` tamanho ≥ 5; `dm` tamanho ≥ 6). Texto secundário em `pixel` tamanho 2 ou mais (12 × 16 px). `pixel` tamanho 1 só para enfeite. |
| Contraste | Texto contra o fundo em cerca de 4,5:1 ou melhor. A hora deve ser a primeira coisa que se vê, a 2 m. |
| Escuro primeiro | Comece por um `bg` escuro: poupa a vista à noite e fica melhor no IPS. Mostrador claro vale, mas confira à noite. |
| Cores (RGB565) | A tela tem 5 bits de vermelho, 6 de verde e 5 de azul. `#rrggbb` é arredondado: vermelho e azul andam de uns 8 em 8, verde de 4 em 4. Degradê liso vira faixas e dois quase-pretos (`#101010`, `#141414`) podem virar a mesma cor. Deixe um degrau claro entre cores vizinhas e confira a prévia. |
| À noite | O relógio pode tingir a tela de âmbar: **o vermelho fica e o azul perde até 45 %** (azul e brancos frios viram âmbar, cinzas esquentam). Texto azul puro perde brilho e pode quase sumir; prefira acentos quentes ou a cor `accent`. Teste com o tingimento ligado. |
| Brilho baixo | À noite a luz de fundo fica bem baixa. Cinzas médios-escuros (`dim`, `card`) somem; deixe a hora bem mais clara que o fundo. |
| Verde LCD | O verde de LCD antigo é um estilo dos mostradores retrô embutidos, aplicado só neles. Não conte com ele num pacote; se quiser esse visual, escolha as cores no próprio `.json`. |
| Animação calma | No máximo **10 fps** (o relógio desenha 10 vezes por segundo). Movimento pequeno e lento. **Nunca piscar nem fazer strobe.** Camadas com `{SS}` ou ponteiro de segundos são redesenhadas a cada segundo: mantenha-as pequenas. |
| Timer do foco | O timer do Pomodoro é o coração do projeto. Não desenhe por cima dele; deixe o meio livre durante o foco ou esconda camadas cheias com `"when"`. |
| Identidade própria | Faça do seu jeito. Pixel art é muito bem-vinda. |
| Direitos | Sem logos, marcas ou personagens que não sejam seus (jogos, filmes, marcas, animes…). Desenhado do zero e "inspirado em" pode; cópia não. |
| Remix | Diga quem fez o original em `author`/`desc` e na postagem. |

### Testar

1. **Prévia no PC:** coloque o `.json` em [`faces/`](../faces) e rode `sh sim/build.sh` (veja [TECHNICAL.md](TECHNICAL.md#building)). Sai `faces/<id>.png` e a linha `pack-<id>  ok`, que confirma que o redesenho não deixa sobras.
2. **Instalar:** no painel, *Relógio → Mais mostradores → Instalar de um arquivo* (o `.json` e, se tiver, o `.png`). Uma recusa traz a mensagem do que corrigir.
3. **Prévia no relógio por 15 s:** o painel mostra o mostrador na tela do relógio por 15 s e depois volta ao anterior.
4. Confira **dia e noite** (com o tingimento e com brilho baixo), **chuva e sem chuva** se usou `when`, e **sem Wi-Fi** (os `--` não podem quebrar o layout). Deixe rodar alguns minutos: nada deve piscar nem deixar marcas.

### Compartilhar

1. Abra a [issue #84](https://github.com/davicorrea0/smalltv-pomodoro/issues/84) e anexe o **`.json`** e **sempre uma imagem** (foto do relógio ou a prévia `.png`); diga o nome, o que mostra e o firmware em que testou.
2. **Licença e crédito:** o crédito é seu (`author`). Ao enviar, você concorda que o mostrador seja distribuído e mostrado na galeria sob a GPL-3.0 (a licença do projeto), com crédito em `author`. Desenhos e imagens precisam ser seus ou de uso livre.

### Checklist final

1. A hora é a primeira coisa que se vê e dá para ler a 2 m.
2. O texto importante fica a 12 px ou mais das bordas; o secundário é `pixel` tamanho 2 ou mais.
3. Fundo escuro, ou conferido à noite se for claro.
4. Fica bom com o tingimento âmbar e com brilho baixo.
5. Nenhum par de cores que o RGB565 funda; sem faixas de degradê indesejadas.
6. Animação calma: 10 fps no máximo, nada piscando.
7. Não cobre o timer do foco.
8. Sem logos, marcas ou personagens que não são seus; remix com crédito.
9. Instala sem erro, funciona sem Wi-Fi e cabe nos [limites](#limites).
10. `.json` mais imagem postados na [#84](https://github.com/davicorrea0/smalltv-pomodoro/issues/84), com seu nome e o firmware testado.

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

O quadro vem do relógio do aparelho, então a animação anda sozinha e só a área do sprite é redesenhada. Um sprite com `px` continua parado. Limites: todos os quadros com o mesmo tamanho (até 32×32), no máximo **8 quadros** por sprite e **6 KB** de sprites no pacote (cada quadro de 32×32 ocupa 512 bytes; um de 16×16, 128). Veja um exemplo completo em [`faces/tomate-pulando.json`](../faces/tomate-pulando.json); outros animados: [`fogueira`](../faces/fogueira.json) (chama de 6 quadros), [`inverno`](../faces/inverno.json) (um sprite de neve repetido em duas escalas), [`gato-soneca`](../faces/gato-soneca.json), [`pausa-cafe`](../faces/pausa-cafe.json), [`aquario`](../faces/aquario.json) e [`janela`](../faces/janela.json) (chuva).

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

## Limites

- arquivo de até **16 KB**, até **48 camadas**;
- o relógio redesenha só o que muda: camadas com `{SS}` ou ponteiro de segundos são redesenhadas a cada segundo — prefira áreas pequenas para elas.

## Testar e publicar

1. Coloque o `.json` em [`faces/`](../faces) e rode `sim/build.sh`: o simulador desenha o pacote, confere o redesenho incremental (`pack-<id>  ok`), grava a prévia `faces/<id>.png` e atualiza `faces/catalog.json`.
2. Para entrar na galeria online, abra um pull request com o `.json`, a prévia e o catálogo atualizado.
