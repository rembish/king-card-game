#!/bin/sh
# Builds what the Pages project serves: the web game into public/, the core into lib/core.wasm.
# Needs Emscripten (source emsdk_env.sh first).
set -e
cd "$(dirname "$0")/.."
emcmake cmake -S . -B build-web -DCMAKE_BUILD_TYPE=Release > /dev/null
cmake --build build-web -j --target king
emcc -O2 -Icore core/kg_core.c core/kg_replay.c core/kg_wasm.c -sSTANDALONE_WASM --no-entry \
    -o cloudflare/lib/core.wasm
rm -rf cloudflare/public
mkdir -p cloudflare/public
cp build-web/king.html cloudflare/public/index.html
cp build-web/king.js build-web/king.wasm cloudflare/public/
cp LICENSE cloudflare/public/LICENSE.txt
