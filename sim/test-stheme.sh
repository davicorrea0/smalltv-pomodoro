#!/bin/bash
# Linux/WSL: g++ com multilib. Leitor de pacotes .stheme (smalltv-pomodoro/stheme.h), em 32 bits como o
# ESP8266, com ASan/UBSan: o teste das amostras e dos defeitos conhecidos, depois o fuzzing a partir das
# amostras. Corpus e número de casos podem vir nos argumentos; a semente, em STHEME_SEED.
set -euo pipefail
cd "$(dirname "$0")"
corpus="${1:-stheme-seeds}"
cases="${2:-100000}"
out=$(mktemp -d /tmp/smalltv-stheme-tests.XXXXXXXX)
flags=(-m32 -D_FILE_OFFSET_BITS=64 -std=c++17 -O1 -g -Wall -Wextra -Werror -Ishim
  -fsanitize=address,undefined -fno-sanitize=alignment -fno-omit-frame-pointer)
g++ "${flags[@]}" stheme_test.cpp -o "$out/test"
g++ "${flags[@]}" stheme_fuzz.cpp -o "$out/fuzz"
export ASAN_OPTIONS=detect_leaks=0
export UBSAN_OPTIONS=halt_on_error=1
"$out/test" "$corpus"
"$out/fuzz" fuzz "$corpus" "$cases" "${STHEME_SEED:-1}"
printf 'Test executables retained in %s\n' "$out"
