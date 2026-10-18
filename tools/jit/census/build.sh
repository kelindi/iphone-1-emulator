#!/usr/bin/env bash
# Build the census runtime next to the browser build (run tools/build-web.sh first).
set -euo pipefail
cd "$(dirname "$0")/../../.."
out=${CENSUS_OUT:-.context/census}
mkdir -p "$out"
cmake --build web/build --target emucore --parallel 4
emcc -O3 -std=c11 -DS5LBOX_WASM_JIT=1 -Icore/include -Iweb web/emu_api.c \
  tools/jit/census/census-runtime.c web/build/core/libemucore.a -lm \
  -sMODULARIZE=1 -sEXPORT_NAME=createCensus -sENVIRONMENT=node -sALLOW_MEMORY_GROWTH=1 \
  -sINITIAL_MEMORY=536870912 -sSTACK_SIZE=4194304 \
  '-sEXPORTED_FUNCTIONS=["_malloc","_free"]' '-sEXPORTED_RUNTIME_METHODS=["HEAPU8","HEAPU32","HEAPF64"]' \
  -o "$out/census.cjs"
echo "$out/census.cjs"
