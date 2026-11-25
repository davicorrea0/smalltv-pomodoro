#!/bin/bash
# Render dourado do desenho real do álbum e referência GIF89a independente.
set -euo pipefail
cd "$(dirname "$0")/.."

OUT="${GIF_QUALIDADE_OUT:-pesquisas/gif-qualidade}"
TMP="$(mktemp -d /tmp/smalltv-gif-qualidade.XXXXXXXX)"
trap 'rm -rf "$TMP"' EXIT

if [ -n "${PYTHON:-}" ]; then
  PY=("$PYTHON")
elif command -v python3 >/dev/null 2>&1; then
  PY=(python3)
elif command -v python >/dev/null 2>&1; then
  PY=(python)
else
  echo "gif-qualidade: Python não encontrado" >&2
  exit 1
fi

compile() {
  local target="$1"
  shift
  for tentativa in 1 2 3; do
    if zig c++ -std=c++17 -O2 -D__LINUX__ -Wno-unused-function "$@" sim/gif_golden.cpp -o "$target"; then
      return
    fi
    echo "gif-qualidade: zig falhou na tentativa $tentativa" >&2
  done
  return 1
}

rm -rf "$OUT/firmware" "$OUT/firmware-antes" "$OUT/referencia" "$OUT/comparacoes" "$OUT/comparacoes-antes"
compile "$TMP/gif-golden"
"$TMP/gif-golden" --out "$OUT/firmware" sim/gif-album/*.gif
"$TMP/gif-golden" --legacy --out "$OUT/firmware-antes" sim/gif-album/*.gif
"${PY[@]}" sim/gif_referencia.py --out "$OUT" --firmware "$OUT/firmware"
"${PY[@]}" sim/gif_referencia.py --out "$OUT/antes" --firmware "$OUT/firmware-antes"
echo "gif-qualidade: ok"

