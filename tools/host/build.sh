#!/bin/sh
# Builds the PC preview of the UI (same code as the PS3 app, software renderer).
set -e
cd "$(dirname "$0")/../.."
mkdir -p build-host
gcc -O2 -g -Wall -Wno-unused-parameter -Wno-misleading-indentation -Wno-format-truncation \
  $(pkg-config --cflags freetype2) -Isource \
  source/main.c source/ui.c source/gfx.c source/font.c source/images.c source/library.c \
  source/config.c source/plat_host.c \
  -o build-host/snes9x-ps5-host $(pkg-config --libs freetype2) -lm -lpthread
echo "built build-host/snes9x-ps5-host"
