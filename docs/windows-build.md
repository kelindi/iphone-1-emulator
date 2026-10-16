# Building on Windows

Status: 2026-10-15. On Windows 11, the native tools, all native core tests and
the WebAssembly bundle build with portable toolchains. Nothing is installed
system-wide.

## Toolchain

Unpack these into one folder. By default it is `../toolchain` next to the
repository; set `TOOLCHAIN` to use another path.

| Folder | Package | Source |
| --- | --- | --- |
| `cmake/` | CMake 4.4.4, `cmake-4.4.4-windows-x86_64.zip` | github.com/Kitware/CMake releases |
| `ninja/` | Ninja 1.13.2, `ninja-win.zip` | github.com/ninja-build/ninja releases |
| `llvm/` | llvm-mingw 20261006, UCRT x86_64 (Clang 23) | github.com/mstorsjo/llvm-mingw releases |
| `emsdk/` | `git clone https://github.com/emscripten-core/emsdk`, then `emsdk.bat install 4.0.16` and `emsdk.bat activate 4.0.16` | CI's Emscripten version |

Node.js and Python come from the host. The firmware tools need
`pip install -r tools/firmware/requirements.txt`, preferably in a virtual
environment.

## Build

```sh
source tools/windows/env.sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build --parallel 8
ctest --test-dir build --output-on-failure -E test_guest_

npm ci --ignore-scripts
emcmake.bat cmake -S . -B web/build -G Ninja -DIPHONE1_WEB=ON -DCMAKE_BUILD_TYPE=Release
cmake --build web/build --parallel 8
```

## Known differences

- Clone with `core.autocrlf=false`. CRLF working files change the hashes
  that `tools/build-offline.py` writes into `web/service-worker.js`.
- `emu.wasm` and `jit/seed.wasm` are byte-identical to CI's. `emu.js` comes
  out with two CRLF line endings from Emscripten's Windows glue; convert it to
  LF before committing `web/dist/`.
- Use `emcmake.bat` and `emcc.bat`. The extensionless scripts receive
  `/c/...` paths, which Windows Python cannot open.
- `web/smoke.mjs` turns its module URL into a path with `URL.pathname`, which
  is `/C:/...` on Windows. `fileURLToPath` is needed for it to run here.
- The `test_guest_*` Python tests pass their cases but fail in teardown,
  because Windows cannot delete a loaded DLL from the temporary directory.

## Firmware helpers without a native build

Two Python tools cover the native helpers for machines without a C compiler:

- `tools/firmware/img3_decrypt.py IN OUT IV KEY` decrypts an IMG3 DATA tag
  with a published key, including the final partial block.
- `tools/firmware/unlzss.py IN OUT` is a port of `tools/unlzss.c`.
