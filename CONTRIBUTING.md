# Contributing

Thanks for helping. Read [`plans/README.md`](plans/README.md) for the project
overview and current status before you start.

## Ground rules

- Run Apple's genuine iPhone OS 1.0 (`1A543a`) software unmodified on the
  emulated `iPhone1,1` / `M68AP`. Do not substitute other devices, later iOS
  builds, or recreated SpringBoard UI.
- Keep `core/` portable C11 with no host JIT, so the WebAssembly build keeps
  working. Host UI code belongs in `tools/` or `web/`.
- Keep shared S5L8900 SoC code in `core/src/soc/` and board wiring in
  `core/src/board/`.
- Do not commit Apple firmware, IPSWs, decrypted images, or new snapshots.
- Keep generated screenshots, recordings, logs, and reports in the ignored
  `.context/` directory.

## Build and test

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

For browser changes, follow the build and test steps in the
[README](README.md#run-the-browser-locally).

## Pull requests

- Keep each pull request focused on one change.
- Write commit subjects in the imperative, optionally prefixed with the area,
  for example `soc: model SPI2 and the Z1 multitouch controller`.
- Describe how you verified the change.
- Record significant implementation and verification updates in
  `plans/README.md` and the relevant engineering log.

By contributing, you agree that your contributions are licensed under the
GNU General Public License, version 2 or (at your option) any later version ([`LICENSE`](LICENSE)).
