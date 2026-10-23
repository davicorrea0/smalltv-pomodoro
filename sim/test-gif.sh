#!/bin/bash
# Linux/WSL: g++ with multilib. Corpus and count can be supplied as arguments.
set -euo pipefail
cd "$(dirname "$0")"
corpus="${1:-gif-seeds}"
cases="${2:-100000}"
test_out=$(mktemp -d /tmp/smalltv-gif-tests.XXXXXXXX)
flags=(-m32 -D_FILE_OFFSET_BITS=64 -O1 -g -w -D__LINUX__ -DreadFLASH=readMem
  -fsanitize=address,undefined -fno-sanitize=alignment -fno-omit-frame-pointer)
g++ "${flags[@]}" gifcheck_test.cpp -o "$test_out/validator"
g++ "${flags[@]}" gif_fuzz.cpp -o "$test_out/fuzz"
export ASAN_OPTIONS=detect_leaks=0
export UBSAN_OPTIONS=halt_on_error=1
"$test_out/validator"
for sample in "$corpus"/*.gif; do "$test_out/fuzz" run "$sample"; done
"$test_out/fuzz" fuzz-fast "$corpus" "$cases" "$test_out"
GIF_CALLBACKS=1 "$test_out/fuzz" fuzz-fast "$corpus" "$cases" "$test_out"
printf 'Test executables retained in %s\n' "$test_out"
