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
# ---- Limites de tamanho (a CI e o scripts/publicar.sh passam por aqui: estourar um deles faz o build FALHAR) ----
# MUDAR ESTES VALORES exige justificativa no PR que muda: data, novo valor, porquê.
# Ajuste pontual ou desligar pelo ambiente (0 desliga).
# Duas linhas desde 22/11/2026: o branch release/ponte gera as versões 1.26.x, que cabem na janela de 1 MB do layout
# antigo e levam os relógios até a 1.23 para o 4M2M; o main gera 1.27+, que podem passar da janela e só chegam aos
# relógios 4M2M pelo campo "l2" do JSON (scripts/ponte.cjs). Por isso a trava da janela só vale no release/ponte (e em
# PR para ele); no main ela fica em 0 e o teto passa a ser o do 4M2M (2 MB de atualização, menos LIMITE_OTA_KB).
RAMO="${GITHUB_BASE_REF:-${GITHUB_REF_NAME:-$(git rev-parse --abbrev-ref HEAD 2>/dev/null)}}"
if [ "$RAMO" = release/ponte ]; then RELOGIOS_4M3M="${RELOGIOS_4M3M:-1}"; JANELA_PADRAO=8; else JANELA_PADRAO=0; fi
LIMITE_JANELA_KB="${LIMITE_JANELA_KB:-$JANELA_PADRAO}"   # folga mínima na janela de 1 MB do layout antigo (firmware + .gz, a conta da ponte)
LIMITE_OTA_KB="${LIMITE_OTA_KB:-16}"        # 4M2M: margem abaixo dos 2 MB da atualização comprimida (firmware + .gz)
LIMITE_RAM="${LIMITE_RAM:-39936}"           # RAM fixa máxima em bytes (DATA + RODATA + BSS); sobra o resto da 80 KB para pilha e heap
CLI="${ARDUINO_CLI:-arduino-cli}"
LAYOUT="${LAYOUT:-4M2M}"
case "$LAYOUT" in 4M2M|4M3M) ;; *) echo "LAYOUT deve ser 4M2M ou 4M3M"; exit 2 ;; esac
mkdir -p build
# Sem o objeto global Wire (I2C, que o relógio não usa): -DNO_GLOBAL_TWOWIRE tira o objeto do core (−296 B de RAM
# fixa) e o -include do sem_wire.h declara o extern que a Adafruit BusIO pede. As duas andam juntas: só a flag quebra
# na hora em Adafruit_I2CDevice.h ("Wire was not declared"); sem nenhuma das duas compila normal, só 296 B maior.
# Vai em compiler.cpp.extra_flags (vazio no platform.txt do core 3.1.2), nunca em build.extra_flags, que é das opções
# da placa. A CI e o scripts/publicar.sh compilam por aqui.
"$CLI" compile --fqbn "esp8266:esp8266:generic:eesz=$LAYOUT,xtal=160,ip=lm2n" --build-property "build.float=" \
  --build-property "compiler.cpp.extra_flags=-DNO_GLOBAL_TWOWIRE -DARDUINOJSON_USE_DOUBLE=0 -fno-threadsafe-statics \"-include{build.source.path}/sem_wire.h\"" \
  --output-dir build smalltv-pomodoro "$@" > build/compilar.log 2>&1; rc=$?
cat build/compilar.log
[ $rc -eq 0 ] || exit 1
node compactar.js build/smalltv-pomodoro.ino.bin > /dev/null || exit 1
size=$(wc -c < build/smalltv-pomodoro.ino.bin)
gz=$(wc -c < build/smalltv-pomodoro.ino.bin.gz)
r() { echo $(( ($1 + 4095) / 4096 * 4096 )); }
# janela de 1 MB do layout antigo: obrigatória no 4M3M e com RELOGIOS_4M3M=1; no 4M2M só é informada (a ponte decide)
AREA=$(( 2097152 - LIMITE_OTA_KB * 1024 ))  # 4M2M: o teto do programa é o do linker (1044464 bytes)
[ "$LAYOUT" = 4M3M ] || [ "${RELOGIOS_4M3M:-0}" = 1 ] && AREA=1048576
need=$(( $(r $size) + $(r $((gz + 512))) ))
if [ $need -gt $AREA ] && [ $AREA -gt 1048576 ]; then
  echo "ERRO firmware: firmware ($(r $size)) + .gz ($(r $((gz + 512)))) = $need bytes passa do teto da atualização comprimida no 4M2M ($AREA = 2 MB - $LIMITE_OTA_KB KB). Reduza o código."
  exit 1
elif [ $need -gt $AREA ]; then
  echo "firmware com $size bytes (.gz $gz): não caberia na atualização comprimida feita pelo layout antigo (4M3M, 1 MB):"
  echo "firmware ($(r $size)) + .gz ($(r $((gz + 512)))) > $AREA. Reduza o código, ou compile sem RELOGIOS_4M3M=1 (a versão irá só para os relógios 4M2M, pela ponte)."
  exit 1
fi
if [ $need -le 1048576 ]; then echo "janela antiga: cabe (sobram $(( (1048576 - need) / 1024 )) KB; serve de ponte para os relógios no 4M3M)"
else echo "janela antiga: não cabe ($(( (need - 1048576) / 1024 )) KB a mais; só para os relógios 4M2M, campo l2 do latest.json)"; fi
[ "$size" -gt 524288 ] && echo "aviso: $size bytes; relógios com versão anterior à 1.14 precisam instalar a 1.14 antes"
if [ "$AREA" -gt 1048576 ]; then left=$(( 1044464 - size )); else left=$(( (AREA - $(r $size) - $(r $((gz + 512)))) * 100 / 174 )); fi
echo "firmware: $size bytes, .gz $gz bytes, layout $LAYOUT (ainda pode crescer ~$(( left / 1024 )) KB)"
# ---- confere os limites (mensagens com "firmware" para o grep do publicar.sh) ----
falhou=0
ram=$(awk '{ for (i = 1; i <= NF; i++) if ($i ~ /^(DATA|RODATA|BSS)$/) s += $(i + 1) } END { print s + 0 }' build/compilar.log)
if [ "$LIMITE_RAM" -gt 0 ] && [ "$ram" -gt "$LIMITE_RAM" ]; then
  echo "ERRO firmware: RAM fixa (DATA+RODATA+BSS) = $ram bytes, acima do limite de $LIMITE_RAM (estoura em $(( ram - LIMITE_RAM )) bytes). Reduza a RAM, ou mude o limite (LIMITE_RAM) justificando no PR."
  falhou=1
fi
folga=$(( 1048576 - need ))
if [ "$LIMITE_JANELA_KB" -gt 0 ] && [ "$folga" -lt $(( LIMITE_JANELA_KB * 1024 )) ]; then
  echo "ERRO firmware: folga na janela de 1 MB do layout antigo = $(( folga / 1024 )) KB ($folga bytes), abaixo do mínimo de $LIMITE_JANELA_KB KB (firmware $size bytes, .gz $gz bytes). Reduza o código, ou mude o limite (LIMITE_JANELA_KB; 0 desliga) justificando no PR."
  falhou=1
fi
if [ $falhou -eq 0 ]; then
  echo "firmware: limites ok (RAM fixa $ram de $LIMITE_RAM bytes; folga da janela $(( folga / 1024 )) KB, mínimo $LIMITE_JANELA_KB KB)"
fi
# ---- informativo: 15 maiores símbolos de RAM fixa (.data, .rodata, .bss) ----
nm_bin=""
tc_dir=$("$CLI" compile --fqbn "esp8266:esp8266:generic:eesz=$LAYOUT,xtal=160,ip=lm2n" smalltv-pomodoro --show-properties 2>/dev/null | awk -F= '
  { sub(/\r$/, "") }
  $1 == "compiler.path" && $2 != "" && $2 !~ /\{/ { p = substr($0, index($0, "=") + 1) }
  $1 == "runtime.tools.xtensa-lx106-elf-gcc.path" && $2 != "" && $2 !~ /\{/ && !p { p = substr($0, index($0, "=") + 1) "/bin" }
  END { gsub(/\\/, "/", p); sub(/\/+$/, "", p); print p }
')
if [ -n "$tc_dir" ]; then
  if [ -x "$tc_dir/xtensa-lx106-elf-nm" ]; then
    nm_bin="$tc_dir/xtensa-lx106-elf-nm"
  elif [ -x "$tc_dir/xtensa-lx106-elf-nm.exe" ]; then
    nm_bin="$tc_dir/xtensa-lx106-elf-nm.exe"
  fi
fi
if [ -z "$nm_bin" ]; then
  nm_bin=$(command -v xtensa-lx106-elf-nm 2>/dev/null || true)
fi
if [ -z "$nm_bin" ]; then
  echo "aviso: xtensa-lx106-elf-nm não encontrado; pulando lista dos maiores símbolos de RAM fixa"
elif [ -f build/smalltv-pomodoro.ino.elf ]; then
  echo "maiores símbolos em RAM fixa (.data, .rodata, .bss):"
  "$nm_bin" -C -S -t d -f sysv build/smalltv-pomodoro.ino.elf 2>/dev/null | awk -F'|' '
    { sub(/\r$/, "") }
    NF >= 7 {
      sec = $NF; gsub(/^[ \t]+|[ \t]+$/, "", sec)
      if (sec != ".data" && sec != ".rodata" && sec != ".bss") next
      sz = $(NF - 2); gsub(/[ \t]+/, "", sz); sub(/^0+/, "", sz)
      if (sz !~ /^[0-9]+$/) next
      name = $1
      for (i = 2; i <= NF - 6; i++) name = name "|" $i
      gsub(/^[ \t]+|[ \t]+$/, "", name)
      if (name != "") printf "  %7d  %-7s  %s\n", sz + 0, sec, name
    }
  ' | LC_ALL=C sort -rn | head -n 15 || true
fi
[ $falhou -eq 0 ] || exit 1
