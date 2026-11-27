# Otimizações pós-1.26.3 (CPU, SPI, heap e HTTP)

Registro das melhorias aplicadas sobre a tag `v1.26.3` (`0cb9b9e`) no branch `perf/otimizacoes-1.26.3`, preservando o comportamento funcional, a renderização bit-a-bit de GIFs e os limites de segurança/memória do ESP8266.

## 1. CPU e barramento SPI

### Cache de `rotEspera()` no rodízio de mostradores
- **Arquivo:** `smalltv-pomodoro/modulos/album.cpp`
- **Problema:** `rotLoop()` roda a cada iteração do `loop()` principal e chamava `rotEspera()` toda vez, executando `snprintf` (`rotChave`) e duas varreduras lineares de até 300 bytes (`rotList` e `rotTimes`) centenas ou milhares de vezes por segundo.
- **Mudança:** memoização de `rotEspera()` em `rotCacheMs`, indexada por `(cfg.theme, cfg.facePkg, cfg.rotMin)` e invalidada explicitamente em `rotSet()`, `rotTimesSet()` e `rotTimesLoad()`.
- **Efeito:** comparações em $O(1)$ no hot-loop, mantendo reação imediata a qualquer troca de intervalo, lista, tempos ou mostrador.

### Caminho rápido para GIFs transparentes ampliados (`scale > 1`)
- **Arquivo:** `smalltv-pomodoro/album_gif_renderer.h`
- **Problema:** no ramo `ucHasTransparency`, o laço `for (int repeatY = 0; repeatY < scale; repeatY++)` recalculava os spans opacos e remontava `buf` do zero em cada linha repetida verticalmente, além de abrir `scale` janelas `setAddrWindow(..., 1)` por span.
- **Mudança:** quando todas as linhas repetidas têm o mesmo comportamento de transparência (`!st.clearTrans`, disposal 0/1, ou restauração cobrindo o bloco inteiro), os pixels de cada linha/span são montados uma única vez e enviados em uma única janela `setAddrWindow(..., ry1 - ry0)` repetindo apenas `writePixelsBE()`. O caso parcial de disposal 2 mantém o tratamento linha a linha.
- **Efeito:** reduz trabalho de CPU e comandos SPI em até `scale`× (2×–4×) nos GIFs pequenos com transparência.

### Preparação de paleta do GIF sem expansão desnecessária
- **Arquivo:** `smalltv-pomodoro/album_gif_renderer.h`
- **Problema:** `albumGifPreparePalette()` convertia todas as 256 cores de RGB565 para RGB888 e preenchia 768 bytes em `st.rgb` a cada quadro, mesmo em pixel art limpa (`nearBlack == 0`) em que o filtro horizontal não é ativado.
- **Mudança:** detecção de quase pretos (`GIF_LIMIAR_PRETO == 12`, garantida por `static_assert`) por máscara direta de bits em RGB565 big-endian (`(p & 0x9EF7u) == 0`), preenchendo `st.rgb` somente quando `nearBlack > 0`.
- **Efeito:** elimina conversões RGB565→RGB888 e escritas de 768 B por quadro em GIFs sem ruído.

### Transação SPI única por glifo cirílico
- **Arquivo:** `smalltv-pomodoro/ui.cpp`
- **Problema:** `uiDrawCyr()` chamava `G->fillRect(...)` para cada segmento vertical de cada coluna, disparando de 5 a 8 transações SPI separadas por letra.
- **Mudança:** envolvimento do glifo com `G->startWrite()` / `G->endWrite()` e uso de `G->writeFillRect(...)` (sem aninhar `startWrite()` em torno de `G->drawChar()`).
- **Efeito:** reduz o overhead de SPI para 1 transação por glifo cirílico tanto em `ui.cpp` quanto em `facepack.cpp`.

## 2. Heap, fragmentação e I/O de flash

### Critério real de bloco contíguo em `albumMakeRoom()`
- **Arquivos:** `smalltv-pomodoro/modulos/album.cpp`, `sim/jpg_nomem_test.cjs`, `sim/pacote_ram_test.cjs`
- **Problema:** `albumMakeRoom()` exigia `ESP.getMaxFreeBlockSize()` maior que a soma total dos buffers (~20,5 KB para GIF; ~17 KB para JPEG), mas desde a 1.26.3 o GIF aloca 4 blocos separados (maior: `GIF_TABLE_BYTES = 8.192 B`) e o JPEG aloca `JPEGDEC` (~7,7 KB) + `2×4 KB` separados (`usHuffAC`). Com o heap moderadamente fragmentado, o pacote de mostrador era descarregado à toa.
- **Mudança:** `albumMakeRoom(totalNeed, maxBlockNeed)` agora libera o pacote somente quando `ESP.getFreeHeap() < totalNeed || ESP.getMaxFreeBlockSize() < maxBlockNeed`.
- **Efeito:** menos despejos falsos de pacote de mostrador e menos releituras do LittleFS no modo alternado.

### Menos realocações por linha HTTP em `wsReadLine()`
- **Arquivos:** `smalltv-pomodoro/ws_parse.h`, `sim/shim_ws/Arduino.h`
- **Problema:** cada linha HTTP crescia byte a byte sem reserva inicial e removia `\r` via `out = out.substring(0, out.length() - 1)`, gerando uma cópia inteira de `String` por cabeçalho.
- **Mudança:** `out.reserve(min(maxLen, 64))` no início e remoção in-place via `out.remove(out.length() - 1)`.

### Cache do cabeçalho Basic Auth em `passOk()`
- **Arquivo:** `smalltv-pomodoro/modulos/web_portal.cpp`
- **Problema:** a cada requisição autenticada (incluindo o polling de `/api/state`), `passOk()` alocava 3 `String`s temporárias e recalculava Base64 de `"admin:" + cfg.otaPass`.
- **Mudança:** cabeçalho `"Basic <base64>"` guardado em buffer estático `authBasic[48]`, invalidado/zerado em `/api/otapass`.

### Cache de `/apps.key` em RAM
- **Arquivo:** `smalltv-pomodoro/modulos/web_portal.cpp`
- **Problema:** `appsWriteAuth()` abria, lia e fechava `/apps.key` no LittleFS a cada `POST /api/apps/data` ou `/api/apps/show`.
- **Mudança:** cache de 33 bytes em RAM (`appsKeyCache` / `appsKeyState`), invalidado/zerado em `/api/appskey` (`new`/`off`).

### Serialização de sessões sem `String` temporária por item
- **Arquivos:** `smalltv-pomodoro/app.h`, `smalltv-pomodoro/modulos/web_portal.cpp`
- **Problema:** `sessoesJsonLinha()` criava um `String nome` apenas para terminar em `\0` antes de `jsonEscapeAppend`.
- **Mudança:** sobrecarga `jsonEscapeAppend(String &o, const char *s, size_t len)` usada diretamente sobre `s->nome` / `s->nome_len`.

### Alinhamento explícito na resposta de `/api/apps/show`
- **Arquivo:** `smalltv-pomodoro/modulos/web_portal.cpp`
- **Mudança:** `alignas(4) char reply[24]` antes de `server.send_P(200, PSTR("application/json"), reply, strlen(reply))`, mantendo zero alocação de `String` e garantindo alinhamento de 32 bits exigido pelo `memcpy_P` no Xtensa.

### Lista do álbum em buffer único contíguo (`AlbumList`)
- **Arquivos:** `smalltv-pomodoro/app.h`, `smalltv-pomodoro/modulos/album.cpp`, `smalltv-pomodoro/modulos/web_portal.cpp`
- **Problema:** `std::vector<String> album` mantinha até 128 alocações individuais de `String` espalhadas no heap o tempo todo, fragmentando o espaço livre disputado por GIFs e JPEGs.
- **Mudança:** `AlbumList` armazena todos os caminhos separados por `\0` em um único bloco contíguo no heap (`realloc` em múltiplos de 256 B) com vetor compacto de offsets `uint16_t`.
- **Efeito:** cai de até ~129 blocos residentes no heap para 2 (e 0 B quando o álbum está vazio), reduzindo fragmentação.

## 3. Painel web

### Polling adaptativo e pausa em aba oculta
- **Arquivos:** `web/index.html`, `smalltv-pomodoro/web.h`
- **Problema:** `poll()` disparava `/api/state?cfg=0` a cada 1 s mesmo com a aba em segundo plano (`document.hidden`), com o relógio parado (`idle`) ou fora da rede, e `draw()` atualizava o DOM a cada 250 ms mesmo oculto.
- **Mudança:**
  - `poll()` pausa completamente quando `document.hidden` é `true` e retoma imediatamente no evento `visibilitychange`.
  - Com o relógio em `idle` fora da aba Ajustes/Diagnóstico (`curTab != 3`), o intervalo sobe de 1 s para 3 s (mantendo 1 s com Pomodoro ativo ou no diagnóstico).
  - Em erro de rede, aplica backoff exponencial (`2 s → 4 s → 8 s → 10 s`), zerado ao reconectar ou voltar à aba.
  - `draw()` pula atualizações de DOM quando a aba está oculta e o Pomodoro não está rodando.
- **Efeito:** reduz drasticamente o tráfego HTTP e sockets em `TIME_WAIT` no ESP8266 quando a aba fica aberta em background.

## 4. Validação e métricas do build

Compilação ESP8266 (`esp8266:esp8266:generic:eesz=4M2M,xtal=160,ip=lm2n`, `build.float=`):
- **RAM fixa (DATA + RODATA + BSS):** `36.408 / 80.192 B` (45%; limite `39.936 B`)
- **IRAM:** `59.851 / 65.536 B` (91%)
- **IROM (flash):** `577.860 / 1.048.576 B` (55%)
- **Binário:** `615.616 B` → **`.bin.gz`:** `455.759 B` (74%)

Testes executados com sucesso:
- `sim/painel_test.cjs`: validações do painel, editor, rodízio e uploads.
- `sim/testes.sh`: todos os testes de segurança, telemetria, `state_cfg`, `ws_parse_test` (16 casos), `jpg_nomem` (incluindo novos casos de heap fragmentado), `pacote_ram`, `rodizio`, `sessoes_lista`, `utf8_pixel` (90.550 casos), `appdata_test` e `facepack_fuzz` (5.000 casos).
- `sim/test-gif-qualidade.sh`: `render dourado: 25 quadros, 0 pixels diferentes` nos GIFs de referência (`ff_240.gif`, `ff_small.gif`, `transicao-lcd-ida.gif`).
