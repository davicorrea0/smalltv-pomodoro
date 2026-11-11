#!/bin/bash
# Testes de C++ do firmware (Linux/WSL, g++ com multilib), em 32 bits como o ESP8266, com ASan/UBSan:
#  - handlers reais de upload, host e senha (security_test.cjs) e histórico de focos (stats_test.cjs);
#  - foto sem memória × foto estragada (jpg_nomem_test.cjs);
#  - portal de Wi-Fi: o HTML e a gravação da rede dos handlers reais (portal_test.cjs);
#  - idioma padrão (inglês) e relógios já instalados que não mudam de idioma (idioma_test.cjs);
#  - telemetria do /api/state (campos aditivos, uma varredura do heap por pedido, tamanho do último pedaço) e a lista branca
#    do "Copiar diagnóstico" do painel (telemetria_test.cjs);
#  - ETag do painel no GET /: 304 só com o painel desta versão, carregador sem ETag, login antes (painel_etag_test.cjs);
#  - cor quente (warm.h): os comandos do gama digital de dia, de noite e na prévia;
#  - curva de brilho (bri_curva.h) contra a fórmula antiga com powf, em todas as entradas;
#  - brilho noturno 0 (tela apagada) que acende para avisos e volta a apagar (noite.h);
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
  node painel_etag_test.cjs "$G/painel_etag.cpp"
fi
export ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1
for t in sec stats jpg_nomem portal idioma telemetria painel_etag; do
  g++ -std=c++17 -fsanitize=address,undefined -g -w "$G/$t.cpp" -o "$T/$t"
  "$T/$t"
done
g++ -std=c++17 -fsanitize=address,undefined -g -w warm_test.cpp -o "$T/warm"
"$T/warm"
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
  -DARDUINOJSON_ENABLE_ARDUINO_PRINT=0 -DARDUINOJSON_ENABLE_PROGMEM=0 -Ishim -I"$GFX" -I"$JSON" facepack_fuzz.cpp "$FW/facepack.cpp" "$FW/ui.cpp" \
  "$FW"/face_*.cpp "$FW/fonts.cpp" "$FW/src/qrcodegen/qrcodegen.cpp" "$GFX/Adafruit_GFX.cpp" -o "$T/facepack_fuzz"
"$T/facepack_fuzz" ../faces "$N"
# saída completa em arquivo; um erro em qualquer um interrompe tudo (set -e) e mostra o fim do log
for f in gif jpeg; do
  if ! bash "test-$f.sh" "$f-seeds" "$N" > "$T/$f.log" 2>&1; then
    tail -20 "$T/$f.log"
    echo "testes: FALHA no $f (log completo em $T/$f.log)"
    exit 1
  fi
done
echo "testes: ok (handlers, histórico, cor quente, $N casos de fuzzing de mostrador de pacote, de GIF e de JPEG)"
