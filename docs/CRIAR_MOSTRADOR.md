# Como criar um mostrador

Um mostrador é **um arquivo** em `smalltv-pomodoro/` e **uma linha** em `faces_list.h`. O resto sai sozinho: o número do tema, a galeria do painel (com prévia), os testes e as imagens do simulador.

## 1. Crie o arquivo

`smalltv-pomodoro/face_<nome>.cpp`, com uma função `void themeX(const UiState &s, bool full)`:

```cpp
// Mostrador Grande: só a hora, enorme, e a data embaixo.
#include "ui_kit.h"

// layout HH:MM em pixel art: x de cada dígito, y, tamanho da célula, x dos dois-pontos
static const TimeLayout TL = {{20, 66, 136, 182}, 70, 8, 116};

void themeGrande(const UiState &s, bool full) {
  // full = primeira chamada (a tela já foi limpa com BG): desenhe o que é fixo aqui
  if (full) G->drawFastHLine(40, 150, 160, CARD2);

  // hora: drawTime4 só repinta os dígitos que mudaram
  if (s.timeOk) {
    int h = displayHour(s);
    drawTime4(TL, h / 10, h % 10, s.now.tm_min / 10, s.now.tm_min % 10,
              s.now.tm_sec % 2 == 0, s.accent, BG);
  }

  // data: changed() diz se o texto mudou desde a última vez
  char d[24];
  dateLabel(s, d, sizeof d);
  if (changed(cA, d)) {
    G->fillRect(0, 164, 240, 16, BG);
    textC(120, 164, 2, MUTED, d);
  }
}
```

## 2. Registre

No **fim** da lista em `smalltv-pomodoro/faces_list.h` (a posição é o número salvo nas configurações; nunca insira no meio):

```cpp
  X(WORDS, themeWords, "Palavras", "A hora por extenso, em português")   \
  X(GRANDE, themeGrande, "Grande", "Só a hora, enorme")
```

## 3. Veja no simulador

```
sim/build.sh
```

- `sim/out/face-N.png`: o mostrador como aparece na tela (N = posição na lista);
- a linha `<nome>  ok (0 pixels diferentes)` confirma que o redesenho incremental não deixa sobras. Se aparecer `FALHOU`, a área indicada tem algo que não é apagado quando muda;
- o mesmo script atualiza `docs/faces/` e `smalltv-pomodoro/faces.h` (prévias da galeria) e `web.h`.

## 4. Compile e confira o tamanho

```
arduino-cli compile --fqbn "esp8266:esp8266:generic:eesz=4M2M,xtal=160,ip=lm2n" --output-dir build smalltv-pomodoro
```

O `.bin` precisa ficar **abaixo de ~530 KB** para ainda ser possível voltar ao firmware de fábrica pelo painel.

## O que o `UiState` traz

| Campo | O que é |
|---|---|
| `timeOk`, `now` | hora válida e `struct tm` local (`tm_hour`, `tm_min`, `tm_sec`, `tm_wday`, `tm_mday`, `tm_mon`) |
| `hour12` | formato 12 h (use `displayHour(s)`) |
| `accent` | cor escolhida no painel para os números |
| `weatherOn`, `wx` | clima ligado e dados: `wx.ok`, `wx.t`, `wx.tmin`, `wx.tmax`, `wx.hum`, `wx.code`, `wx.day`, `wx.version` |
| `city`, `ip` | cidade e IP |
| `today`, `dailyGoal` | focos do dia e meta |

## O kit (`ui_kit.h`)

- **Texto** (fonte 6×8, só ASCII): `text`, `textC` (centralizado), `textR`, `textW`, `degree`, `tempText`, `tempDesc`, `weatherText`
- **Dígitos**: `dmDigit` / `drawTime4` (pixel art), `geoDigit` (fonte vetorial de traço, qualquer tamanho)
- **Clima**: `weatherIcon(cx, cy, escala, s.wx.code, s.wx.day, corDeFundo)`
- **Mascote e sprites**: `mascot(x, y, escala, FACE_HAPPY…)` e `uiSprite(&SPR_…)` (ver `assets/`)
- **Formas**: `thickLine`, `mix`, e tudo do Adafruit GFX via `G->` (`fillRoundRect`, `fillCircle`…)
- **Paleta**: `BG`, `CARD`, `CARD2`, `TEXT`, `MUTED`, `DIM`, `WORK`, `SHORT`, `LONG`, `SUN`, `CLOUD`, `RAIN`
- **Caches de redesenho**: `changed(cA…cC, texto)`, `lastDig`, `lastColon`, `lastMin`, `lastHour`, `lastWx` (compare com `s.wx.version`), `lastToday` — zerados a cada troca de tela

## Regras que evitam problemas no aparelho

- **Redesenhe só o que mudou.** A função roda a cada ~100 ms; apagar a tela inteira causa piscadas. Para trocar um texto, pinte o fundo só daquela área.
- **Sem flicker**: pinte por cima com a cor de fundo em vez de limpar e redesenhar (como `drawTime4` faz com os dígitos).
- **RAM é pouca (~80 KB no total)**: nada de buffers grandes. Tabelas constantes vão com `PROGMEM` e são lidas com `pgm_read_byte`/`pgm_read_word` — no ESP8266, um `static const` comum ocupa RAM.
- **Sem ponto flutuante no laço por pixel**: o ESP8266 não tem FPU. Conta por linha ou por objeto tudo bem; por pixel, use inteiros.
- **Animações** (como a virada do Flip) podem rodar num laço próprio: chame `uiFrameHook()` entre quadros (cede tempo ao Wi-Fi) e use `uiMillis()` para guiar pelo tempo.
- **Fontes de terceiros**: só com licença que permita embutir (ex.: SIL OFL) e com o arquivo da licença em `licenses/`.
