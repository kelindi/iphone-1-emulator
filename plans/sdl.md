# Plan: SDL2 desktop frontend

## Goal
Add an interactive native desktop window so a developer can boot (or `--load` the home-screen snapshot) and use the phone with mouse and keyboard, instead of relying on batch scripts and PPM screenshots.

## Requirements
- **New tool** `tools/iphone1_sdl.c`; prefer sharing argument parsing via a small `tools/harness_common.c`/`harness_common.h` rather than duplicating options from `tools/iphone1.c`.
- **Build** only when SDL2 is available (`find_package(SDL2)` / `pkg-config sdl2`, CMake option `IPHONE1_SDL` default ON if found). CI must still pass if SDL is not installed.
- **Window** 320×480 guest framebuffer rendered at integer scale (default 2×, `--scale N`). Reuse the framebuffer accessor used for PPM screenshots and `web/emu_api.c`; do not re-implement pixel conversion. Show black when display off.
- **Input mapping**
  - Left mouse down/move/up → touch down/move/up at guest coords (divide by scale, clamp).
  - `Escape`/`H` = Home press, `P` = Hold/power, `Up`/`Down` = volume, `R` = ringer switch, `Q` = quit.
  - `F5` save snapshot to path from `--save FILE`; `F9` reload snapshot; `F12` PNG screenshot; `Space` pause/resume.
  - Button presses follow the 100 ms guest-time hold/release convention used in the browser build.
- **Run loop** execute the guest in 2–5 ms host slices, pump SDL events, present. Pace to real time when the emulator is faster than real (guest cycles @ 412 MHz vs host clock); run flat out otherwise. Show instr/s, guest/real-time ratio, and paused state in the window title.
- **--serial -** mirror serial log to stdout from `tools/iphone1_sdl`.
- No SDL code in `core/`; additions are host-only in `tools/`.

## Verification
Start from `fw/home_ship.snap` and perform the scripted demo sequence manually:
1. Click Dismiss on the Repair Needed alert (~160, 315 in guest coords).
2. Click Settings (~45, 370).
3. Press Escape to return home.
4. Click Safari (~45, 450, dock).
5. Press Escape to return home.
6. Open Clock → Stopwatch and start it; show it running.

Run under `xvfb-run` if no X display is available (`apt-get install xvfb`) and use `F12` to save PNG screenshots for the PR.

## Files to touch
- New `tools/iphone1_sdl.c`, optionally `tools/harness_common.*` if option parsing is factored out.
- `tools/CMakeLists.txt` — SDL2 detection and `IPHONE1_SDL` option.
- `.github/workflows/ci.yml` — install `libsdl2-dev` or skip the SDL build if not installed.
- `README.md` — add a "Desktop window" section.
- `docs/iphone1_1_os1_port.md` — mention the SDL frontend.

## Correctness gates
- `cmake --build build` succeeds with or without SDL installed.
- `ctest --test-dir build` green.
- `git diff --check` clean.
- WASM build unaffected.
- Screenshots from `xvfb-run` showing the demo sequence.

## Deliverable
A PR from a branch `sdl` targeting `main`.
