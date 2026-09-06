#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
if command -v emcmake >/dev/null 2>&1; then
  emcmake_command="$(command -v emcmake)"
elif [[ -x .context/emsdk/upstream/emscripten/emcmake ]]; then
  emcmake_command="$PWD/.context/emsdk/upstream/emscripten/emcmake"
else
  echo 'Emscripten is missing. Run bash tools/setup.sh first.' >&2
  exit 1
fi
"$emcmake_command" cmake -S . -B web/build -DIPHONE1_WEB=ON -DCMAKE_BUILD_TYPE=Release
cmake --build web/build --parallel 4
node web/smoke.mjs
