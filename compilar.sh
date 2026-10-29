#!/bin/sh
# Compila o firmware em build/ (rode sim/build.sh antes: ele gera assets.h e web.h e testa as telas) e
# gera também o .bin.gz da atualização comprimida.
#
# build.float= tira da newlib o printf/scanf de números decimais (%f) e o strtod: ~7 KB que nada usa
# (o sim/build.sh recusa %f, toFloat e scanf no código, porque no relógio eles não imprimiriam nada).
#
# Layout da flash (LAYOUT, padrão 4M2M): programa até ~1019 KB (limite do ESP8266, imposto pelo linker) e LittleFS de
# 2 MB em 0x200000. Até a 1.23 era 4M3M (LittleFS de 3 MB em 0x100000, só ~1 MB entre o programa e a memória de
# arquivos). LAYOUT=4M3M ainda compila a mesma fonte no layout antigo (versão de transição que só leva o painel novo).
#
# Limites (atualização comprimida: o .gz fica no fim da área livre e o firmware descompactado precisa terminar antes
# dele; o eboot descompacta por cima do firmware atual):
#  - TRANSIÇÃO (RELOGIOS_4M3M=1, o padrão): a primeira versão 4M2M chega por atualização pela rede feita pelo
#    firmware ANTIGO (4M3M), que só enxerga 1 MB: firmware arredondado + .gz arredondado (com ~512 bytes do envio)
#    <= 1 MB (1.23.1: 551 KB + .gz 407 KB). Passou disso, o build é recusado: relógios no layout antigo não
#    conseguiriam instalar. Quando todos estiverem no 4M2M, rode com RELOGIOS_4M3M=0: o teto vira o do linker,
#    ~1019 KB, e os 2 MB livres comportam firmware + .gz com folga;
#  - instalar o .bin sem compressão a partir de versões anteriores à 1.14: no máximo 524.288 bytes
#    (aviso: acima disso essas versões precisam passar antes pela 1.14);
#  - voltar ao firmware original: sem compressão até ~532 KB; acima disso, pelo .gz dele (compactar.js).
cd "$(dirname "$0")"
CLI="${ARDUINO_CLI:-arduino-cli}"
LAYOUT="${LAYOUT:-4M2M}"
case "$LAYOUT" in 4M2M|4M3M) ;; *) echo "LAYOUT deve ser 4M2M ou 4M3M"; exit 2 ;; esac
"$CLI" compile --fqbn "esp8266:esp8266:generic:eesz=$LAYOUT,xtal=160,ip=lm2n" --build-property "build.float=" \
  --output-dir build smalltv-pomodoro "$@" || exit 1
node compactar.js build/smalltv-pomodoro.ino.bin > /dev/null || exit 1
size=$(wc -c < build/smalltv-pomodoro.ino.bin)
gz=$(wc -c < build/smalltv-pomodoro.ino.bin.gz)
r() { echo $(( ($1 + 4095) / 4096 * 4096 )); }
# janela de 1 MB do layout antigo: vale no 4M3M e, enquanto houver relógios nele, também no 4M2M
AREA=1048576
[ "$LAYOUT" = 4M2M ] && [ "${RELOGIOS_4M3M:-1}" = 0 ] && AREA=2097152  # o teto passa a ser só o do linker (1044464 bytes)
if [ $(( $(r $size) + $(r $((gz + 512))) )) -gt $AREA ]; then
  echo "firmware com $size bytes (.gz $gz): não caberia na atualização comprimida feita pelo layout antigo (4M3M, 1 MB):"
  echo "firmware ($(r $size)) + .gz ($(r $((gz + 512)))) > $AREA. Reduza o código, ou use RELOGIOS_4M3M=0 se nenhum relógio ficou no layout antigo."
  exit 1
fi
[ "$size" -gt 524288 ] && echo "aviso: $size bytes; relógios com versão anterior à 1.14 precisam instalar a 1.14 antes"
if [ "$AREA" = 2097152 ]; then left=$(( 1044464 - size )); else left=$(( (AREA - $(r $size) - $(r $((gz + 512)))) * 100 / 174 )); fi
echo "firmware: $size bytes, .gz $gz bytes, layout $LAYOUT (ainda pode crescer ~$(( left / 1024 )) KB)"
