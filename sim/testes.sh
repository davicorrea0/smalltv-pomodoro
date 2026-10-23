#!/bin/bash
# Testes de C++ do firmware (Linux/WSL, g++ com multilib), em 32 bits como o ESP8266, com ASan/UBSan:
#  - handlers reais de upload, host e senha (security_test.cjs) e histórico de focos (stats_test.cjs);
#  - GIF e JPEG: as amostras válidas precisam decodificar e o fuzzing não pode achar erro de memória.
# Uso: sim/testes.sh [casos de fuzzing por formato, padrão 5000]
# Os .cpp dos dois primeiros testes são gerados com node; sem node (WSL), passe a pasta já gerada em GEN.
set -euo pipefail
cd "$(dirname "$0")"
N="${1:-5000}"
T=$(mktemp -d /tmp/smalltv-testes.XXXXXX)
G="${GEN:-$T}"
if [ -z "${GEN:-}" ]; then
  node security_test.cjs "$G/sec.cpp"
  node stats_test.cjs "$G/stats.cpp"
fi
export ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1
for t in sec stats; do
  g++ -std=c++17 -fsanitize=address,undefined -g -w "$G/$t.cpp" -o "$T/$t"
  "$T/$t"
done
# saída completa em arquivo; um erro em qualquer um interrompe tudo (set -e) e mostra o fim do log
for f in gif jpeg; do
  if ! bash "test-$f.sh" "$f-seeds" "$N" > "$T/$f.log" 2>&1; then
    tail -20 "$T/$f.log"
    echo "testes: FALHA no $f (log completo em $T/$f.log)"
    exit 1
  fi
done
echo "testes: ok (handlers, histórico, $N casos de fuzzing de GIF e de JPEG)"
