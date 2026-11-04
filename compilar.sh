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
#  - 4M2M: o teto é o do linker, ~1019 KB (1.044.464 bytes), e os 2 MB livres comportam firmware + .gz com folga;
#  - janela do layout antigo: um relógio ainda no 4M3M (1.23.x ou anterior) só enxerga 1 MB para a atualização:
#    firmware arredondado + .gz arredondado (com ~512 bytes do envio) <= 1 MB. Acima disso ele recusa a troca sem
#    risco ("firmware grande demais para descompactar"). Quem garante que ninguém fica preso é a PONTE
#    (scripts/ponte.cjs, aplicada pelo scripts/publicar.sh): o topo do latest.json/beta.json é sempre uma versão
#    que cabe nessa janela e tem o painel que conhece a ponte; a versão maior vai no campo "l2", que só o painel
#    novo lê nos relógios 4M2M. Este script só informa se a versão cabe ("janela antiga: cabe/não cabe").
#    RELOGIOS_4M3M=1 volta a recusar o build que não caiba (para compilar de propósito uma versão-ponte);
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
# janela de 1 MB do layout antigo: obrigatória no 4M3M e com RELOGIOS_4M3M=1; no 4M2M só é informada (a ponte decide)
AREA=2097152  # 4M2M: o teto é o do linker (1044464 bytes)
[ "$LAYOUT" = 4M3M ] || [ "${RELOGIOS_4M3M:-0}" = 1 ] && AREA=1048576
need=$(( $(r $size) + $(r $((gz + 512))) ))
if [ $need -gt $AREA ]; then
  echo "firmware com $size bytes (.gz $gz): não caberia na atualização comprimida feita pelo layout antigo (4M3M, 1 MB):"
  echo "firmware ($(r $size)) + .gz ($(r $((gz + 512)))) > $AREA. Reduza o código, ou compile sem RELOGIOS_4M3M=1 (a versão irá só para os relógios 4M2M, pela ponte)."
  exit 1
fi
if [ $need -le 1048576 ]; then echo "janela antiga: cabe (sobram $(( (1048576 - need) / 1024 )) KB; serve de ponte para os relógios no 4M3M)"
else echo "janela antiga: não cabe ($(( (need - 1048576) / 1024 )) KB a mais; só para os relógios 4M2M, campo l2 do latest.json)"; fi
[ "$size" -gt 524288 ] && echo "aviso: $size bytes; relógios com versão anterior à 1.14 precisam instalar a 1.14 antes"
if [ "$AREA" = 2097152 ]; then left=$(( 1044464 - size )); else left=$(( (AREA - $(r $size) - $(r $((gz + 512)))) * 100 / 174 )); fi
echo "firmware: $size bytes, .gz $gz bytes, layout $LAYOUT (ainda pode crescer ~$(( left / 1024 )) KB)"
