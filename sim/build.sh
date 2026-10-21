#!/bin/sh
# Gera assets.h, compila e roda o simulador; gera out/*.png, out/folha.png e out/assets.png.
# Requer: zig (https://ziglang.org), node e a biblioteca Adafruit GFX instalada pelo arduino-cli.
cd "$(dirname "$0")"
ZIG=$(command -v zig)
if [ -z "$ZIG" ] && [ -n "$LOCALAPPDATA" ]; then  # Windows: zig instalado pelo winget fora do PATH do shell
  ZIG=$(ls "$(cygpath -u "$LOCALAPPDATA")"/Microsoft/WinGet/Packages/zig.zig*/zig-*/zig.exe 2>/dev/null | head -1)
fi
[ -z "$ZIG" ] && { echo "zig não encontrado"; exit 1; }
node ../assets/gerar.js || exit 1  # assets/*.txt -> smalltv-pomodoro/assets.h
GFX="${GFX:-$HOME/Documents/Arduino/libraries/Adafruit_GFX_Library}"
mkdir -p out
"$ZIG" c++ -target x86-windows-gnu -O1 -std=c++17 -DARDUINO=100 -DARDUINOJSON_ENABLE_ARDUINO_STRING=0 -DARDUINOJSON_ENABLE_ARDUINO_STREAM=0 -DARDUINOJSON_ENABLE_ARDUINO_PRINT=0 -DARDUINOJSON_ENABLE_PROGMEM=0 -Ishim -I"$GFX" -I"${JSON:-$HOME/Documents/Arduino/libraries/ArduinoJson/src}" sim.cpp ../smalltv-pomodoro/ui.cpp ../smalltv-pomodoro/face_*.cpp ../smalltv-pomodoro/facepack.cpp ../smalltv-pomodoro/fonts.cpp "$GFX/Adafruit_GFX.cpp" -o sim.exe || exit 1
./sim.exe; r=$?
node png.js > /dev/null
node faces.js
node catalog.js
node ../web/build.js
exit $r
