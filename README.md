# iphone-1

An S5L8900 emulator whose goal is to run the genuine iPhone OS 1.0 (build
1A543a) on an emulated original iPhone (`iPhone1,1` / `M68AP`). Derived from
[S5LBox](https://github.com/j0shua-SYSON/S5LBox), with a separate M68 board
target and an OS 1.0 boot path.

Status and engineering log: [docs/iphone1_1_os1_port.md](docs/iphone1_1_os1_port.md).

## Firmware

No Apple firmware, binaries or keys are in this repository. You supply the
`iPhone1,1_1.0_1A543a_Restore.ipsw` and the build's root-filesystem key
locally:

```sh
python3 tools/firmware/extract_1a543a.py iPhone1,1_1.0_1A543a_Restore.ipsw fw/ --rootfs-key HEX
# -> fw/kernel_1A543a.macho  fw/devicetree_m68ap.bin  fw/rootfs_1A543a_hfs.img

python3 tools/firmware/rootfs_work.py fw/rootfs_1A543a_hfs.img fw/work_1A543a.img \
    --fstab "/dev/md0 / hfs rw 0 1" \
    --launchd-env com.apple.SpringBoard.plist=LK_ENABLE_MBX2D=0,LK_ENABLE_OGL=0 \
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
    --unmatch arm-io/usb-otg --unmatch arm-io/spi2/multi-touch \
    --steps 12000000000 --serial run.log --screenshot run.ppm --shot-every 500000000
python3 tools/firmware/ppm2png.py run.ppm.11.ppm home.png
```

The genuine 2007 home screen appears around 5.5G instructions (~3 min host
time); `--shot-every` writes `run.ppm.<n>.ppm` every N instructions. There is
no input yet, so SpringBoard eventually auto-locks and turns the display off.

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

The WebAssembly build and plain JavaScript frontend use the Emscripten SDK:

```sh
source ~/emsdk/emsdk_env.sh
emcmake cmake -S . -B web/build -DIPHONE1_WEB=ON -DCMAKE_BUILD_TYPE=Release
cmake --build web/build
python3 -m http.server 8080 --directory web
```

The `web/firmware/` directory contains the 1A543a kernel, device tree, and
compressed activated rootfs and home-screen snapshot assets. The manifest uses
the snapshot by default, avoiding the multi-minute cold boot; remove the
snapshot entry to load the rootfs and boot from scratch. Open
`http://127.0.0.1:8080/` after starting the server.
