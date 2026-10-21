#!/bin/sh
# Compila o firmware em build/ (rode sim/build.sh antes: ele gera assets.h e web.h e testa as telas).
#
# build.float= tira da newlib o printf/scanf de números decimais (%f) e o strtod: ~7 KB que nada usa
# (o sim/build.sh recusa %f, toFloat e scanf no código, porque no relógio eles não imprimiriam nada).
# No fim confere o tamanho: para voltar a qualquer firmware original da GeekMagic pelo /update, o nosso
# precisa deixar espaço para o maior deles (V9.0.31, 509.792 bytes): no máximo 532.480 bytes.
cd "$(dirname "$0")"
CLI="${ARDUINO_CLI:-arduino-cli}"
"$CLI" compile --fqbn "esp8266:esp8266:generic:eesz=4M3M,xtal=160,ip=lm2n" --build-property "build.float=" \
  --output-dir build smalltv-pomodoro "$@" || exit 1
size=$(wc -c < build/smalltv-pomodoro.ino.bin)
if [ "$size" -gt 532480 ]; then
  echo "firmware com $size bytes: passa de 532480 e não daria para voltar ao firmware original"
  exit 1
fi
echo "firmware: $size bytes (limite 532480)"
