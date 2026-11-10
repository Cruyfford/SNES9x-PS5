#!/bin/sh
# Builds the snes9x2010 libretro core (with the libretro-common pieces it
# needs) as a static library for this front end.
#   tools/ci/build-core.sh ps3    -> build-core/ps3/lib/libsnes9x2010.a
#   tools/ci/build-core.sh host   -> build-core/host/lib/libsnes9x2010.a
set -e
cd "$(dirname "$0")/../.."
CORE_REPO=https://github.com/libretro/snes9x2010
CORE_COMMIT=fe690dd321fa5a46b5234a2bde089d2518c62b0e
TARGET="${1:-ps3}"
mkdir -p build-core
if [ ! -d build-core/src ]; then
  git clone -q "$CORE_REPO" build-core/src
  git -C build-core/src checkout -q "$CORE_COMMIT"
fi
mkdir -p "build-core/$TARGET/lib"
SRC=build-core/src
make -C $SRC -f Makefile.libretro clean >/dev/null 2>&1 || true
find $SRC -name '*.o' -delete
# STATIC_LINKING=0 keeps libretro-common in the core (we are not RetroArch).
# The core's own final link step is not needed and may fail; only the
# object files are used.
if [ "$TARGET" = ps3 ]; then
  make -C $SRC -f Makefile.libretro platform=psl1ght STATIC_LINKING=0 -j"$(nproc)" -k >/dev/null 2>&1 || true
  AR=ppu-ar
else
  make -C $SRC -f Makefile.libretro platform=unix STATIC_LINKING=0 -j"$(nproc)" -k >/dev/null 2>&1 || true
  AR=ar
fi
rm -f "build-core/$TARGET/lib/libsnes9x2010.a"
OBJS=$(find $SRC -name '*.o' | sort)
[ -n "$OBJS" ] || { echo "core build failed"; exit 1; }
$AR rcs "build-core/$TARGET/lib/libsnes9x2010.a" $OBJS
echo "core built for $TARGET ($(echo "$OBJS" | wc -l) objects)"
