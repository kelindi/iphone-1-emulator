# iphone-1

An emulator that runs iPhone OS 1.0, build `1A543a`, for the original iPhone
(`iPhone1,1` / `M68AP`). Try the browser version at [2007.mobile](https://2007.mobile/).

The emulator executes Apple's original kernel, frameworks, SpringBoard, and apps.
SpringBoard is the original home-screen application.
The browser displays the guest framebuffer, which contains the pixels drawn by the emulated phone.
HTML and CSS provide the phone case and surrounding controls.

The portable C core derives from [S5LBox](https://github.com/j0shua-SYSON/S5LBox).
The project adds an original-iPhone hardware target, an OS 1.0 boot path,
a native diagnostic tool, and a WebAssembly browser interface.

## Current status

Status checked against the code and retained test reports on October 13, 2026.
The phone boots and supports several offline workflows.
Many original apps still cannot complete their main tasks.

- **Original interface:** all 16 apps reached recognizable UI in the launch audit.
  An app opening does not prove that its main task works.
- **Offline apps:** focused browser tests cover Calculator, Notes, Calendar, and Contacts workflows.
  The tested organizer edits survive save and reload.
- **Input:** the browser supports taps, scrolling, slide to unlock, and the physical phone buttons.
  Broader gesture behavior still needs tests.
- **Clock:** focused tests cover Stopwatch timing and manual date/time changes across save and reload.
  Audible alarms remain unfinished.
- **Saved data:** the browser supports manual save, autosave, pause, backup export, and backup import.
  Tests cover selected recovery and saved-phone upgrade cases.
- **Offline visits:** Keep offline stores the page, emulator, and verified firmware for disconnected reloads.

See the [app audit](plans/app-coverage.md) for tested workflows and their limits.
The [engineering log](plans/execution.md) records later changes.
Some audit entries describe older fixtures, so their dates and stated scope matter.

### Experimental app services

Separate prepared images demonstrate these workflows:

- Safari loads local pages, submits a form, and preserves a bookmark across reload.
- Weather and Stocks display fixed demo data through local services.
- YouTube signs a local device challenge and displays one demo catalog entry.
  Its player receives movie bytes, but playback still fails.

These experiments use the original apps and guest networking code.
They require their own prepared images and local services.
The default phone does not include these services.
See the [service tools](tools/legacy_services/README.md) and
[YouTube investigation](plans/youtube-services.md).

### Issues that still need fixes

1. **Speed:** reduce app launch delays and qualify typing, scrolling, and animation.
   Settings still exceeds the two-second warm-launch target in the recorded cloud baseline.
2. **Networking:** complete a usable guest network connection and compatible app services.
   General internet access, Maps, Mail, and modern service compatibility remain unfinished.
3. **Audio and video:** original system sounds (such as the lock click) now play through the modeled I2S clock, FIFO, and DMA.
   iPod music and YouTube playback have not been verified yet, and sound stutters when the emulator runs slower than real time.
   Genuine video playback remains unfinished.
4. **Photos and Camera:** complete photo-library navigation, camera preview, capture, and saved media.
   An isolated importer supplies images, but broader photo workflows still fail or lack tests.
5. **Phone and Text:** complete simulated call and message workflows through the original apps.
   The baseband device is not ready.
   Real carrier service lies outside the initial product scope.
6. **Hardware and reliability:** complete USB host transfers and the native NAND storage layout.
   Check broader gestures, preference effects, alarms, long sessions, and saved-data upgrades.
   Test actual Safari and Chrome on reference laptops.
7. **CPU accuracy:** fix the known WebAssembly floating-point failures.
   Existing tests expose missing exception flags and incorrect rounding behavior.
   See the [floating-point failure report](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/wasm-vfp-conformance/README.md).

The [product roadmap](plans/web-iphone.md) defines the remaining targets.
The [completion checklist](plans/completion-checklist.md) explains the larger gaps.
Hardware-specific plans and reports provide the detailed failure evidence.

### Performance evidence

A September 8 cloud baseline used Chrome 152 on a 2.90 GHz Xeon.
It measured 30 warm samples for each action.
Settings measured 2.318 seconds at the median and 2.567 seconds at p95.
Home measured 0.866 seconds at the median and 0.868 seconds at p95.
The p95 value describes the threshold for 95% of the samples.

These results apply to that fixture and cloud machine.
They do not establish performance on your hardware.
See the [baseline report](https://github.com/kelindi/iphone-1/blob/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces/adaptive-compiler-baseline/README.md).
A saved home-screen snapshot avoids the slow full boot.
The default browser also uses accelerated graphics.
The CPU engine uses WebAssembly code generated for selected instruction regions.
Runtime checks return unsupported work to the interpreter.

## Run the browser locally

You need Git, Node.js, npm, CMake, a C/C++ compiler, and Python.
CI uses Node.js 22 and Emscripten 4.0.16.

On Amazon Linux, the setup script installs build dependencies and the Emscripten SDK:

```sh
bash tools/setup.sh
npm run serve
```

Open `http://127.0.0.1:8080/`.
Set `PORT` to choose another server port.
If you define `CONDUCTOR_PORT`, the server uses it unless `PORT` overrides it.

With build dependencies and Emscripten already installed:

```sh
npm ci
bash tools/build-web.sh
npm run serve
```

The build script finds Emscripten on `PATH` or in `.context/emsdk`.
It writes generated files to `web/dist/` and regenerates the offline package.
Rebuild after you change browser files.

The default [manifest](web/manifest.json) downloads firmware and a saved home state
from `https://iphone.files.garden/`.
The browser checks each download against its recorded SHA-256 digest.
It then restores the saved state instead of performing a full cold boot.
Source copies of these firmware objects remain in `web/firmware/`.

## Controls and saved data

Slide to unlock when the phone opens.
Tap the guest screen to select an app.
Drag the guest screen to scroll.

| Control | Keyboard shortcut |
| --- | --- |
| Home | Escape or H |
| Sleep/wake | P |
| Volume up/down | + / - |
| Silent mode | M |

Volume and silent-mode controls change emulated hardware state.
They do not yet produce audible sound.

Save stores a compressed checkpoint in the browser and retains a previous generation.
A checkpoint contains the phone's memory, device state, and stored data.
Autosave runs every 30 seconds when input is idle.
The page also tries to save when the tab becomes hidden.
Pause and Resume remain available during ordinary saves.
A save does not resume a paused phone.

Export creates a portable `.snap` backup.
Import accepts a compatible snapshot, including a gzip-compressed snapshot.
One tab owns the writable profile.
Other tabs can explore the phone and export backups.
The browser preserves incompatible saves for export instead of silently resetting them.

Keep exported backups.
The browser can clear stored data or reach its storage limit.
Reset removes the current saved state and restores a fresh phone.
Export your phone before Reset.
Importing an older backup also restores that backup's older phone state.

Keep offline downloads the complete offline package.
Offline mode needs service-worker support and HTTPS or localhost.
If a cached firmware file fails its digest check, the browser tries a fresh download.
Offline package updates wait until existing phone tabs close.

The public site uses PostHog for visit and emulator lifecycle events.
The configuration disables session recording and automatic UI capture.
Event calls exclude guest text, pixels, touch coordinates, logs, and backup contents.
Local and preview hosts do not send these production events.
See [the analytics configuration](web/analytics.js).

## Firmware and native diagnostics

The prepared work image seeds local activation data and disables automatic locking.
It mounts the root filesystem from a RAM disk instead of the native NAND partitions.
These changes affect data files and boot configuration.
The project does not patch Apple's executable code.
The emulator retains the upstream [S5LBox license](third_party/S5LBox-LICENSE).

To prepare a work image, supply the original restore package and its root-filesystem key locally:

```sh
python3 -m pip install -r tools/firmware/requirements.txt
python3 tools/firmware/extract_1a543a.py iPhone1,1_1.0_1A543a_Restore.ipsw fw/ --rootfs-key HEX
python3 tools/firmware/rootfs_work.py fw/rootfs_1A543a_hfs.img fw/work_1A543a.img \
    --fstab "/dev/md0 / hfs rw 0 1" \
    --launchd-env com.apple.SpringBoard.plist=LK_ENABLE_MBX2D=1,LK_ENABLE_OGL=0 \
    --add /var/root/Library/Lockdown/data_ark.plist=tools/firmware/data_ark_activated.plist \
    --no-autolock --activate
```

The extractor produces a kernel, device tree, and root filesystem.
The work-image tool copies the extracted image before it changes data.
These commands prepare a work image.
They do not recreate every packaged snapshot or release artifact.
See the [boot and hardware log](docs/iphone1_1_os1_port.md) for details.

Build and test the native diagnostic tool:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

Run a full native boot with the prepared files:

```sh
mkdir -p .context
./build/tools/iphone1 --board m68 \
    --kernel fw/kernel_1A543a.macho --devicetree fw/devicetree_m68ap.bin \
    --ramdisk fw/work_1A543a.img --ram-mb 256 --debug-enabled \
    --cmdline "debug=0x8 serial=3 rd=md0 -v" \
    --steps 12000000000 --serial .context/run.log \
    --screenshot .context/run.ppm --shot-every 500000000
```

A full boot takes billions of guest instructions.
The native tool supports scripted input, snapshots, screenshots, and CPU/device logs.
It does not provide a desktop phone window on the current main branch.

## Tests

After the web build, run the unit and WebAssembly smoke checks:

```sh
npm run test:unit
npm run test:web
npm run test:capture
```

With the server running and Chrome installed, run the relevant browser checks:

```sh
npm run test:browser
npm run test:controls
npm run test:persistence
npm run test:offline
npm run test:offline-update
npm run test:clock
npm run test:save-pause
npm run test:calculator
npm run test:notes
```

Browser tests default to `/usr/bin/google-chrome`.
Set `CHROME_PATH` to choose another Chrome or Chromium executable.
The background-tab clock test also needs `DISPLAY` or Xvfb.
On Amazon Linux, install Xvfb with `sudo dnf install -y xorg-x11-server-Xvfb`.
Recording tests need Playwright's encoder: `npx playwright install ffmpeg`.

Keep generated reports, screenshots, recordings, and exports in ignored `.context/` paths.
Historical engineering reports formerly under `docs/traces/` remain available at
[commit 09c6897](https://github.com/kelindi/iphone-1/tree/09c68979f3df9f4e80b00c66cb4e2e8676e350fd/docs/traces).
Historical browser recordings and milestone publishing files no longer belong to the branch histories.
Retained engineering reports describe their original runs.
Their historical hashes can predate the archival cleanup.

## Repository guide

| Path | Purpose |
| --- | --- |
| `core/` | Portable ARMv6 CPU, memory, boot, and hardware models |
| `web/` | Browser interface, worker, WebAssembly API, saves, and offline cache |
| `tools/iphone1.c` | Native diagnostic tool |
| `tools/firmware/` | Firmware extraction and work-image tools |
| `tools/jit/` | Generated CPU code and isolated compiler experiments |
| `tools/legacy_services/` | Local app services and prepared-image experiments |
| `plans/` | Implementation history, feature plans, and remaining work |

## License

iphone-1 is free software under the GNU General Public License, version 2 or
(at your option) any later version; see [LICENSE](LICENSE).
The portable C core derives from [S5LBox](https://github.com/j0shua-SYSON/S5LBox),
whose MIT notice is kept in [third_party/S5LBox-LICENSE](third_party/S5LBox-LICENSE)
and in the affected source files.

The license covers this project's source code only.
Apple firmware, iPhone OS software, and the files derived from them remain Apple's property
and are not licensed by this project.
iPhone and iPhone OS are trademarks of Apple Inc. iphone-1 is not affiliated with or endorsed by Apple.

See [CONTRIBUTING.md](CONTRIBUTING.md) and [SECURITY.md](SECURITY.md).
