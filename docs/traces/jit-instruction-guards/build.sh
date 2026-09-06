#!/usr/bin/env bash
set -euo pipefail
.context/emsdk/upstream/emscripten/emcc -O3 -DNDEBUG -DS5LBOX_WASM_JIT -Icore/include -Iweb .context/jit-instruction-guards/oracle.c web/jit-runtime.c web/build/core/libemucore.a -lm -sINITIAL_MEMORY=536870912 -sALLOW_MEMORY_GROWTH=1 -sALLOW_TABLE_GROWTH=1 -sMODULARIZE=1 -sEXPORT_NAME=createOracle -sENVIRONMENT=node '-sEXPORTED_RUNTIME_METHODS=["addFunction","removeFunction","HEAPU8","HEAPU32"]' '-sEXPORTED_FUNCTIONS=["_malloc","_free"]' -o .context/jit-instruction-guards/oracle.cjs
