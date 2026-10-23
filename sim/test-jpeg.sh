#!/bin/bash
# Linux/WSL: g++ com multilib. Amostras válidas precisam decodificar; depois, fuzzing com ASan/UBSan
# em 32 bits (como o ESP8266), cada caso num processo. Corpus e número de casos podem vir nos argumentos.
set -euo pipefail
cd "$(dirname "$0")"
corpus="${1:-jpeg-seeds}"
cases="${2:-20000}"
out=$(mktemp -d /tmp/smalltv-jpeg-tests.XXXXXXXX)
flags=(-m32 -D_FILE_OFFSET_BITS=64 -O1 -g -w -D__LINUX__
  -fsanitize=address,undefined -fno-sanitize=alignment -fno-omit-frame-pointer)
g++ "${flags[@]}" jpeg_fuzz.cpp -o "$out/fuzz"
export ASAN_OPTIONS=detect_leaks=0
export UBSAN_OPTIONS=halt_on_error=1
"$out/fuzz" valid "$corpus"/*.jpg
"$out/fuzz" fork "$corpus" "$cases" "$out"
printf 'Test executables retained in %s\n' "$out"
