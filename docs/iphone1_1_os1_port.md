# Booting genuine iPhone OS 1.0 (1A543a) on an emulated iPhone1,1 / M68AP

Running engineering log for the iPhone1,1 target. Everything here was observed
on this emulator running Apple's unmodified 1A543a kernel, device tree and root
filesystem. No firmware, Apple binary or firmware key is stored in this
repository; the developer supplies a local IPSW.

## 1. Target

| | |
| --- | --- |
| Device | original iPhone (2007) |
| Product | `iPhone1,1` |
| Board | `M68AP` / `m68ap` |
| SoC | Samsung `S5L8900` |
| CPU | ARM1176JZF-S (ARMv6, VFPv2) |
| OS | iPhone OS 1.0, build `1A543a` ("Heavenly") |
| Kernel | `Darwin Kernel Version 9.0.0d1 ... xnu-933.0.0.178 RELEASE_ARM_S5L8900XRB` |

## 2. Architecture

```
core/           portable emulator core (no host UI, no libc beyond C11) -> WASM-friendly
  src/arm/      ARMv6 interpreter (arm_interp.c), VFP
  src/mmu.c     ARMv6 short-descriptor MMU + TLB
  src/soc/      shared S5L8900 devices: vic, gpioic, timer, clcd, spi, i2c, i2s,
                dma (pl080), nor, nand, mbx, power, uart, usbotg, storage, buttons,
                shared multitouch reports and the Z1/Z2 controllers
  src/board/    board definitions: board.c (registry), m68.c, n82.c
  src/boot/     bringup.c (iOS 3 / N82 path), bringup_os1.c (OS 1.0 path),
                dt_patch.c (device-tree patching shared by both)
  src/firmware/ 8900/IMG2/IMG3 unwrap, LZSS, Mach-O loader, kernel symbols
tools/iphone1.c host harness: argument parsing, firmware unwrapping, run loop,
                UART to stdout, PPM screenshots, tracing/diagnostics
tools/firmware/ python tooling: IPSW extraction, UDIF/vfdecrypt, HFSX reader,
                device-tree dump, work-image builder, PPM->PNG
```

The split the port introduced: everything S5L8900-generic lives in `core/src/soc`,
and per-board wiring lives behind a single `s5l_board_t` vtable
(`core/include/board.h`). Board code attaches I2C/SPI slaves and picks the NOR
base; no board `if` statements exist in SoC or boot code.

### M68 vs N82 (what actually matters for booting XNU)

| | M68 (iPhone1,1) | N82 (iPhone 3G) |
| --- | --- | --- |
| RAM base / size | `0x08000000`, 128 MiB | `0x08000000`, 128 MiB |
| NOR base | `0x24000000` | `0x00000000` (in-SoC alias used by iOS 3) |
| LCD | `lcd,merlot` on **SPI1** CS0 | `lcd,merlot` on SPI0 |
| Touch | `multi-touch,z1` on **SPI2** CS0 | MTZ2 on SPI1 CS0 |
| Audio | `audio-control,wm8758` on I2C0 `0x1a` | WM8991 |
| PMU | PCF50635 on I2C0 `0x73` | PCF50635 on I2C0 |
| Panel ID patch | none (OS 1.0 reads `_lcdPanelID: 0x000000`) | required |
| Kernel | Mach-O, `__PRELINK`, virt base `0xc0000000` | `__PRELINK_INFO`, later layout |

## 3. Memory map (as the OS 1.0 kernel uses it)

RAM `0x08000000`. The real device has 128 MiB; the current runs use
`--ram-mb 256` because the ~185 MiB root filesystem is booted as a RAM-resident
ramdisk (`rd=md0`) until NAND/disk0 is modeled. This is a deliberate deviation.

Layout printed by the harness for run52:

```
virt_base=0xc0000000 phys_base=0x08000000 ram_size=0x10000000
kernel 0x08000000..0x085c4b80  entry_pa=0x0805f7f0 (va 0xc005f7f0)
devicetree_pa=0x085c5000 size=0x9000    boot_args_pa=0x085ce000
ramdisk_pa=0x085d0000 size=0x0b8a5000
framebuffer_pa=0x13e78000 vram=0x00960000  top_of_kernel_data_pa=0x147d8000
```

MMIO (from the M68 device tree, `/arm-io` ranges):

| base | size | device |
| --- | --- | --- |
| `0x00100000` | | clock/reset (MIU) second range |
| `0x00200000` | | DMA0 (PL080) |
| `0x00400000` | | USB OTG |
| `0x00900000` | `0x1000` | CLCD (`aliases/screen` -> `/arm-io/lcd`) |
| `0x00a00000` | | NAND / FMC |
| `0x00c00000` | | AES |
| `0x00d00000` | | SDIO |
| `0x00e00000` | `0x2000` | PL192 VIC |
| `0x00e02000` | | edge interrupt controller |
| `0x01900000` | | DMA1 |
| `0x01a00000` | | power controller |
| `0x03000000` | `0x1000000` | MBX (2D/3D) |
| `0x04300000` | | SPI0 |
| `0x04500000` | | clock controller |
| `0x04600000` | | I2C0 |
| `0x04900000` | | I2C1 |
| `0x04a00000` | | I2S0 |
| `0x04c00000`/`04c04000`/`04c0c000`/`04c10000` | | UART0/1/3/4 |
| `0x04e00000` | | SPI1 |
| `0x05000000` | | PKE |
| `0x05200000` | | SPI2 (runtime physical `0x3d200000`) |
| `0x06200000` | | timer |
| `0x06300000` | | watchdog |
| `0x06400000` | | GPIO |
| `0x06500000` | | chip ID |
| `0x1c000000` | `0x100000` | NOR (device-tree view; physical base `0x24000000`) |

NOR sub-ranges: `diagnostic-data` `0x6000`, `raw-device` `0x8000..0xfc000`,
`nvram` `0xfc000` and `0xfe000` (two 8 KiB copies).

## 4. Firmware formats in 1A543a

1A543a predates IMG3. Components are `8900` containers:

```
0x00 '8900'   0x07 format byte   0x0c payload length (LE u32)   0x800 payload
```

For this build the payloads are **plaintext** (no AES needed): either an IMG2
(`2gmI`, payload at `+0x400`) or a bare `complzss` blob. `tools/firmware/unwrap8900.py`
and the harness both accept 8900/IMG2/`complzss`/raw.

The root filesystem is inside the IPSW's DMG (UDIF, vfdecrypt key from the
build's key set, supplied by the developer) and is **HFSX** (`HX`, version 5,
4 KiB blocks, 47269 blocks, not journaled). `tools/firmware/extract_1a543a.py`
drives IPSW -> kernel + device tree + rootfs image; keys are never hardcoded.

## 5. Boot handoff (`core/src/boot/bringup_os1.c`)

SecureROM -> LLB -> iBoot is skipped; the harness constructs XNU's entry state
directly. That is much cheaper than emulating the boot chain and loses nothing
for this milestone (the kernel only consumes boot args + device tree).

Differences from the existing iOS 3 path that forced a separate file:

* OS 1.0's `__HIB` segment starts exactly at the virtual base `0xc0000000`
  (the iOS 3 path rejects that).
* Prelink segment is `__PRELINK`, not `__PRELINK_INFO`.
* No N82 panel-ID device-tree patches, no iOS 3 SVC memory-disk bridge.

Placement rules: device tree 4 KiB aligned, one 4 KiB boot-args page, ramdisk
64 KiB aligned, framebuffer pool 16 KiB aligned, top-of-kernel-data rounded to
16 KiB with >= 24 MiB free above it. Device-tree patches applied: CPU/root
frequencies, `/memory/reg`, `/vram/reg`, `/chosen/debug-enabled`,
`/chosen/memory-map/RAMDisk`. Boot args are revision 1 / version 1 with a
320x480x32 framebuffer description and the command line (<= 255 bytes). Entry:
`r15 = entry_pa`, `r0 = boot_args_pa`.

## 6. Boot progress reached

Command used for the current best run (`--unmatch` explained in §8):

```
iphone1 --board m68 --kernel kernel_1A543a.macho --devicetree devicetree_m68ap.bin \
        --ramdisk work_1A543a.img --ram-mb 256 --debug-enabled \
        --cmdline "debug=0x8 serial=3 rd=md0 -v" \
        --unmatch arm-io/usb-otg --unmatch arm-io/spi2/multi-touch \
        --steps 1500000000 --serial run.log --screenshot run.ppm
```

Kernel log highlights (verbatim):

```
Darwin Kernel Version 9.0.0d1: Tue May 22 21:15:55 PDT 2007; root:xnu-933.0.0.178.obj~3/RELEASE_ARM_S5L8900XRB
vm_page_bootstrap: 14111 free pages and 51425 wired pages
70 prelinked modules
AppleS5L8900XIO::start: chip-revision: EVT0
AppleARMPL192VIC::start: _vicBaseAddress = 0xedc14000
AppleS5L8900XClockController: Dynamic Performance State Management Enabled with max state 3
AppleMBX: Detected mbx,s5l8900x.  SupportsWriteCombining: true
AppleImage2NORAccess::start(raw-device) <1>
ApplePCF50635PMU::start: pmu _pmuIICNub: 0xccbec800
BSD root: md0, major 2, minor 0
AppleMerlotLCD: _lcdPanelID: 0x000000
Jettisoning kernel linker.
AppleMBX: Added swap device: AppleH1CLCD  id: cc995200
/dev/md0 on / (hfs, local, noatime)
```

Userspace: genuine `launchd` runs, `fsck_hfs` validates the volume ("The volume
Heavenly1A543a.UserBundle appears to be OK"), daemons start (`configd`,
`notifyd`, `syslogd`, `BTServer`, `lockdownd`, `CommCenter`), and
`_execve` tracing shows Apple's own binaries executing, including
`/System/Library/CoreServices/SpringBoard.app/SpringBoard`.

SpringBoard loads its real frameworks (UIKit, CoreGraphics, Foundation,
LayerKit, IOMobileFramebuffer, CoreSurface, MBX2D, MBXConnect, OpenGLES,
MultitouchSupport), reads its genuine artwork/fonts/app metadata, and **renders
into the framebuffer**: the captured 320x480 PPM contains ~44k distinct colours
and shows the authentic 2007 "Activate iPhone / connect to iTunes" lock screen
with the Earth wallpaper, the "Searching..." status bar and the emergency-call
slider.

That is the first-pixels milestone: genuine SpringBoard drawing on emulated
hardware.

### Home screen

With the activation state seeded into the work image (§9) and a 12G-instruction
run (`--shot-every 500000000` writes a PPM every 500M instructions):

* ~0.5–5G: kernel text, then black while SpringBoard starts and waits for
  `CommCenter`/`lockdownd` (SpringBoard's main thread blocks ~4.5G instructions
  in a Mach receive on the CommCenter registration reply);
* 5.5G: **the genuine June-2007 home screen** — status bar with time and
  battery, the 12 icons (Text, Calendar, Photos, Camera, YouTube, Stocks, Maps,
  Weather, Clock, Calculator, Notes, Settings) and the Phone/Mail/Safari/iPod
  dock;
* 6.5G: a "Repair Needed — iPhone cannot make or receive calls" alert on top of
  the home screen (CommCenter has no baseband — expected, dismissable once touch
  exists);
* later the display is turned off (`AppleMerlotLCD::_lcdEnable: enable: 0`):
  SpringBoard's idle auto-lock, since there is no input yet.

SpringBoard's main thread then sits in a healthy CFRunLoop (~2k syscalls per
guest-second, mostly `mach_msg` with CommCenter). The run takes ~5.5 min of host
time for 12G instructions.

### Interpreter speed and cycles per instruction

The first measured interpreter optimization keeps the existing guest-visible
timing contract while reducing host overhead. The home-snapshot benchmark
(`--no-fb`) executes 400M instructions after restoring the snapshot at
instruction 9.79G. The measured snapshot-load offset was 1.204637289 s:

| build | executed instructions | load offset | net seconds | M instructions/s | gain |
| --- | ---: | ---: | ---: | ---: | ---: |
| baseline | 400M | 1.204637289 s | 11.304953053 s | 35.383 | 0% |
| native `-O3` + IPO | 400M | 1.204637289 s | 10.418884755 s | 38.392 | 8.50% |
| privileged tick batching | 400M | 1.204637289 s | 9.607753276 s | 41.633 | 17.66% |

The baseline `perf` profile's top ten symbols were:
`arm_step` (21.27%), `thumb_step` (17.79%), `s5l8900_run` (10.96%),
`s5l8900_tick` (6.46%), `exec_data_processing` (5.93%),
`mem_r32_as` (5.86%), `arm_mmu_translate` (4.35%),
`sn_raw.part.0` (2.41%), `snap_verify_stream.constprop.0` (2.26%), and
`arm_cond_passed` (2.16%).

Interpreter tick batching now applies in privileged modes as well as User
mode, stopping at each timebase edge and immediately flushing before WFI,
guest MMIO, host input, or a host-time callback. The privileged SVC hook
continues to disable batching because it can execute host work inside
`arm_step()`. The WFI flush uses direct `s5l8900_tick()` so pending retired
instructions are represented before the idle jump; in active-clock mode this
also records the observed guest ticks for the next host-clock synchronization.

### Cycles per instruction

`--cpi` and the WebAssembly `emu_set_cpi` API model the average ARM1176 cycles
per retired instruction. They leave the guest wall clock (the 6 MHz timebase)
unchanged and only change how many instructions fit in a guest second; this is
a timing-model parameter, not a guest modification. Both defaults are 1; the
web frontend accepts `?cpi=N` (1..16).

What it does and does not buy: the guest is only time-starved while it is
CPU-saturated. In steady state the guest does a fixed amount of work per guest
second and WFI jumps straight to the next timer edge, so guest time advances at
`host_instr_per_s / work_per_guest_s` regardless of CPI — measured with the
guest's own Stopwatch in Chrome: 0.74x real time at CPI 1 vs 0.65x at CPI 2
(the extra timer traffic costs ~13% host throughput). App launches are
host-CPU-bound and take the same host time at either setting. CPI only
shortens saturated, timeout-bound phases in guest time: cold boot reaches the
home screen at 7.0G steps (198 s) at CPI 1 and by ~3.5G steps at CPI 2, and the
tap → Settings → Home sequence renders identical screens at CPI 1/2/4. Host
responsiveness therefore has to come from interpreter speed, not from CPI.

## 7. Hardware behaviour implemented for this target

* **NOR + CHRP NVRAM.** M68 NOR sits at physical `0x24000000`. xnu-933's
  `IODTNVRAM::initNVRAMImage` walks partitions by their length field and has no
  `length < 16` guard, so a blank NVRAM hangs it. `m68.c` programs a valid
  `common` partition (with the CHRP checksum) into both 8 KiB copies, which is
  what a real unit ships with.
* **Timer/timebase.** OS 1.0 programs timer4 differently from iOS 3. Timer4 is
  a one-shot down-counter: expiry latches `TIMER4_IRQ_BITS`, zeroes the value,
  and clears `START`. The kernel re-arms it by writing `COUNTBUF` followed by
  `STATE=START|UPDATE`, as `_s5l8900x_set_decrementer` does. Auto-reload was
  wrong: a slow handler re-latched FIQ, and the 10-tick minimum deadline then
  livelocked. The machine converts CPU cycles to timebase ticks with an
  accumulator (`412 MHz` CPU, `6 MHz` timebase). Before this, the 100 Hz
  `hertz_tick` fired ~5x in 114M instructions instead of ~27x and `IODelay`
  wakeups arrived millions of instructions late, which broke driver-registration
  ordering.
* **MBX.** OS 1.0 writes 0 (as well as 1) to the TA context-reset register and
  polls the status bit; the model now completes for 0 and 1.
* **ARM exception state.** `MSR CPSR` in exception handlers no longer allows the
  T bit (or bit 24) to be written, matching ARM1176 behaviour; without this the
  kernel took a wild data abort during symbol lookup.
* **Merlot LCD on SPI1**, PCF50635 PMU, WM8758 (register writes are NAKed and
  the driver tolerates it), CLCD scanout to the host framebuffer.
* **Z1 multitouch on SPI2 CS0.** The model implements the two-stage bootloader:
  `c2` A-speed upload, `c4` execute, `c3` B-speed stream, `05` checksum, then
  `c4` firmware transition. Firmware mode uses full-duplex `aa` replies for
  `d0` interface version, `8f` report information, `82` report data and `92`
  set-report. Result-length reads use `aa L_hi L_lo checksum_hi checksum_lo`;
  result-data reads return a path frame followed by its big-endian payload
  checksum. Z1 input frames use type `0xce`, with a 16-byte header and 16-bit
  contact coordinates plus density/total fields. The 1A543a parser's `0xcc`
  path route hard-codes contact density to zero, so a `0xcc` frame can never
  pass the make/break density threshold and register a touch. Z1 timestamps are
  device uptime in milliseconds, not frame-relative time; the model advances
  that clock from the 6 MHz timebase even while idle. This matters because
  MultitouchSupport's cheek and flat-finger-clasp face detectors use a
  99-millisecond hysteresis against an initially zero last-detected timestamp:
  small frame timestamps produce face flags and suppress fingers. Reports `d1`,
  `d3`, `d7`, `d9`, `d0`, and `a1` share MTZ2's 15-row, 10-column, 4800x7200
  geometry. Z1 Zephyr parameters use the table-backed reports listed in
  `core/src/soc/mtz1.c`; the 1A543a init sequence ends with `0xbf` and `0xaf`
  set-reports and does not write `0xab`. The frame-timestamp and face-flag
  observations came from MultitouchSupport PCs `0x3130e07c` and `0x3130e12c`.
  Attention is GPIOIC line 163.

  Alert dismissal is proven with a contact at `(160,315)`; Settings and Safari
  launch from their corresponding SpringBoard icons. The Home-button path was
  traced through SpringBoard's `0x3cc40` dispatch table: type 3 reaches
  `0x3bbe0`, while type 10 digitizer input reaches `0x3ccc4`. The Menu event
  uses HID consumer page `0x0c`, usage `0x40`; Power is `0x30`, and volume uses
  `0xe9`/`0xea`. SpringBoard's byte at `0x8d69` records the display-dimmed
  state, set by `setDimmed:`/the dim timer and cleared at `0x928c`. While
  dimmed, Menu is consumed by
  `performSelectorOnMainThread:@selector(userEventOccurred:)` at `0x3be18`
  to undim; otherwise it follows the `0x3e8`/`0x3e9` menu down/up GSEvent path
  at `0x3bd4c`. With the original idle timing, the device auto-locked during
  long experiments, so the first Menu press only undimmed and a later press
  reached the lock screen. The work-image `--no-autolock` option seeds
  `SBAutoLockTime` and `SBAutoDimTime` to integer `-1`; this is a user
  preference, not a binary patch, and can be dropped once real-time speed
  makes the idle timer a non-issue.

### App switching and the MBX 2D path

With an activated, no-autolock work image, the sequence *dismiss alert → tap
Safari → Menu → tap Settings → Menu* now ends on the genuine home screen each
time (`home50`/`home51` runs, screenshots every 100M instructions). Two MBX
fixes were needed, both derived from what SpringBoard/CoreSurface on 1A543a
actually submits (`MBX2D reject ring+0x0040 (2 commands): packet terminators
are incomplete`, followed by the guest's own `Graphics hang with pending 2D
blit` / `Graphics Restart during 3D Blit` recovery):

* OS 1.0 pads 2D copy/blend packets with a *variable* number of `0x70000000`
  terminators (later firmware always pads to the maximum). The parser now
  requires only the first terminator after the packet body, consumes the
  contiguous run, and advances batched packets by the real length.
* The app-close animation stretches single-column textures (constant u). The
  textured-sprite validator accepts `u_start == u_end`.

The close animation itself still scans out black for ~400M instructions (the
3D blit path is not rendered), then the home screen reappears. Rendering that
transition is cosmetic and left for later.

## 8. Workarounds currently in force (each is a substitution for hardware we
have not modeled yet, not a firmware patch)

1. `--unmatch arm-io/usb-otg` — flips one byte of the node's `compatible` string
   in the *loaded copy* of the device tree so IOKit does not match the USB
   drivers. Reason: `AppleS5L8900XUSBWrangler::phyRegistered` dereferences its
   notifier pointer, and with our (correct) synchronous registration the
   callback runs before the pointer is stored. USB is not needed for
   SpringBoard. What we know so far (`run_usb.sh` runs, `--pc-log` on
   `_thread_setrun` `0xc0021e2c`, `_thread_invoke` switch `0xc005fa20`,
   `_thread_quantum_expire` `0xc00206a4`, `csw_check` `0xc00217b4`):
   * `AppleS5L8900XUSBWrangler::start` (`0xc043a32c`) calls
     `IOService::addNotification` for the PHY and only stores the returned
     notifier at `[this+0x6c]` afterwards (`0xc043a34c`); the callback
     `phyRegistered` (`0xc043a4dc`) loads `[this+0x6c]` first. So the crash
     occurs iff the PHY is already registered when the wrangler resumes.
   * The wrangler's thread blocks ~50M instructions (77M→127M) waiting for the
     interrupt controller (`AppleARMPL192VIC` registers at ~107M); the PHY
     (`start` at 98.5M, no interrupt dependency) registers at ~99.5M.
   * xnu-933's IOKit config threads are fixed-priority 81 and are **not**
     time-sliced in kernel mode: `csw_check` returns `AST_PREEMPT` without
     `AST_URGENT` (the pset `urgency` count is zero), and only urgent ASTs are
     taken in `ml_set_interrupts_enabled`. The 27 config threads spawned when
     `arm-io` publishes its children (64–65M) therefore run FIFO to their
     first block, and the power→CPU→VIC chain woken at 72.9M does not run until
     ~107M. `--insn-hz` experiments (60–275 MHz) moved nothing: the ordering is
     a property of the guest scheduler, not of our timer pacing (timer4
     programming was verified against `_s5l8900x_get_decrementer`:
     `count_buffer - live_value`, i.e. a down-counter).
   * The PHY's `start` touches only `0x3c400000` (OPHYPWR/ORSTCON bit
     toggles with `IODelay`, no sleeps), so nothing in the PHY itself can make
     it register later. The remaining hypothesis is that on hardware the
     clock/power/VIC chain registers *before* the arm-io child burst (e.g. an
     earlier interrupt-driven wakeup we do not reproduce), which would let the
     wrangler run through without blocking. Unverified; still under
     `--unmatch`.
   Separately, without a USB device controller userspace `lockdownd` logs
   `Can't find AppleUSBDeviceMux service` and is respawn-throttled by launchd;
   this was initially treated as harmless for SpringBoard. The 2026-09-05
   [transition investigation](../plans/transition-latency.md) instead traces
   a 30-second guest-time Home delay to retries against this absent daemon.
   It is distinct from the historical kernel crash, but is now a performance
   blocker. See [current USB revalidation](../plans/usb.md).
2. Single-ramdisk root. The stock `/etc/fstab` mounts `/dev/disk0s1` and
   `/dev/disk0s2`; we boot the rootfs as `md0` with no NAND partitions, so
   `launchd` aborts in `fwexec(mount_tool)`. `tools/firmware/rootfs_work.py`
   builds a *work image* (a copy; the extracted firmware image is never
   modified) whose `/etc/fstab` is one line `/dev/md0 / hfs rw 0 1`, and which
   sets `LK_ENABLE_MBX2D=0` / `LK_ENABLE_OGL=0` in SpringBoard's launchd plist.
   Replaceable by modeling NAND/disk0 partitions.

No Apple binary is modified anywhere in this pipeline.

## 9. Activation: lockdownd verification path

Unseeded, `lockdownd` eventually recomputes activation at startup from
`/var/root/Library/Lockdown/activation_record.plist`: with no record it logs
"The activation record was not valid" and sets the activation state to
`Unactivated`, even if a cached data-ark state initially let SpringBoard reach
the home screen. The genuine verification path loads
`/System/Library/Lockdown/iPhoneActivation.pem` and
`iPhoneDeviceCA.pem` with `PEM_read_X509`. The checks are visible around
`0x8b18` (record check), `0x9de0` (certificate/signature verification), and
`0xa070` (state selection):

* `DeviceCertificate` is verified with the public key in
  `iPhoneDeviceCA.pem`;
* `AccountToken` is hashed with SHA-1 and `AccountTokenSignature` is checked
  with `RSA_verify(NID_sha1, ...)` and the public key in
  `iPhoneActivation.pem` (PKCS#1 v1.5);
* when `AccountToken` is not parseable as a property list,
  `lockdownd` selects `Activated` directly after the cryptographic checks.

The observed unseeded syslog path is therefore "The activation record was not
valid" followed by "Setting the activation state to Unactivated" when the
record is absent. With the local authority in place, act40 instead logged
`SpringBoard[12]: lockdown says the device is: [Activated], state is 2` and
repeated `lockdown: The activation state has not changed.` lines. The
`rootfs_work.py --activate` option supplies the data that hardware activation
normally receives from Apple's service: a locally generated device certificate
signed by a local device CA, a local activation certificate, and a matching
activation record whose non-plist token is signed with SHA-1/RSA. It replaces
the two certificate *data files* and adds the record; no Apple binary is
modified. The keys are throwaway and are never written into the image.

The separate `--add` seed still supplies a cached *Activated* state during the
early boot path. It creates `/var/root/Library/Lockdown/data_ark.plist` in the
HFSX catalog (folder/file + thread records, allocation bitmap, volume counters,
both volume headers). The seed is a flat plist:

```xml
<dict>
  <key>com.apple.mobile.lockdown_cache-ActivationState</key>
  <string>Activated</string>
</dict>
```

`--swi-log` confirms `lockdownd` opens `/var/root//Library/Lockdown/data_ark.plist`,
and with it SpringBoard proceeds to the home screen (§6). The data ark is
per-device state, not an Apple binary; `--activate` additionally keeps the
later activation-record verification on the Activated path.

## 10. Diagnostics and input in the harness

`--trace-devices`, `--break-pc`, `--watch-pc`, `--pc-log`, `--trace-from/--trace-last`
(cycle-based, 2M-entry ring of `pc/cpsr/r0-r3/lr/sp`), `--exec-pc` (prints every
`_execve` path), `--swi-log` (Darwin/Mach syscall log with decoded paths),
`--dump-va`, `--unmatch`, `--break-swi SYSCALL TTBR0`, `--shot-every N`
(periodic PPMs), and the `--serial` file prefixes every line with the guest
instruction count so kernel messages line up with the syscall log. `tools/firmware/ksym.py` and the local `tsym.py`
annotate traces with kernel symbols.

Repeatable host input is available without a UI frontend:

```text
--at STEP tap X Y
--at STEP touch down|move|up X Y
--at STEP button NAME down|up
--at STEP press NAME
```

`NAME` is one of `hold`, `menu`, `volup`, `voldown` and `ringerab`. Every
injected event is printed to stderr. `tap` sends contact 1 down and schedules
an automatic lift after 100 ms of guest time; `press` releases the button
after the same guest-time interval. Explicit touch and button down/up events
remain step-scheduled. Touch events call the machine-level API and use the
same coordinate conversion as MTZ2.

Snapshots preserve the guest CPU instruction count and device state:

```text
--save-at 5800000000 fw/home.snap
--load fw/home.snap --at 6000000000 tap 48 60 --steps 6500000000
```

Loading resumes absolute instruction counting from the saved CPU cycle count;
`--steps` is an absolute stop count, not an additional run length. Snapshot
files and firmware images belong outside this repository.

## 11. Next steps

1. Input: Home/hold buttons through the M68 buttons GPIOs (dismiss the Repair
   alert, wake the display), and Z1 multitouch scripted events.
2. Speed: native execution now measures ~41.6 M instructions/s, or roughly
   1/10 real time at the 412 MHz model clock; an interpreter fast path or a JIT
   for the boot-to-home-screen path may provide further gains before
   interactive use.
3. Model USB OTG PHY registration ordering -> remove workaround 1; model NAND
   partitions -> remove workaround 3.
4. Then: application launching, Settings, MobileSafari, stability; only after
   that, the Emscripten/WASM port (explicitly out of scope for now).

Nothing found so far suggests the goal is infeasible: the kernel, drivers,
userspace and SpringBoard all run unmodified, and the remaining blockers are
ordinary missing-device / missing-state problems.
