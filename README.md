# iphone-1

An S5L8900 emulator whose goal is to run the genuine iPhone OS 1.0 (build
1A543a) on an emulated original iPhone (`iPhone1,1` / `M68AP`). Derived from
[S5LBox](https://github.com/j0shua-SYSON/S5LBox), with a separate M68 board
target and an OS 1.0 boot path.

Status and engineering log: [docs/iphone1_1_os1_port.md](docs/iphone1_1_os1_port.md).
Current browser implementation and verification: [plans/execution.md](plans/execution.md).
Product roadmap: [plans/web-iphone.md](plans/web-iphone.md).
Original-app audit: [plans/app-coverage.md](plans/app-coverage.md).

## Firmware

The source copies in `web/firmware/` include the kernel, device tree,
compressed work image, and home-screen snapshot used by `web/manifest.json`.
The production manifest downloads matching public copies from
`https://iphone.files.garden/`; the browser verifies every object against the
checked-in SHA-256 digest before caching, decoding, or loading it.
To regenerate them from the original restore package, supply
`iPhone1,1_1.0_1A543a_Restore.ipsw` and its root-filesystem key locally:

```sh
python3 tools/firmware/extract_1a543a.py iPhone1,1_1.0_1A543a_Restore.ipsw fw/ --rootfs-key HEX
# -> fw/kernel_1A543a.macho  fw/devicetree_m68ap.bin  fw/rootfs_1A543a_hfs.img

python3 tools/firmware/rootfs_work.py fw/rootfs_1A543a_hfs.img fw/work_1A543a.img \
    --fstab "/dev/md0 / hfs rw 0 1" \
    --launchd-env com.apple.SpringBoard.plist=LK_ENABLE_MBX2D=1,LK_ENABLE_OGL=0 \
    --add /var/root/Library/Lockdown/data_ark.plist=tools/firmware/data_ark_activated.plist \
    --no-autolock --activate
```

The `--add` option seeds `lockdownd`'s cached activation state, while
`--activate` replaces the two Lockdown certificate data files in the work image
and adds a locally signed activation record so the genuine verifier continues
to report Activated. `--no-autolock` seeds the SpringBoard idle-time
preferences. These are data-file changes only; no Apple binary is modified,
and the extracted image is copied, never changed.

## Build and run

```sh
cmake -S . -B build -G Ninja && cmake --build build && ctest --test-dir build
./build/tools/iphone1 --board m68 \
    --kernel fw/kernel_1A543a.macho --devicetree fw/devicetree_m68ap.bin \
    --ramdisk fw/work_1A543a.img --ram-mb 256 --debug-enabled \
    --cmdline "debug=0x8 serial=3 rd=md0 -v" \
    --steps 12000000000 --serial run.log --screenshot run.ppm --shot-every 500000000
python3 tools/firmware/ppm2png.py run.ppm.11.ppm home.png
```

The genuine 2007 home screen appears after several billion instructions
(historically around 5.5–7G, depending on the work image and clock settings).
`--shot-every` writes `run.ppm.<n>.ppm` every N instructions. Touch and physical
button input are implemented; keep the multitouch node matched. The work-image
command above disables automatic screen locking during experiments.

The harness also supports repeatable `--pc-log ADDR` targets for logging
registers and readable argument buffers at selected guest PCs, as well as
`--at STEP tap X Y`, `--at STEP touch down|move|up X Y`, and
`--at STEP press NAME` for scripted input.
`tap` schedules its automatic lift after 100 ms of guest time. Explicit
`touch down|move|up` events remain step-scheduled. `press NAME` holds the
button for 100 ms of guest time before releasing it.

For long idle experiments, `rootfs_work.py --no-autolock` seeds
`SBAutoLockTime` and `SBAutoDimTime` as integer `-1` in SpringBoard's
preferences. This is a user-preference change in the work image, not an Apple
binary patch.

## Browser build

The browser displays the genuine guest framebuffer inside a clickable iPhone
case. Emulation runs in a dedicated worker; the page handles touch, physical
buttons, loading, and browser saves. Emscripten **4.0.16** is the tested SDK.

On the provided Amazon Linux workspace, this installs dependencies and builds:

```sh
bash tools/setup.sh
npm run serve
```

With dependencies already installed:

```sh
bash tools/build-web.sh
npm run serve
```

The build script accepts an SDK on PATH or the one installed in `.context/emsdk`.
Generated files live in `web/dist/emu.js` and `web/dist/emu.wasm`. Open
`http://127.0.0.1:8080/`; `PORT` overrides the port, and local Conductor workspaces
use their assigned `CONDUCTOR_PORT`. Conductor's run menu includes Web, Build,
and Test scripts.

The manifest uses a home snapshot by default, avoiding the multi-minute cold
boot. Remove its `snapshot` entry to boot from the rootfs. The supplied snapshot
already has the Repair Needed alert dismissed. Native diagnostic commands in
older plans may contain stale icon coordinates; the supplied home state has
Settings near `(275,250)` and Safari near `(200,430)`.

New phones use accelerated MBX graphics, the guarded WebAssembly CPU engine,
and foreground clock pacing. A quiet paired VM test with 20 warm samples per
action measures Home p95 at **0.92 seconds**, down from 1.27; Settings p95 is
**3.57 seconds**, down from 3.88 and still above its two-second target. See the
[measured comparison and recording](tweets/055_mbx_navigation_speed.md),
[continuous sixteen-app walkthrough](tweets/056_continuous_sixteen_apps.md),
and [default package verification](tweets/058_accelerated_default_phone.md).
These are cloud-VM results, not a full-speed or reference-Mac acceptance claim.
Existing saved phones keep their boot state, graphics mode and data. Export
first, then Reset to start a fresh accelerated phone; importing an old backup
restores its older state too. The original USB service keeps lockdownd alive;
USB data transfer to a host is still unmodeled.

The fresh phone uses a 168 MiB compacted work image. Preserving every file while
removing unused disk blocks frees about 16.6 MiB of guest RAM and fixes the
verified Contacts editing failure caused by memory pressure. About 6 MiB of disk
space remains; large media libraries still need a separate storage device.
Existing saved phones keep their original disk layout and data. To try the new
starting phone, Export your saved phone first, then Reset. See the
[memory investigation](plans/guest-memory.md) for validation and limits.

Click the round Home button or use Escape/H; P is sleep/wake, +/- is volume,
and M toggles the ringer. Drag the guest screen to scroll. Touchscreen browsers
can supply multiple fingers; broader gesture behavior is still being verified.
Volume and ringer GPIOs are modeled, but audible sound is not implemented yet.

Save stores a compressed checkpoint in this browser, retaining a previous
generation. Autosave runs every 30 seconds when input is idle, with extra
opportunities when the tab is hidden. Initial snapshot capture measured about
0.17–0.20 seconds; checksum and transfer copying then yield in small chunks
while the guest continues. Pause and Resume remain available during ordinary saves; saving finishes without resuming a paused phone. Full save still takes several seconds. Export creates a
portable `.snap` backup; Import also accepts gzip-compressed snapshots. One tab
owns the writable profile; other tabs can explore and export. Keep exported
backups because browser storage can be cleared or reach its quota. Exact
snapshot-format compatibility is required; incompatible saves are preserved
for export rather than silently reset.

Keep offline installs a verified copy of the page, emulator, and R2-hosted firmware for
disconnected reloads. Firmware cache corruption triggers verification and a
fresh download; Retry is available after reconnecting. Offline bundle updates
wait until existing phone tabs close. After changing frontend files, rebuild
to regenerate `web/service-worker.js`; its content hashes keep matching JS and
WASM together. If firmware changes, run `python3 tools/firmware/update_manifest.py`
before rebuilding.

The full product goal is **not complete**: networking, cellular service,
audio, Camera, rotation, many app workflows, and near-real-time performance
remain under development. The current verification includes real Settings and
Safari navigation; the presence of an icon does not imply a working app.

## Verification

```sh
cmake -S . -B build && cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
npm run test:web
npm run test:unit
npm run test:capture
# With the server running and Chrome installed:
npm run test:browser
npm run test:persistence
npm run test:offline -- --record
npm run test:controls
npm run test:offline-update
npm run test:clock
npm run test:save-pause
```

Browser tests use `/usr/bin/google-chrome` by default (`CHROME_PATH` overrides it).
The real background-tab clock test also requires `DISPLAY` or `Xvfb` (on this
VM: `sudo dnf install -y xorg-x11-server-Xvfb`). It creates a disposable Chrome
profile with Playwright's focus override disabled.
Video recording also needs Playwright's ffmpeg: `npx playwright install ffmpeg`.
Reports, guest screenshots, and recordings go to `.context/`; committed milestone
recordings and explanations are indexed in [tweets/README.md](tweets/README.md).
