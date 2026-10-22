#!/bin/sh
# Compila o firmware em build/ (rode sim/build.sh antes: ele gera assets.h e web.h e testa as telas).
#
# build.float= tira da newlib o printf/scanf de números decimais (%f) e o strtod: ~7 KB que nada usa
# (o sim/build.sh recusa %f, toFloat e scanf no código, porque no relógio eles não imprimiriam nada).
#
# No fim confere dois limites do tamanho (área do programa: 1 MB; a atualização pelo Wi-Fi grava a
# versão nova no espaço livre depois da atual, arredondada para setores de 4 KB, e reserva mais 4 KB):
#  - atualizar para uma versão do mesmo tamanho: tamanho + 4 KB + tamanho arredondado <= 1 MB,
#    ou seja, no máximo 520.192 bytes. Passar disso trava a próxima atualização pelo Wi-Fi;
#  - voltar ao firmware original da GeekMagic: o maior (V9.0.31, 509.792 bytes) precisa caber
#    no espaço livre, ou seja, no máximo 532.480 bytes (o primeiro limite já garante este).
cd "$(dirname "$0")"
CLI="${ARDUINO_CLI:-arduino-cli}"
"$CLI" compile --fqbn "esp8266:esp8266:generic:eesz=4M3M,xtal=160,ip=lm2n" --build-property "build.float=" \
  --output-dir build smalltv-pomodoro "$@" || exit 1
size=$(wc -c < build/smalltv-pomodoro.ino.bin)
rounded=$(( (size + 4095) / 4096 * 4096 ))
if [ $(( size + 4096 + rounded )) -gt 1048576 ]; then
  echo "firmware com $size bytes: passa de 520192 e a próxima atualização pelo Wi-Fi não caberia"
  exit 1
fi
echo "firmware: $size bytes (limite 520192; sobram $(( 520192 - size )) bytes)"
