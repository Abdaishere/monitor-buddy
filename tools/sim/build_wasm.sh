#!/bin/sh
# Builds the simulator for a browser: ../app/sim/mbsim.js and mbsim.wasm, the
# live preview of the app (tools/buddy_app.py). The same sources as `make`:
# src/main.cpp, lib/ and host/, compiled by Emscripten instead of g++.
#
#   ./build_wasm.sh          needs emcc on the PATH, or Docker (emscripten/emsdk)
#
# The two files are committed, so the app needs no compiler. Run this again
# after a change to the firmware's drawing code, then `python3 -m pytest
# tests_web/test_preview.py` (the preview's pixels against the golden pictures).
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
EMSDK_IMAGE=${EMSDK_IMAGE:-emscripten/emsdk:4.0.10}

[ -d "$here/.deps/gfx" ] || "$here/fetch_deps.sh"
deps=$(cd "$here/.deps" && pwd -P)

if ! command -v emcc >/dev/null 2>&1; then
  command -v docker >/dev/null 2>&1 || { echo "build_wasm: needs emcc (Emscripten) or docker" >&2; exit 1; }
  # .deps may link to a PlatformIO build's libraries: those folders are mounted too
  set -- -v "$root:$root" -v "$deps:$deps"
  for entry in "$deps"/*; do
    real=$(readlink -f "$entry")
    case "$real" in "$deps"/*) ;; *) [ -d "$real" ] && set -- "$@" -v "$real:$real" || set -- "$@" -v "$(dirname "$real"):$(dirname "$real")" ;; esac
  done
  exec docker run --rm -u "$(id -u):$(id -g)" -e HOME=/tmp -e EM_CACHE="$here/.build-wasm/emcache" \
    "$@" -w "$here" "$EMSDK_IMAGE" ./build_wasm.sh
fi

build="$here/.build-wasm"
out="$root/tools/app/sim"
mkdir -p "$build/obj" "$out"

# mbedtls (the chip's crypto) for WebAssembly
mbed=$(cd "$deps/mbedtls" && pwd -P)
if [ ! -f "$mbed/build-wasm/library/libmbedcrypto.a" ]; then
  echo "build_wasm: mbedtls"
  emcmake cmake -S "$mbed" -B "$mbed/build-wasm" -DENABLE_TESTING=OFF -DENABLE_PROGRAMS=OFF -DCMAKE_BUILD_TYPE=Release >/dev/null
  cmake --build "$mbed/build-wasm" --target mbedcrypto -j"${MBSIM_JOBS:-4}" >/dev/null
fi

python3 "$here/gen_fonts.py" "$root/src/main.cpp" "$deps/u8g2_fonts.c" "$build/u8g2_sim_fonts.h" "$build/u8g2_sim_fonts.c"

gfx="$deps/gfx/src"
inc="-I $here/host -I $build -I $here/config -I $root/src -I $gfx -I $deps/ArduinoJson/src -I $deps/AnimatedGIF/src -I $mbed/include"
for d in "$root"/lib/*; do inc="$inc -I $d"; done
defs='-DMBSIM=1 -D__LINUX__ -DARDUINOJSON_ENABLE_ARDUINO_STRING=1 -DARDUINOJSON_ENABLE_ARDUINO_STREAM=1 -DARDUINOJSON_ENABLE_ARDUINO_PRINT=1 -DARDUINOJSON_ENABLE_PROGMEM=0 -Wno-builtin-macro-redefined'
opt="-O2 -fno-strict-aliasing -w"

objs=""
pids=""
compile() {  # compile SRC: one object, only when the source is newer
  o="$build/obj/$(basename "$1" | sed 's/\.[a-z]*$//').o"
  objs="$objs $o"
  if [ ! -f "$o" ] || [ "$1" -nt "$o" ] || [ -n "${MBSIM_REBUILD:-}" ]; then
    case "$1" in
      *.c) emcc $opt -I "$here/host" -I "$build" -c "$1" -o "$o" & ;;
      *)   em++ -std=gnu++17 $opt $inc $defs '-D__DATE__="Jan  5 2026"' '-D__TIME__="08:00:00"' -c "$1" -o "$o" & ;;
    esac
    pids="$pids $!"
  fi
}
# a header changed: everything again (there are few files)
newest=$(ls -t "$here"/host/*.h "$here"/config/*.h "$root"/src/*.h "$root"/lib/*/*.h | head -1)
[ -f "$build/stamp" ] && [ "$build/stamp" -nt "$newest" ] || MBSIM_REBUILD=1
for f in "$root/src/main.cpp" "$root"/lib/*/*.cpp "$here"/host/*.cpp \
         "$gfx/Arduino_GFX.cpp" "$gfx/Arduino_G.cpp" "$gfx/Arduino_DataBus.cpp" "$gfx/Arduino_TFT.cpp" \
         "$gfx/canvas/Arduino_Canvas.cpp" "$gfx/display/Arduino_ST7789.cpp" "$deps/AnimatedGIF/src/AnimatedGIF.cpp" \
         "$build/u8g2_sim_fonts.c"; do
  compile "$f"
done
for p in $pids; do wait "$p"; done
touch "$build/stamp"

em++ $opt $objs "$mbed/build-wasm/library/libmbedcrypto.a" -o "$out/mbsim.js" \
  -sASYNCIFY -sMODULARIZE -sEXPORT_NAME=MBSim -sENVIRONMENT=web,node -sINVOKE_RUN=0 -sEXIT_RUNTIME=0 \
  -sALLOW_MEMORY_GROWTH -sINITIAL_MEMORY=33554432 -sSTACK_SIZE=1048576 \
  -sEXPORTED_FUNCTIONS=_main,_malloc,_free,_mbsim_cmd,_mbsim_reply,_mbsim_frame \
  -sEXPORTED_RUNTIME_METHODS=FS,callMain,ccall,UTF8ToString,HEAPU8,HEAP32
ls -l "$out/mbsim.js" "$out/mbsim.wasm"
