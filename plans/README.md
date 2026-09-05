# Project overview and work log

## What this project is

`iphone-1` is an S5L8900 (original iPhone SoC) emulator whose goal is to boot and interact with the genuine Apple iPhone OS 1.0 build `1A543a` on an emulated `iPhone1,1` / `M68AP`. The emulator derives from the S5LBox research base, but adds a clean M68 machine target, OS 1.0 firmware tooling, and both a native harness and a WebAssembly/browser build.

Fidelity rule: the project runs Apple's real, unmodified kernel, device tree, filesystem, frameworks, launchd, SpringBoard, and applications. It does not recreate the UI, use HTML/CSS, or substitute later firmware or a different device.

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

- **Goal achieved:** the genuine iPhone OS 1.0 home screen renders. SpringBoard displays the original 2007 home screen, the "Repair Needed" alert, and app launch/return animations.
- **Interactive in browser:** a hard-refresh of the WASM site loads multipart firmware, restores a home-screen snapshot, and lets mouse/keyboard interact (tap → Settings, Escape → Home, tap → Safari).
- **Speed:** native ~42 M instructions/s after `-O3`/tick batching; still ~1/10 of the real 412 MHz, so the next big lever is a decoded-block interpreter or JIT.
- **Workarounds:** USB OTG is not matched (PHY registration-order crash), and the root filesystem boots as a single ramdisk (`md0`) instead of NAND partitions.

## What was implemented

### M68 machine target (`core/src/board/m68.c`, `core/src/soc/machine.c`)
- Defined M68 vs N82 differences: NOR base `0x24000000`, Merlot LCD on SPI1 CS0, Z1 touch on SPI2 CS0, WM8758 audio on I2C0 `0x1a`, PCF50635 PMU.
- Wired M68 buttons: `hold`, `menu` (Home), `volup`, `voldown`, `ringerab`.
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
- Emscripten build `iphone1.wasm` using the same portable core.
- `web/app.js` fetches `firmware/manifest.json`, reassembles multipart <100 MB firmware chunks, gunzips, and loads snapshot or rootfs.
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

1. **USB OTG unmatched** — flips `compatible` in the loaded device tree so `AppleUSBDeviceMux`/`AppleS5L8900XUSBWrangler` do not start. The wrangler crashes when the PHY registers synchronously before its notifier pointer is stored. Real fix is timer/VIC registration ordering or proper PHY modeling.
2. **Single ramdisk root** — `/etc/fstab` points to `/dev/md0` instead of NAND `disk0s1`/`disk0s2`. Real fix is a NAND/disk0 model.
3. **Activation state** — the work image includes a seeded `data_ark.plist` and a locally generated, locally signed activation record so `lockdownd` reports `Activated`. No Apple binary is modified.
4. **No-autolock prefs** — seeds `SBAutoLockTime`/`SBAutoDimTime` to `-1` to prevent the screen turning off during slow experiments. This is a user preference, not a binary patch.

## Current blockers / next steps

1. **Speed.** Interpreter is ~10× slower than real time. Next lever: decoded-block execution cache or basic-block translation.
2. **USB registration ordering.** Remove `--unmatch arm-io/usb-otg` by understanding why real hardware does not hit the PHY/wrangler race.
3. **NAND partitions.** Model disk0 so the real `/etc/fstab` works and `/private/var` exists.
4. **Networking.** Emulate Marvell 88W8686 SDIO Wi-Fi (or a simpler device) bridged to the host.
5. **SDL desktop window.** Native mouse/keyboard UI beyond the current browser harness.

## Work still in flight

Parallel child sessions are tackling:
- decoded-block interpreter speed engine
- USB OTG/PHY timing
- Marvell 88W8686 SDIO Wi-Fi
- SDL2 desktop frontend

## How to run

See `README.md` for full build/run instructions and firmware extraction commands.
