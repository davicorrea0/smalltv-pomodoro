#!/bin/sh
# Gera assets.h, compila e roda o simulador; gera out/*.png, out/folha.png e out/assets.png.
# Requer: zig (https://ziglang.org), node e a biblioteca Adafruit GFX instalada pelo arduino-cli.
cd "$(dirname "$0")"
# módulos: todo modulos/*.cpp precisa de #include "modulos/x.cpp" no main.cpp (compila como um arquivo só) e vice-versa
FW=../smalltv-pomodoro
for f in "$FW"/modulos/*.cpp; do
  grep -q "^#include \"modulos/$(basename "$f")\"" "$FW/main.cpp" || { echo "ERRO: modulos/$(basename "$f") não tem #include no main.cpp; o arquivo ficaria fora do firmware (acrescente a linha ao lado dos outros)"; exit 1; }
done
for m in $(sed -n 's/^#include "\(modulos\/[^"]*\.cpp\)".*/\1/p' "$FW/main.cpp"); do
  [ -f "$FW/$m" ] || { echo "ERRO: main.cpp inclui $m, mas o arquivo não existe"; exit 1; }
done
# o arduino-cli compila src/ inteira (bibliotecas embutidas são ok): .cpp solto na raiz de src/ ou cópia de módulo em src/ seria compilado em dobro
for f in "$FW"/src/*.cpp $(for g in "$FW"/modulos/*.cpp; do find "$FW/src" -name "$(basename "$g")"; done); do
  [ -f "$f" ] && { echo "ERRO: $f em src/ seria compilado em dobro pelo arduino-cli (módulos ficam em modulos/, incluídos pelo main.cpp)"; exit 1; }
done
ZIG=$(command -v zig)
if [ -z "$ZIG" ] && [ -n "$LOCALAPPDATA" ]; then  # Windows: zig instalado pelo winget fora do PATH do shell
  ZIG=$(ls "$(cygpath -u "$LOCALAPPDATA")"/Microsoft/WinGet/Packages/zig.zig*/zig-*/zig.exe 2>/dev/null | head -1)
fi
[ -z "$ZIG" ] && ! command -v g++ >/dev/null && { echo "zig (ou g++ com multilib) não encontrado"; exit 1; }
node ../assets/gerar.js || exit 1  # assets/*.txt -> smalltv-pomodoro/assets.h
# o firmware é compilado sem printf/scanf de float (compilar.sh): %f e companhia não imprimiriam nada
if grep -nE "%[-+ #0-9.]*[fFeEgG]|toFloat|atof|strtod|scanf" ../smalltv-pomodoro/*.cpp ../smalltv-pomodoro/modulos/*.cpp ../smalltv-pomodoro/*.h; then
  echo "use números inteiros ou String(x, casas) em vez de float no printf/scanf"; exit 1
fi
GFX="${GFX:-$HOME/Documents/Arduino/libraries/Adafruit_GFX_Library}"
mkdir -p out
node tint_test.cjs out/tint_test.inc || exit 1
# Windows: executável Windows de 32 bits; Linux (também o build automático do GitHub): Linux de 32 bits, estático
if [ -n "$LOCALAPPDATA" ]; then TARGET=x86-windows-gnu; BIN=sim.exe; else TARGET=x86-linux-musl; BIN=sim; fi
# sem zig (Linux): o g++ de 32 bits (pacote g++-multilib), o mesmo dos testes de GIF e JPEG; com
# _FILE_OFFSET_BITS=64, senão o readdir de 32 bits falha em pastas com inode grande (WSL) e os pacotes ficam de fora
if [ -n "$ZIG" ]; then CXX="$ZIG c++ -target $TARGET"; else CXX="g++ -m32 -D_FILE_OFFSET_BITS=64"; fi
$CXX -O1 -std=c++17 -DARDUINO=100 -DARDUINOJSON_ENABLE_ARDUINO_STRING=0 -DARDUINOJSON_ENABLE_ARDUINO_STREAM=0 -DARDUINOJSON_ENABLE_ARDUINO_PRINT=0 -DARDUINOJSON_ENABLE_PROGMEM=0 -Ishim -I"$GFX" -I"${JSON:-$HOME/Documents/Arduino/libraries/ArduinoJson/src}" sim.cpp ../smalltv-pomodoro/ui.cpp ../smalltv-pomodoro/face_*.cpp ../smalltv-pomodoro/facepack.cpp ../smalltv-pomodoro/fonts.cpp ../smalltv-pomodoro/src/qrcodegen/qrcodegen.cpp "$GFX/Adafruit_GFX.cpp" -o "$BIN" || exit 1
./"$BIN"; r=$?
node png.js > /dev/null
node transicao_lcd.js
node faces.js
node catalog.js
$CXX -O1 -std=c++17 -w -Ishim brilho_test.cpp -o "out/brilho_$BIN" && "./out/brilho_$BIN" || exit 1
node painel_test.cjs
node ponte_test.cjs || exit 1
node fuso_test.cjs || exit 1
node ../web/build.js
exit $r
