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
#  - GIF e JPEG: as amostras válidas precisam decodificar e o fuzzing não pode achar erro de memória.
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
[ -n "${GEN:-}" ] || node ip_cache_test.cjs  # makeState sem toString() do IP a cada desenho
[ -n "${GEN:-}" ] || node wifi_flash_test.cjs  # persistent do Wi-Fi só nas gravações de propósito (sem gravar a flash a cada ciclo)
[ -n "${GEN:-}" ] || node faceinstall_test.cjs  # instalar mostrador por cima de outro: sem remove antes do rename
# saída completa em arquivo; um erro em qualquer um interrompe tudo (set -e) e mostra o fim do log
for f in gif jpeg; do
  if ! bash "test-$f.sh" "$f-seeds" "$N" > "$T/$f.log" 2>&1; then
    tail -20 "$T/$f.log"
    echo "testes: FALHA no $f (log completo em $T/$f.log)"
    exit 1
  fi
done
echo "testes: ok (handlers, histórico, cor quente, $N casos de fuzzing de GIF e de JPEG)"
