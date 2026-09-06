## Tweet idea

The original iPhone emulator has reached interactive iPhone OS 1.0: genuine SpringBoard, touch, Settings, Safari, and Home-button navigation in a browser. Next up: faster execution and filling in missing hardware, including USB, storage, and Wi-Fi.

## What you're seeing

Existing recording: [browser_home_settings_safari_cpi1.mp4](videos/browser_home_settings_safari_cpi1.mp4), 0:00–0:34, with its [thumbnail](thumbnails/browser_home_settings_safari_cpi1.jpg).

This reuses the previously documented browser demonstration of snapshot startup and app navigation. It is not a new runtime verification or evidence of networking inside Safari.

## ELI5 technical explanation

Status review on 2026-09-05, against local commit `b0a7399` (the merged initial OS 1.0 PR):

- The portable C core imitates the original phone's ARM CPU and peripherals. Apple's actual kernel, SpringBoard, and apps supply the phone UI; the browser displays the emulated LCD framebuffer and forwards input.
- The emulator enters XNU directly instead of running the complete SecureROM/LLB/iBoot chain. Apple executable binaries remain unmodified, while the loaded device tree and a filesystem work image supply boot workarounds, activation data, and preferences.
- Reverse-engineering timer, interrupt, touch, and graphics behavior made the home screen and app navigation possible. Some app-close rendering remains incomplete: the engineering log records a black interval before the home screen returns because the 3D blit path is not rendered.
- Recorded native throughput improved from 35.383 to 41.633 million instructions/second through compiler optimization and privileged tick batching. These are prior measurements, not measurements from this workspace. CPU-saturated execution is still roughly one tenth of the modeled 412 MHz clock at CPI 1; idle guest time can advance faster through WFI.
- USB remains unmatched to avoid a driver-registration crash. The filesystem uses a ramdisk instead of working NAND partitions. Wi-Fi/host networking and an SDL desktop frontend are planned, with no completed implementations in this checkout. The missing baseband explains the expected “Repair Needed” alert.
- The four task plans describe future work and reference the old PR branch. Their existence, and the older parallel-session update, do not establish that those changes landed.

Documentation has drifted: the top-level README says input is unavailable and firmware is absent, but input is implemented and `web/firmware/` contains tracked assets. The actual browser manifest is `web/manifest.json`, and generated browser outputs go to `web/dist/emu.js` and `web/dist/emu.wasm`.

This review inspected source, history, plans, CI definitions, and existing recording references. It did not rerun the emulator or tests: this fresh workspace has no C compiler, CMake, Ninja, or Emscripten installed. CI defines 22 native test executables; its WASM smoke test checks initialization error handling, not an OS boot or app navigation.

## Why it matters

The project has passed the boot-and-interact milestone. The next phase is improving responsiveness and hardware completeness while preserving real Apple software behavior. Distinguishing working features, documented experiments, and planned work gives the next task a reliable starting point.
