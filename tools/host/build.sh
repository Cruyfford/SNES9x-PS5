#!/bin/sh
# Builds the PC preview of the app (same code as the PS3 build, software renderer).
# Needs the snes9x2010 core built for the host:  tools/ci/build-core.sh host
set -e
cd "$(dirname "$0")/../.."
mkdir -p build-host
CORE=build-core/host/lib/libsnes9x2010.a
[ -f "$CORE" ] || tools/ci/build-core.sh host
gcc -O2 -g -Wall -Wno-unused-parameter -Wno-misleading-indentation -Wno-format-truncation \
  $(pkg-config --cflags freetype2) -Isource \
  source/main.c source/ui.c source/gfx.c source/font.c source/images.c source/library.c \
  source/config.c source/emu.c source/video.c source/stbiw.c source/plat_host.c \
  -o build-host/snes9x-ps5-host "$CORE" $(pkg-config --libs freetype2) -ltheoradec -logg -lm -lpthread
echo "built build-host/snes9x-ps5-host"
