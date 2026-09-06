# Project overview and work log

## What this project is

`iphone-1` is an S5L8900 (original iPhone SoC) emulator whose goal is to boot and interact with the genuine Apple iPhone OS 1.0 build `1A543a` on an emulated `iPhone1,1` / `M68AP`. The emulator derives from the S5LBox research base, but adds a clean M68 machine target, OS 1.0 firmware tooling, and both a native harness and a WebAssembly/browser build.

Fidelity rule: the project runs Apple's real kernel, frameworks, launchd, SpringBoard, and applications without patching Apple binaries. The guest screen is the emulated framebuffer; it is never recreated in HTML/CSS or substituted with later firmware. The surrounding physical case and browser controls use HTML/CSS. Work-image data and boot-time device-tree adjustments are listed below.

Repository: https://github.com/kelindi/iphone-1
Main PR: https://github.com/kelindi/iphone-1/pull/1
Detailed engineering log: `docs/iphone1_1_os1_port.md`

## High-level architecture

```
core/                    portable emulator core, no host UI, C11 only (WASM-friendly)
  src/arm/               ARMv6 interpreter + VFP
  src/mmu.c              ARMv6 short-descriptor MMU + TLB
  src/soc/               shared S5L8900 devices (VIC, GPIO-IC, timers, CLCD, SPI, I2C,
                         I2S, DMA, NOR, NAND, MBX, PMU, UART, USB OTG, buttons, Z1/Z2)
  src/board/             machine definitions: M68 (iPhone1,1) and N82 (iPhone 3G)
  src/boot/              OS 3/iBoot bringup and OS 1.0 direct-XNU bringup
  src/firmware/          8900/IMG2/IMG3 unwrap, LZSS, Mach-O loader, kernel symbols
tools/iphone1.c         native host harness: run loop, tracing, screenshots, input
tools/firmware/         Python IPSW extractors, HFSX tools, work-image builder
docs/iphone1_1_os1_port.md   detailed boot log, MMIO notes, and current blockers
```

The key split is: shared S5L8900 SoC code in `core/src/soc/`, and per-board wiring in `core/src/board/` behind a `s5l_board_t` vtable. No board-specific `if` statements are in the SoC code.

## Status

**Default graphics correction (2026-09-06):** the Safari source-row fix is now in the local web package. Actual previous-runtime saved-phone upgrade passes 30 checks and the final unmarked package passes 21, including ordinary autosave and disconnected restoration. All 30 native suites and 58 JavaScript tests pass. [Milestone 096](../tweets/096_graphics_fix_default_release.md) records the result and retained first audit failure. App launches remain too slow. Following the user's concern about elapsed time, work is narrowing to perceived responsiveness; further audio/network expansion is deferred while existing runs close out.

**Latest speed experiment (2026-09-06):** expanding compiled coverage preserves all four complete guest checkpoints and passes 88 browser screen checks. Warm Settings p95 improves from 3.467 to 3.261 seconds in the pooled comparison, but first-launch results are mixed and reverse-order Home p95 worsens by about 100 ms. The candidate remains isolated; the default CPU is unchanged. [Milestone 088](../tweets/088_mixed_seed_coverage_diagnostic.md) includes the complete result and the disk-cleanup interruption before the final control. An isolated full-web LLVM LTO build preserves the public API, JIT ABI, focused CPU boundary states and all four complete 100M checkpoints in both interpreter and original-seed execution. A quiet fixed-work comparison reduces CPU time by 5.81%, with exact checkpoint matches. The browser comparison is now complete with the failed first control explicitly retained: all 88 screen pairs match, warm Settings median improves only 1.53%, and pooled Home p95 worsens 49 ms. The candidate remains parked; [milestone 092](../tweets/092_web_lto_navigation_diagnostic.md) includes the separately frozen continuation and recording limitation. Production CPU and seed remain unchanged. An exact executed-work counter now passes focused state/count tests and all four complete checkpoints in its single100M trajectory check. Retained counts identify already generated hash/search loops; a separately bracketed warm-browser census remains unrun.

**Existing browser floating-point defect:** the unchanged WASM core and LTO candidate produce identical VFP test results: 595 passed, 12 failed. Missing exception flags and ineffective directed rounding explain the failures, including three incorrect vector refusal/commit checks. [The retained conformance report](../docs/traces/wasm-vfp-conformance/README.md) separates this baseline correctness issue from relative LTO validation. Native test success does not cover these browser semantics; a separate fix remains open.

**Calculator input measured (2026-09-06):** direct canvas delivery resolves the skipped-frame uncertainty in [089](../tweets/089_calculator_input_feedback.md). All 60 measured presses pass independent retained-pixel checks; button feedback p95 is 135.7 ms for 9 and 134.0 ms for C, meeting the 150 ms target for this workload on cloud Chrome. [Milestone 090](../tweets/090_calculator_frame_delivery.md) retains the complete recording, observer overhead and limits. This confirms existing behavior; it is not a production speedup, a physical-paint measurement, or acceptance of every app. App launches remain too slow.

**Original Safari local demo (2026-09-06):** one isolated imported phone passes 20 checks: original keyboard URL entry, rendered local pages, link, form, bookmark, Home/revisit and ordinary autosave/reload. Seven actual guest HTTP exchanges are independently verified, including two fresh requests after restoring the running server. [Milestone 091](../tweets/091_safari_local_demo.md) includes the full recording and exact retained-screen comparisons. This builds on [own program execution](../tweets/086_own_guest_program.md) and [loopback qualification](../tweets/087_guest_loopback_http.md). Default-phone distribution, external networking and Weather delivery remain open; the demo is not yet available in the normal starting phone.

**Default local-demo qualification failed (2026-09-06):** the maintained quiet daemon boots from the actual pristine MBX default image and original Safari renders its landing page and Next. The first form stalls with guest graphics hangs; the final process list also lacks the daemon, whose exit cause remains unobserved. [093](../tweets/093_default_local_demo_qualification_failure.md) retains the complete single attempt and failed-state evidence. Default distribution remains blocked; no retry, software fallback or restart-policy change is hidden.

**Original Photos fixture (2026-09-06):** our own importer writes two image/thumbnail pairs using original filesystem calls, with complete payload verification. Actual Photos recognizes both and renders the first full image, but navigation then leaves stale content and a narrow strip. [Milestone094](../tweets/094_photos_first_workflow_failure.md) records the failed first workflow. Graphics, second-image navigation and persistence remain open; the default phone is unchanged.

**Visible recovery (2026-09-06):** failed downloads and later runtime errors now show recovery actions inside the phone. One real browser session passes 12 checks, including pointer and keyboard retry into the genuine OS. [Milestone 085](../tweets/085_visible_recovery.md) records the test and its injected-error scope. This frontend follow-up changes the shell generation to `6fc3927f86a86269a30dc811`; the accepted core and loader below remain unchanged.

**RTC and seed-loader integration (2026-09-06):** the default local web build now includes complete manual clock updates and Chrome compiled-seed reuse. Both real time edits survive autosave/reload; the actual previous-runtime upgrade passes 30 checks, followed by 21 checks on the exact final bundle, including physically disconnected restore. All 28 native suites, 58 JavaScript tests and the WASM smoke pass. The minimalist UI, R2 manifest and seed bytes are preserved. [Integration evidence](../docs/traces/rtc-loader-release/README.md), [clock recording](../tweets/082_manual_time_autosave.md) and [offline reuse](../tweets/084_offline_compiled_seed_reuse.md) cover the results. This is a local integration, not a public deployment or a full-speed claim.

**Remote firmware delivery (2026-09-06):** all six prepared firmware objects now load from `https://iphone.files.garden/`, with their public bytes matching the local package's checked-in SHA-256 values exactly. The browser permits only that external firmware origin, still rejects unrelated hosts, and verifies every object before use. A real R2-backed navigation run and a six-check offline corruption/recovery run pass. [Milestone 081](../tweets/081_r2_firmware_delivery.md) contains the recording and implementation explanation.

**Save responsiveness updates (2026-09-06):** two verified changes now reduce Save interruptions. Compression runs in a dedicated worker, reducing measured page main-thread CPU from about 6.8 s to 0.3–0.4 s ([milestone 068](../tweets/068_save_compression_worker.md)). Faster RAM page classification reduces the initial Chrome snapshot capture from 225–273 ms to 136–143 ms ([milestone 071](../tweets/071_faster_snapshot_capture.md)). Full backup bytes and all eight Home screens match exactly; production clock, persistence and actual old-runtime offline upgrade checks pass. These are separate comparisons on this VM, not a claim of faster total Save or general app navigation. Full-speed app functionality remains an active goal.

**Implementation resumed 2026-09-05:** follow [the execution log](execution.md) for current verified results and active work toward [the browser product roadmap](web-iphone.md). The historical status below describes the initial OS 1.0 bringup.

The current [16-app audit](app-coverage.md) records actual app behavior and remaining workflow/hardware blockers. Weather's unit selection and city removal now survive three exact Save/reload comparisons ([milestone 074](../tweets/074_weather_controls_persist.md)). Its standalone demo service passes ten local tests, but guest networking and forecast delivery remain open. New milestones, recordings, and explanations are indexed in [tweets/README.md](../tweets/README.md).

**Latest input verification:** a quick first tap now retains the touchscreen's normal held-contact report before release. The rebuilt production phone passes 59 browser lifecycle/update checks and all twelve Calculator workflows, alongside 26 native suites and 34 JavaScript tests. [Milestone 063](../tweets/063_short_tap_contact_report.md) contains the actual recording, preserved test failures and narrow image-comparison limits. This closes a reproduced missed-tap bug; app-launch speed remains below target.

**Current hardware work:** isolated tests now receive the entire original Wi-Fi helper and main-program container, with all 504 original driver acknowledgements and independently verified wire bytes, memory and write coverage. [Milestone 069](../tweets/069_sdio_complete_main_received.md) records the final handoff request. A verified private checkpoint now reproduces the full upload and subsequent 41.9 million instruction driver failure exactly ([milestone 073](../tweets/073_reusable_wifi_checkpoint.md)), allowing the next readiness transition to be investigated without another full boot. The exact helper selects a bounded semantic model; its ARM CPU is not emulated and no network interface is ready yet. Audio now connects the original driver's real 16 KiB input transfer to an experimental decoder and produces its first independently verified 4,096-byte PCM buffer ([milestone 075](../tweets/075_first_guest_audio_buffer.md)). The original guest now consumes every byte of the first buffer and returns ownership through its real driver ([milestone083](../tweets/083_original_audio_buffer_consumed.md)). A later isolated run verifies 53 consecutive PCM buffers read by the guest and 51 ownership returns. The original driver now also saves and suspends a real decoder generation; an observer code-binding check stops before downstream HAL entry is verified ([evidence](../docs/traces/amc-first-consumption/power-suspension/README.md)). Continuous sample timing, downstream transport and sound remain open. See [networking](networking.md), [audio](audio.md) and their milestone evidence. These experimental device models are not part of the production phone yet.

**Browser speed baseline:** the guarded WebAssembly CPU engine and foreground clock correction are enabled. The earlier quiet graphics comparison used the same engine in four alternating software/MBX sessions, with 20 warm observations per action and configuration. Accelerated Home returns improve from **1.27 to 0.92 seconds at p95**; Settings improves from **3.88 to 3.57 seconds**, still above the two-second target. The measured MBX package is now the default for new phones after the continuous sixteen-app rendering gate and 65 browser package checks passed. See [the default promotion and fresh-load recording](../tweets/058_accelerated_default_phone.md). Existing saved phones retain their state and data. These are measurements on this cloud VM, not the user's Mac, and do not establish full speed or animation/input acceptance. See [milestone 055 and its original-speed recording](../tweets/055_mbx_navigation_speed.md), [the graphics investigation](graphics-speed.md), [the CPU investigation](speed-experiments.md), and [the execution log](execution.md).

- **Goal achieved:** the genuine iPhone OS 1.0 home screen renders. SpringBoard displays the original 2007 home screen, the "Repair Needed" alert, and app launch/return animations.
- **Interactive in browser:** a hard-refresh of the WASM site loads multipart firmware, restores a home-screen snapshot, and lets mouse/keyboard interact (tap → Settings, Escape → Home, tap → Safari).
- **Historical interpreter speed:** native ~42 M instructions/s after `-O3`/tick batching. The browser now has a guarded WebAssembly CPU seed; current measured navigation and remaining speed gates are described above.
- **Workarounds:** USB OTG is not matched (PHY registration-order crash), and the root filesystem boots as a single ramdisk (`md0`) instead of NAND partitions.

## What was implemented

### M68 machine target (`core/src/board/m68.c`, `core/src/soc/machine.c`)
- Defined M68 vs N82 differences: NOR base `0x24000000`, Merlot LCD on SPI1 CS0, Z1 touch on SPI2 CS0, WM8758 audio on I2C0 `0x1a`, PCF50635 PMU.
- Wired M68 buttons: `hold`, `menu` (Home), `volup`, `voldown`, `ringerab`.
- Current browser work also attaches the SPI0 LM2512 display-link transport; see [the wake investigation](display-wake.md) for its measured scope and legacy snapshot routing.
- Programmed a valid CHRP NVRAM image for `IODTNVRAM::initNVRAMImage`.

### Firmware extraction and builder tools (`tools/firmware/`)
- `extract_1a543a.py`: IPSW → `kernel_1A543a.macho`, `devicetree_m68ap.bin`, HFSX rootfs image.
- `unwrap8900.py`: parses 8900/IMG2/`complzss` containers from the pre-IMG3 era.
- `rootfs_work.py`: copies the extracted HFSX image and builds a *work image* with a single `/dev/md0` fstab, SpringBoard env vars (`LK_ENABLE_MBX2D/OGL=0`), activation seed, and no-autolock prefs.
- `hfsx_extract.py`, `ppm2png.py`, `ksym.py`, `tsym.py`, `dis.py`, `machomap.py`, etc.

### OS 1.0 XNU boot path (`core/src/boot/bringup_os1.c`)
- Skips SecureROM/LLB/iBoot; constructs the kernel entry state directly from Mach-O, device tree, boot-args, and ramdisk.
- Handles OS 1.0-specific `__HIB` at `0xc0000000`, `__PRELINK` prelinked kext segment, and M68 memory map.
- Patches the device tree for CPU/root frequencies, `/memory`, `/vram`, debug-enabled, and RAMDisk.

### ARM/SoC fixes for OS 1.0
- Timer4 modeled as a true one-shot down-counter with proper re-arm (`COUNTBUF` + `STATE=START|UPDATE`); fixes `hertz_tick` and `IODelay` timing.
- `MSR CPSR` in exception handlers rejects ARM/Thumb bit writes matching ARM1176.
- MBX 2D packet parser handles variable `0x70000000` terminators and constant-u textured sprites.
- NOR/NVRAM layout for M68, Merlot LCD panel ID, PCF50635 registers.

### Z1 multitouch (`core/src/soc/mtz1.c`)
- Reverse-engineered from the genuine `AppleMultitouchSPI` kext: bootloader upload, firmware mode, attention/result reads, and touch-frame reports.
- Emits `0xce` input frames with contact coordinates, density, and device-uptime timestamps.
- Supports browser tap injection and harness `--at STEP tap X Y` / `touch` / `press` commands.

### Browser/WebAssembly build (`web/`, `core/CMakeLists.txt`)
- Emscripten build `web/dist/emu.wasm` using the same portable core.
- The browser loads `web/manifest.json`, reassembles multipart <100 MB firmware chunks, gunzips, and loads snapshot or rootfs. Current worker/frontend additions are tracked in the execution log.
- Canvas rendering, status bar, `?cpi=` knob, mouse-as-touch, Escape=Home, Shift+click=hold.
- GitHub Actions CI builds and deploys to Pages (currently expects Pages source = "GitHub Actions").

### Speed passes
- Native `-O3` + IPO.
- Privileged tick batching in the interpreter, stopping at timebase edges, WFI, MMIO, host input, and host callbacks.
- `--cpi` knob for guest clock ratio; measured and documented that CPI affects guest time, not host throughput.

### Diagnostics in the harness
- `--serial` with instruction-count prefixes, `--trace-devices`, `--pc-log`, `--trace-from`/`--trace-last`, `--exec-pc`, `--swi-log`, `--dump-va`, `--break-pc`, `--break-swi`, `--shot-every` screenshots.
- Snapshot save/load (`--save-at`, `--load`) preserving guest instruction count and device state.

## Workarounds (not Apple binary patches)

1. **USB unmatch retired for new phones** — current-core and preserved baseline revalidation both boot the original USB stack without the historical early crash. The default phone now keeps lockdownd alive, removing the Home connection-retry delay. Old saved phones retain their old boot state; Export then Reset opts into the new phone. Host USB transfer remains unmodeled. See [the measured service recovery](../tweets/036_home_service_recovery.md).
2. **Single ramdisk root** — `/etc/fstab` points to `/dev/md0` instead of NAND `disk0s1`/`disk0s2`. Real fix is a NAND/disk0 model.
3. **Activation state** — the work image includes a seeded `data_ark.plist` and a locally generated, locally signed activation record so `lockdownd` reports `Activated`. No Apple binary is modified.
4. **No-autolock prefs** — seeds `SBAutoLockTime`/`SBAutoDimTime` to `-1` to prevent the screen turning off during slow experiments. This is a user preference, not a binary patch.

## Current blockers / next steps

1. **Speed.** Settings still exceeds the launch target. [Actual first-use Chrome traces](../docs/traces/browser-first-use/README.md) identify substantial compilation work; warm guest execution also remains slow. Runtime continuation across admitted regions passes correctness and improves a fixed CPU replay by about 4.2%, but leaves normal browser navigation p95 essentially unchanged, so it is parked. [Smaller generated functions](../docs/traces/jit-lazy-groups/README.md) also pass correctness and all 24 screen checks but make first-use and warm Settings slower. Neither experiment is in production. An isolated streaming loader proves Chrome can reuse compiled seed code across fresh browser processes ([milestone 076](../tweets/076_compiled_seed_reuse.md)). The controlled return-visit comparison now improves first Settings from 5.112/4.909 to 3.566/3.417 seconds and warm Settings p95 from 3.667 to 3.417 seconds, while Home p95 worsens 49.8 ms ([milestone 080](../tweets/080_faster_returning_settings.md)). All 88 strict screens pass. Real service-worker reuse and exact autosave restoration now pass 71 functional checks with the origin server physically stopped ([milestone 084](../tweets/084_offline_compiled_seed_reuse.md)); the combined actual-runtime upgrade/fallback gate and final unmarked-bundle verification also pass. The loader is now integrated with the RTC fix in the default local build ([integration](../docs/traces/rtc-loader-release/README.md)). Faster RAM page classification is integrated for Save capture; decoder partitioning, cache and seed-layout experiments without repeatable navigation gains remain parked.
2. **USB controller behavior.** The obsolete unmatch workaround is removed from new boots. Model transfers and active-controller reset behavior when host connectivity is added.
3. **NAND partitions.** Model disk0 so the real `/etc/fstab` works and `/private/var` exists.
4. **Networking.** Emulate Marvell 88W8686 SDIO Wi-Fi (or a simpler device) bridged to the host.
5. **SDL desktop window.** Native mouse/keyboard UI beyond the current browser harness.

## Work still in flight / per-task plans

The browser product roadmap is [A usable original iPhone in the browser](web-iphone.md). It defines the clickable device frame, acceptance targets for all 16 apps, browser speed gates, persistence, audio/camera, networking, and compatible services. It takes priority for product sequencing over the older implementation plans below; its features are planned, not completed.

Each parallel work stream has its own plan document under `plans/`:

- [Speed](speed.md) — decoded-block interpreter speed engine
- [USB](usb.md) — retired unmatch workaround; detached service works, host transfers remain
- [Wi-Fi](wifi.md) — Marvell 88W8686 SDIO Wi-Fi + host bridging
- [Legacy app services](legacy-services.md) — original Weather protocol discovery and remaining guest-network acceptance
- [Audio](audio.md) — original WM8758 control protocol verified; PCM transport and audible output remain
- [SDL](sdl.md) — SDL2 desktop frontend, outside the browser product's critical path

## How to run

See `README.md` for full build/run instructions and firmware extraction commands.


## Retained experiment artifacts

Eleven completed browser-test exports, five completed RTC test snapshots and nine historical CPU replay outputs are stored as verified gzip archives with adjacent `.archived.json` receipts to keep browser storage tests within the VM disk budget. [Exact locations and restore helper](../docs/traces/storage-reclamation/README.md). Current firmware, active replay fixtures and saved browser profiles are preserved.
