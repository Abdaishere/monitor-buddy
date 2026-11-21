#!/bin/sh
# Gets the libraries the simulator compiles the firmware against, at the
# versions platformio.ini resolves to, into tools/sim/.deps/.
#
# With a PlatformIO build of the firmware next door (.pio/libdeps/esp32-c6, or
# the folder in MBSIM_LIBDEPS) the libraries are linked from there; otherwise
# they come from GitHub. mbedtls (the chip's crypto, built here for the PC)
# always comes from GitHub.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
deps="$here/.deps"
mkdir -p "$deps"
cd "$deps"

GFX=1.6.9
JSON=7.4.3
GIF=c2478eca7aa2f3b7a09bdc583e5a4a43d03ac0d9   # 2.2.3: the registry has it, GitHub has no tag for it
U8G2=2.36.18
MBEDTLS=3.6.4

pio="${MBSIM_LIBDEPS:-$here/../../.pio/libdeps/esp32-c6}"

get() {  # get NAME URL: unpack a GitHub tarball into NAME/
  [ -d "$1" ] && return 0
  echo "fetch_deps: $1"
  mkdir "$1.tmp"
  curl -fsSL "$2" | tar xz -C "$1.tmp" --strip-components=1
  mv "$1.tmp" "$1"
}

if [ -d "$pio/GFX Library for Arduino/src" ] && [ -d "$pio/ArduinoJson/src" ] && [ -d "$pio/AnimatedGIF/src" ] && [ -f "$pio/U8g2/src/clib/u8g2_fonts.c" ]; then
  [ -e gfx ] || ln -s "$pio/GFX Library for Arduino" gfx
  [ -e ArduinoJson ] || ln -s "$pio/ArduinoJson" ArduinoJson
  [ -e AnimatedGIF ] || ln -s "$pio/AnimatedGIF" AnimatedGIF
  [ -e u8g2_fonts.c ] || ln -s "$pio/U8g2/src/clib/u8g2_fonts.c" u8g2_fonts.c
else
  get gfx "https://github.com/moononournation/Arduino_GFX/archive/refs/tags/v$GFX.tar.gz"
  get ArduinoJson "https://github.com/bblanchon/ArduinoJson/archive/refs/tags/v$JSON.tar.gz"
  get AnimatedGIF "https://github.com/bitbank2/AnimatedGIF/archive/$GIF.tar.gz"
  [ -f u8g2_fonts.c ] || { echo "fetch_deps: u8g2 fonts"; curl -fsSL -o u8g2_fonts.c.tmp "https://raw.githubusercontent.com/olikraus/U8g2_Arduino/$U8G2/src/clib/u8g2_fonts.c" && mv u8g2_fonts.c.tmp u8g2_fonts.c; }
fi

if [ ! -f "mbedtls-$MBEDTLS/build/library/libmbedcrypto.a" ]; then
  if [ ! -d "mbedtls-$MBEDTLS" ]; then
    echo "fetch_deps: mbedtls $MBEDTLS"
    curl -fsSL "https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-$MBEDTLS/mbedtls-$MBEDTLS.tar.bz2" | tar xj
  fi
  cmake -S "mbedtls-$MBEDTLS" -B "mbedtls-$MBEDTLS/build" -DENABLE_TESTING=OFF -DENABLE_PROGRAMS=OFF -DCMAKE_BUILD_TYPE=Release >/dev/null
  cmake --build "mbedtls-$MBEDTLS/build" --target mbedcrypto -j"${MBSIM_JOBS:-2}" >/dev/null
fi
[ -e mbedtls ] || ln -s "mbedtls-$MBEDTLS" mbedtls
echo "fetch_deps: ready in $deps"
