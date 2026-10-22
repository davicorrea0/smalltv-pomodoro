#!/bin/sh
# Compila o firmware em build/ (rode sim/build.sh antes: ele gera assets.h e web.h e testa as telas) e
# gera também o .bin.gz da atualização comprimida.
#
# build.float= tira da newlib o printf/scanf de números decimais (%f) e o strtod: ~7 KB que nada usa
# (o sim/build.sh recusa %f, toFloat e scanf no código, porque no relógio eles não imprimiriam nada).
#
# Limites (área do programa: 1 MB, em setores de 4 KB):
#  - atualização comprimida (a partir da 1.14): o .gz fica no fim da área e o firmware descompactado
#    precisa terminar antes dele; para trocar por uma versão do mesmo tamanho:
#    firmware arredondado + .gz arredondado (com ~512 bytes do envio) <= 1 MB (hoje: ~600 KB);
#  - instalar o .bin sem compressão a partir de versões anteriores à 1.14: no máximo 524.288 bytes
#    (aviso: acima disso essas versões precisam passar antes pela 1.14);
#  - voltar ao firmware original: sem compressão até ~532 KB; acima disso, pelo .gz dele (compactar.js).
cd "$(dirname "$0")"
CLI="${ARDUINO_CLI:-arduino-cli}"
"$CLI" compile --fqbn "esp8266:esp8266:generic:eesz=4M3M,xtal=160,ip=lm2n" --build-property "build.float=" \
  --output-dir build smalltv-pomodoro "$@" || exit 1
node compactar.js build/smalltv-pomodoro.ino.bin > /dev/null || exit 1
size=$(wc -c < build/smalltv-pomodoro.ino.bin)
gz=$(wc -c < build/smalltv-pomodoro.ino.bin.gz)
r() { echo $(( ($1 + 4095) / 4096 * 4096 )); }
if [ $(( $(r $size) + $(r $((gz + 512))) )) -gt 1048576 ]; then
  echo "firmware com $size bytes (.gz $gz): não caberia na próxima atualização comprimida"
  exit 1
fi
[ "$size" -gt 524288 ] && echo "aviso: $size bytes; relógios com versão anterior à 1.14 precisam instalar a 1.14 antes"
left=$(( 1048576 - $(r $size) - $(r $((gz + 512))) ))
echo "firmware: $size bytes, .gz $gz bytes (o firmware ainda pode crescer ~$(( left * 100 / 174 / 1024 )) KB)"
