#!/bin/bash
# Downloads the prebuilt PSL1GHT toolchain and rebuilds the host tools that
# need OpenSSL 3 / Python 3 (the prebuilt ones link to OpenSSL 1.1 / Python 2).
# Usage: tools/ci/setup-toolchain.sh <install-dir>
set -euo pipefail
DEST="${1:-$PWD/ps3dev-root}"
mkdir -p "$DEST"
cd "$DEST"
if [ ! -d ps3dev ]; then
  curl -sSL https://github.com/bucanero/ps3toolchain/releases/download/ubuntu-latest-fad3b5fb/ps3dev-ubuntu-latest-2020-08-31.tar.gz | tar xz
fi
export PS3DEV="$DEST/ps3dev"
if [ ! -d PSL1GHT-src ]; then
  git clone --depth 1 https://github.com/ps3dev/PSL1GHT PSL1GHT-src
fi
make -C PSL1GHT-src/tools/geohot >/dev/null
cp PSL1GHT-src/tools/geohot/make_self PSL1GHT-src/tools/geohot/make_self_npdrm \
   PSL1GHT-src/tools/geohot/package_finalize "$PS3DEV/bin/"
make -C PSL1GHT-src/tools/ps3py >/dev/null
cp PSL1GHT-src/tools/ps3py/pkg.py PSL1GHT-src/tools/ps3py/sfo.py PSL1GHT-src/tools/ps3py/fself.py \
   PSL1GHT-src/tools/ps3py/Struct.py PSL1GHT-src/tools/ps3py/pkgcrypt*.so "$PS3DEV/bin/"
sed -i '1s|.*|#!/usr/bin/env python3|' "$PS3DEV/bin/pkg.py" "$PS3DEV/bin/sfo.py" "$PS3DEV/bin/fself.py"
echo "toolchain ready: $PS3DEV"
