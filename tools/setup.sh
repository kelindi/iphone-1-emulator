#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
if command -v dnf >/dev/null 2>&1; then
  sudo dnf install -y gcc gcc-c++ cmake ninja-build python3.11 libatomic
fi
for dependency in git cmake node npm; do
  if ! command -v "$dependency" >/dev/null 2>&1; then
    echo "Install $dependency and run this setup again." >&2
    exit 1
  fi
done
npm ci
if ! command -v emcmake >/dev/null 2>&1; then
  mkdir -p .context
  if [[ ! -f .context/emsdk/emsdk.py ]]; then
    git clone --depth 1 https://github.com/emscripten-core/emsdk.git .context/emsdk
  fi
  sdk_python="$(command -v python3.11 || command -v python3)"
  "$sdk_python" .context/emsdk/emsdk.py install 4.0.16
  "$sdk_python" .context/emsdk/emsdk.py activate 4.0.16
fi
bash tools/build-web.sh
