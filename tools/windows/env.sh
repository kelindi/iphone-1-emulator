# Portable Windows toolchain for this repository. Use from Git Bash:
#   source tools/windows/env.sh
# TOOLCHAIN defaults to ../toolchain beside the repository; see
# docs/windows-build.md for what goes in it. Nothing is installed in Windows.
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
T="${TOOLCHAIN:-$REPO/../toolchain}"
T="$(cd "$T" && pwd)"                 # /c/... for PATH
TW="$(cd "$T" && pwd -W)"             # C:/... for Windows programs
EMSDK_NODE_DIR="$(ls -d "$T"/emsdk/node/*/ 2>/dev/null | head -1)"
EMSDK_PYTHON_DIR="$(ls -d "$T"/emsdk/python/*/ 2>/dev/null | head -1)"
export EMSDK="$TW/emsdk"
export EM_CONFIG="$TW/emsdk/.emscripten"
[ -n "$EMSDK_NODE_DIR" ] && export EMSDK_NODE="$(cd "$EMSDK_NODE_DIR" && pwd -W)/node.exe"
[ -n "$EMSDK_PYTHON_DIR" ] && export EMSDK_PYTHON="$(cd "$EMSDK_PYTHON_DIR" && pwd -W)/python.exe"
export PATH="$T/cmake/bin:$T/ninja:$T/llvm/bin:$T/emsdk:$T/emsdk/upstream/emscripten:$PATH"
# Git Bash rewrites absolute guest paths such as /var/root; keep them verbatim.
export MSYS_NO_PATHCONV=1
