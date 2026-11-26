#!/bin/bash
# Testes de C++ do firmware (Linux/WSL, g++ com multilib), em 32 bits como o ESP8266, com ASan/UBSan:
#  - handlers reais de upload, host e senha (security_test.cjs) e histórico de focos (stats_test.cjs);
#  - foto sem memória × foto estragada (jpg_nomem_test.cjs);
#  - portal de Wi-Fi: o HTML e a gravação da rede dos handlers reais (portal_test.cjs);
#  - idioma padrão (inglês) e relógios já instalados que não mudam de idioma (idioma_test.cjs);
#  - UTF-8 da fonte pixel: alemão, russo, acentos reduzidos à letra base e entradas malformadas (utf8_test.cpp);
#  - telemetria do /api/state (campos aditivos, uma varredura do heap por pedido, tamanho do último pedaço) e a lista branca
#    do "Copiar diagnóstico" do painel (telemetria_test.cjs);
#  - /api/state completo e com cfg=0, cv derivado sem senhas e cache do painel (state_cfg_test.cjs);
#  - conexão do boot com o SDK sem GOT_IP e a estação com IP: conecta, marca boot.wf + 8, sem rede vai ao portal (wifi_ip_test.cjs);
#  - ETag do painel no GET /: 304 só com o painel desta versão, carregador sem ETag, login antes (painel_etag_test.cjs);
#  - cor quente (warm.h): os comandos do gama digital de dia, de noite e na prévia;
#  - curva de brilho (bri_curva.h) contra a fórmula com powf, em todas as entradas;
#    e o brilho noturno 0 (tela apagada) que acende para avisos e volta a apagar (noite.h);
#  - brilho no loop (backlight.cpp real): sem vai e vem nem reescrita com o alvo parado, troca sigma-delta/nível fixo sem piscar;
#  - sinf/cosf próprios (trig.cpp) iguais bit a bit aos do newlib (trig_test.cpp);
#  - GIF e JPEG: as amostras válidas precisam decodificar e o fuzzing não pode achar erro de memória;
#  - mostradores de pacote (facepack.cpp): os pacotes da galeria estragados de propósito, carregados e desenhados
#    (facepack_fuzz.cpp); precisa das bibliotecas Adafruit GFX e ArduinoJson (GFX e JSON, como no sim/build.sh).
# Uso: sim/testes.sh [casos de fuzzing por formato, padrão 5000]
# Os .cpp dos testes são gerados com node; sem node (WSL), passe a pasta já gerada em GEN.
set -euo pipefail
cd "$(dirname "$0")"
N="${1:-5000}"
T=$(mktemp -d /tmp/smalltv-testes.XXXXXX)
G="${GEN:-$T}"
if [ -z "${GEN:-}" ]; then
  node security_test.cjs "$G/sec.cpp"
  node stats_test.cjs "$G/stats.cpp"
  node jpg_nomem_test.cjs "$G/jpg_nomem.cpp"
  node portal_test.cjs "$G/portal.cpp"
  node idioma_test.cjs "$G/idioma.cpp"
  node telemetria_test.cjs "$G/telemetria.cpp"
  node state_cfg_test.cjs "$G/state_cfg.cpp"
  node wifi_ip_test.cjs "$G/wifi_ip.cpp"
  node painel_etag_test.cjs "$G/painel_etag.cpp"
  node brilho_loop_test.cjs "$G/brilho_loop.cpp"
fi
export ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1
g++ -std=c++17 -fsanitize=address,undefined -g sessoes_lista_test.cpp -o "$T/sessoes_lista"
"$T/sessoes_lista" "$N"
for t in sec stats jpg_nomem portal idioma telemetria state_cfg wifi_ip painel_etag brilho_loop; do
  g++ -std=c++17 -fsanitize=address,undefined -g -w "$G/$t.cpp" -o "$T/$t"
  "$T/$t"
done
g++ -std=c++17 -fsanitize=address,undefined -g -w warm_test.cpp -o "$T/warm"
"$T/warm"
g++ -m32 -std=c++17 -fsanitize=address,undefined -g utf8_test.cpp -o "$T/utf8"
"$T/utf8"
# Analisador de requisições do ESP8266WebServer (smalltv-pomodoro/ws_parse.h) sobre o Parsing-impl.h REAL do core 3.1.2, com
# cliente falso e relógio simulado: multipart de 33 campos, multipart truncado, envio parado, corpo grande (C1–C4 da
# auditoria). O mesmo teste sem o remendo precisa FALHAR (prova de que ele enxerga os defeitos).
CORE_WS="${CORE_WS:-}"
if [ -z "$CORE_WS" ]; then
  for d in "$HOME/.arduino15" "$HOME/Library/Arduino15" /mnt/c/Users/*/AppData/Local/Arduino15; do
    f="$d/packages/esp8266/hardware/esp8266/3.1.2/libraries/ESP8266WebServer/src/Parsing-impl.h"
    [ -f "$f" ] && { CORE_WS="$f"; break; }
  done
fi
[ -f "$CORE_WS" ] || { echo "testes: Parsing-impl.h do core 3.1.2 não encontrado (instale o core ou defina CORE_WS)"; exit 1; }
[ "$(tr -d '\r' < "$CORE_WS" | sha256sum | cut -d' ' -f1)" = f8fe756b04222f20c49813ea63f0c8a90a99034255324de84349e32755afe818 ] \
  || { echo "testes: $CORE_WS não é o Parsing-impl.h do core 3.1.2, de que o ws_parse.h é cópia"; exit 1; }
mkdir -p "$T/corews" && cp "$CORE_WS" "$T/corews/Parsing-impl.h"
# -fno-sanitize=null: o _parseArguments do core amarra uma referência a nullptr na primeira passada (inofensivo, não toca no objeto)
WSF="-std=c++17 -fsanitize=address,undefined -fno-sanitize=null -g -w -Ishim_ws -I$T/corews"
g++ $WSF -I../smalltv-pomodoro ws_parse_test.cpp -o "$T/ws_parse"
"$T/ws_parse" todos
g++ $WSF -DWS_UNPATCHED ws_parse_test.cpp -o "$T/ws_core"
for c in c1 c2_fechado c2_mudo c2_lixo c2_valor_enorme c4_mudo c3 c5 c6_host_stale c7_hdr_enorme c8_multipart_sem_upload; do
  if "$T/ws_core" "$c" > "$T/ws_core.log" 2>&1; then echo "testes: o core SEM o remendo passou em $c: o teste não enxerga o defeito"; exit 1; fi
done
[ -n "${GEN:-}" ] || node pacote_ram_test.cjs  # pacote de mostrador × álbum e OTA: ligação entre as peças
g++ -std=c++17 -fsanitize=address,undefined -g -w -Ishim brilho_test.cpp -o "$T/brilho"
"$T/brilho"
# sinf/cosf próprios (trig.cpp) contra o newlib, 1 float a cada 97 (todos: "$T/trig todos"); -O2 pelo tempo
g++ -std=c++17 -O2 -pthread -fsanitize=undefined,float-cast-overflow -fno-sanitize-recover -w trig_test.cpp -o "$T/trig"
"$T/trig"
[ -n "${GEN:-}" ] || node ip_cache_test.cjs  # makeState sem toString() do IP a cada desenho
[ -n "${GEN:-}" ] || node wifi_flash_test.cjs  # persistent do Wi-Fi só nas gravações de propósito (sem gravar a flash a cada ciclo)
[ -n "${GEN:-}" ] || node faceinstall_test.cjs  # instalar mostrador por cima de outro: sem remove antes do rename
# mostradores de pacote: as bibliotecas vêm do arduino-cli (no WSL, as do Windows em /mnt/c)
GFX="${GFX:-$HOME/Documents/Arduino/libraries/Adafruit_GFX_Library}"
JSON="${JSON:-$HOME/Documents/Arduino/libraries/ArduinoJson/src}"
if [ ! -d "$GFX" ] || [ ! -d "$JSON" ]; then
  for d in /mnt/c/Users/*/Documents/Arduino/libraries; do
    [ -d "$d/Adafruit_GFX_Library" ] && [ -d "$d/ArduinoJson/src" ] && { GFX="$d/Adafruit_GFX_Library"; JSON="$d/ArduinoJson/src"; break; }
  done
fi
[ -d "$GFX" ] && [ -d "$JSON" ] || { echo "testes: Adafruit GFX e ArduinoJson não encontrados (defina GFX e JSON)"; exit 1; }
FW=../smalltv-pomodoro
g++ -std=c++17 -fsanitize=address,undefined -g -O1 -w -DARDUINO=100 -DARDUINOJSON_ENABLE_ARDUINO_STRING=0 -DARDUINOJSON_ENABLE_ARDUINO_STREAM=0 \
  -DARDUINOJSON_ENABLE_ARDUINO_PRINT=0 -DARDUINOJSON_ENABLE_PROGMEM=0 -Ishim -I"$GFX" -I"$JSON" facepack_fuzz.cpp "$FW/facepack.cpp" "$FW/appdata.cpp" "$FW/ui.cpp" \
  "$FW"/face_*.cpp "$FW/fonts.cpp" "$FW/src/qrcodegen/qrcodegen.cpp" "$GFX/Adafruit_GFX.cpp" -o "$T/facepack_fuzz"
"$T/facepack_fuzz" ../faces "$N" apps-seeds ../docs/exemplos
# dados empurrados: POST /api/apps/data, {x.campo}, ttl na volta do millis, incremental x cheio, fuzz do parser
g++ -std=c++17 -fsanitize=address,undefined -g -O1 -w -DARDUINO=100 -DARDUINOJSON_ENABLE_ARDUINO_STRING=0 -DARDUINOJSON_ENABLE_ARDUINO_STREAM=0   -DARDUINOJSON_ENABLE_ARDUINO_PRINT=0 -DARDUINOJSON_ENABLE_PROGMEM=0 -Ishim -I"$GFX" -I"$JSON" appdata_test.cpp "$FW/facepack.cpp" "$FW/appdata.cpp" "$FW/ui.cpp"   "$FW"/face_*.cpp "$FW/fonts.cpp" "$FW/src/qrcodegen/qrcodegen.cpp" "$GFX/Adafruit_GFX.cpp" -o "$T/appdata_test"
"$T/appdata_test" apps-seeds/bento-casa.json "$N"
# pacote tokensmall (miniapps/tokensmall): campos empurrados pelo companheiro, "--" na janela sem leitura, incremental x cheio
g++ -std=c++17 -fsanitize=address,undefined -g -O1 -w -DARDUINO=100 -DARDUINOJSON_ENABLE_ARDUINO_STRING=0 -DARDUINOJSON_ENABLE_ARDUINO_STREAM=0   -DARDUINOJSON_ENABLE_ARDUINO_PRINT=0 -DARDUINOJSON_ENABLE_PROGMEM=0 -Ishim -I"$GFX" -I"$JSON" tokensmall_test.cpp "$FW/facepack.cpp" "$FW/appdata.cpp" "$FW/ui.cpp"   "$FW"/face_*.cpp "$FW/fonts.cpp" "$FW/src/qrcodegen/qrcodegen.cpp" "$GFX/Adafruit_GFX.cpp" -o "$T/tokensmall_test"
"$T/tokensmall_test" apps-seeds/tokensmall.json
# saída completa em arquivo; um erro em qualquer um interrompe tudo (set -e) e mostra o fim do log
for f in gif jpeg; do
  if ! bash "test-$f.sh" "$f-seeds" "$N" > "$T/$f.log" 2>&1; then
    tail -20 "$T/$f.log"
    echo "testes: FALHA no $f (log completo em $T/$f.log)"
    exit 1
  fi
done
echo "testes: ok (handlers, histórico, cor quente, $N casos de fuzzing de mostrador de pacote, de GIF e de JPEG)"
