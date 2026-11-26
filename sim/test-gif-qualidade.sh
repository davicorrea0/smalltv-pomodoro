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
  # zig no Windows (o antivírus às vezes trava o primeiro .exe: 3 tentativas); g++ no CI e no Linux
  local cxx=(g++)
  command -v zig >/dev/null 2>&1 && cxx=(zig c++)
  for tentativa in 1 2 3; do
    if "${cxx[@]}" -std=c++17 -O2 -D__LINUX__ -Wno-unused-function "$@" sim/gif_golden.cpp -o "$target"; then
      return
    fi
    echo "gif-qualidade: ${cxx[*]} falhou na tentativa $tentativa" >&2
  done
  return 1
}

# GIFs do teste: sim/gif-album/ (fora do git, só na máquina de quem tem os GIFs) ou, sem ela (CI), GIFs do próprio
# projeto: tela cheia, pequeno ampliado 4x e uma animação de 16 quadros.
GIFS=(sim/gif-album/*.gif)
[ -e "${GIFS[0]}" ] || GIFS=(sim/gif-seeds/ff_240.gif sim/gif-seeds/ff_small.gif docs/transicao-lcd-ida.gif)
echo "gif-qualidade: ${#GIFS[@]} GIFs (${GIFS[*]})"

rm -rf "$OUT/firmware" "$OUT/firmware-sem-limpeza" "$OUT/firmware-sem-filtro" "$OUT/firmware-antes" \
  "$OUT/referencia" "$OUT/comparacoes" "$OUT/original" "$OUT/comparacoes-antes"
compile "$TMP/gif-golden"
compile "$TMP/gif-golden-sem-limpeza" -DGIF_LIMPEZA_PALETA=0 -DGIF_FILTRO_HORIZONTAL=0
compile "$TMP/gif-golden-sem-filtro" -DGIF_FILTRO_HORIZONTAL=0
"$TMP/gif-golden" --out "$OUT/firmware" "${GIFS[@]}"
"$TMP/gif-golden-sem-limpeza" --out "$OUT/firmware-sem-limpeza" "${GIFS[@]}"
"$TMP/gif-golden-sem-filtro" --out "$OUT/firmware-sem-filtro" "${GIFS[@]}"
"$TMP/gif-golden" --legacy --out "$OUT/firmware-antes" "${GIFS[@]}"
"${PY[@]}" sim/gif_referencia.py --gifs "${GIFS[@]}" --out "$OUT" --firmware "$OUT/firmware"
"${PY[@]}" sim/gif_referencia.py --gifs "${GIFS[@]}" --out "$OUT/original" --limiar -1 --sem-filtro-horizontal \
  --firmware "$OUT/firmware-sem-limpeza"
"${PY[@]}" sim/gif_ruido.py --original "$OUT/original/referencia" \
  --sem-limpeza "$OUT/firmware-sem-limpeza" --so-paleta "$OUT/firmware-sem-filtro" \
  --com-limpeza "$OUT/firmware" --out "$OUT"
echo "gif-qualidade: ok"
