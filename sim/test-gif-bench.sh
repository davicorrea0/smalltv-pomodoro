#!/bin/bash
# Host: benchmark reproduz o album (RGB565 BE, RAW e MAX_WIDTH do firmware).
# Uso: bash sim/test-gif-bench.sh [repeticoes] [argumentos do gif_bench]
set -euo pipefail
cd "$(dirname "$0")/.."
repeticoes="${1:-15}"
shift || true
saida="${GIF_BENCH_OUT:-$(mktemp -d /tmp/smalltv-gif-bench.XXXXXXXX)/gif_bench}"

for tentativa in 1 2 3; do
  if zig c++ -std=c++17 -O2 -D__LINUX__ -Wno-unused-function sim/gif_bench.cpp -o "$saida"; then
    "$saida" --runs "$repeticoes" "$@"
    exit
  fi
  echo "gif-bench: zig falhou na tentativa $tentativa" >&2
done
echo "gif-bench: nao foi possivel compilar com zig c++" >&2
exit 1
