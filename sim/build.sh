#!/bin/sh
# Compila e roda o simulador; gera out/*.png e out/folha.png.
# Requer: zig (https://ziglang.org), node e a biblioteca Adafruit GFX instalada pelo arduino-cli.
cd "$(dirname "$0")"
ZIG=$(command -v zig)
if [ -z "$ZIG" ] && [ -n "$LOCALAPPDATA" ]; then  # Windows: zig instalado pelo winget fora do PATH do shell
  ZIG=$(ls "$(cygpath -u "$LOCALAPPDATA")"/Microsoft/WinGet/Packages/zig.zig*/zig-*/zig.exe 2>/dev/null | head -1)
fi
[ -z "$ZIG" ] && { echo "zig não encontrado"; exit 1; }
GFX="${GFX:-$HOME/Documents/Arduino/libraries/Adafruit_GFX_Library}"
mkdir -p out
"$ZIG" c++ -target x86-windows-gnu -O1 -std=c++17 -DARDUINO=100 -Ishim -I"$GFX" sim.cpp "$GFX/Adafruit_GFX.cpp" -o sim.exe || exit 1
./sim.exe; r=$?
node png.js > /dev/null
exit $r
