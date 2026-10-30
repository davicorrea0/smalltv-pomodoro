#!/bin/sh
# Gera assets.h, compila e roda o simulador; gera out/*.png, out/folha.png e out/assets.png.
# Requer: zig (https://ziglang.org), node e a biblioteca Adafruit GFX instalada pelo arduino-cli.
cd "$(dirname "$0")"
ZIG=$(command -v zig)
if [ -z "$ZIG" ] && [ -n "$LOCALAPPDATA" ]; then  # Windows: zig instalado pelo winget fora do PATH do shell
  ZIG=$(ls "$(cygpath -u "$LOCALAPPDATA")"/Microsoft/WinGet/Packages/zig.zig*/zig-*/zig.exe 2>/dev/null | head -1)
fi
[ -z "$ZIG" ] && ! command -v g++ >/dev/null && { echo "zig (ou g++ com multilib) não encontrado"; exit 1; }
node ../assets/gerar.js || exit 1  # assets/*.txt -> smalltv-pomodoro/assets.h
# o firmware é compilado sem printf/scanf de float (compilar.sh): %f e companhia não imprimiriam nada
if grep -nE "%[-+ #0-9.]*[fFeEgG]|toFloat|atof|strtod|scanf" ../smalltv-pomodoro/*.cpp ../smalltv-pomodoro/*.h; then
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
$CXX -O1 -std=c++17 -DARDUINO=100 -DARDUINOJSON_ENABLE_ARDUINO_STRING=0 -DARDUINOJSON_ENABLE_ARDUINO_STREAM=0 -DARDUINOJSON_ENABLE_ARDUINO_PRINT=0 -DARDUINOJSON_ENABLE_PROGMEM=0 -Ishim -I"$GFX" -I"${JSON:-$HOME/Documents/Arduino/libraries/ArduinoJson/src}" sim.cpp ../smalltv-pomodoro/ui.cpp ../smalltv-pomodoro/face_*.cpp ../smalltv-pomodoro/facepack.cpp ../smalltv-pomodoro/fonts.cpp "$GFX/Adafruit_GFX.cpp" -o "$BIN" || exit 1
./"$BIN"; r=$?
node png.js > /dev/null
node transicao_lcd.js
node faces.js
node catalog.js
node painel_test.cjs
node ../web/build.js
exit $r
