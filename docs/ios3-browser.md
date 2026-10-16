# iPhone OS 3.1.3 in the browser

The browser can run iPhone OS 3.1.3 (7E18) on the original iPhone (M68)
alongside the published 1.0 phone. The 3.1.3 firmware is prepared locally
from your own IPSW. It is Apple software and is never committed or published.

## Prepare the firmware

Requirements: the native build (`build/tools/iphone1`, see the README or
`docs/windows-build.md`), the firmware tool requirements
(`pip install -r tools/firmware/requirements.txt`), and this build's
published firmware keys as JSON. The keys must cover the kernelcache,
`DeviceTree.m68ap.img3` and the root filesystem DMG.

```sh
python3 tools/firmware/prepare_ios3_web.py iPhone1,1_3.1.3_7E18_Restore.ipsw \
    web/firmware/7E18 --keys keys_7E18.json --iphone1 build/tools/iphone1
```

The script decrypts the kernel and device tree, then decrypts, grows and
provisions the root volume. The steps are those in `plans/ios3-m68.md`, plus
"never auto-lock" for the mobile user and a provisioned lockdownd device key
pair. It then boots the volume natively, slides to unlock, dismisses the
first-run alert, and waits on the home screen until the first-boot Security
work is over (`--steps`, default 7 G instructions, about 4.5 minutes). It
writes a manifest describing the paired snapshot and disk:

| File | Size |
| --- | --- |
| `kernel_7E18.macho` | 7.9 MB |
| `devicetree_m68ap.bin` | 38 KB |
| `disk_7E18.img` | 534 MB |
| `home_7E18.snap` | 115 MB |
| `manifest.json` | `"kind": "ios3"` |

Without `--iphone1`, there is no snapshot and the page cold boots.
`web/firmware/*/` is ignored by git.

## Use it

Serve `web/` (`npm run serve`) and open the page. The **Firmware** picker
under the phone lists every build in `web/firmwares.json` whose manifest this
server has, and switches with `?firmware=7E18`. When no local firmware
exists, the picker stays hidden. The page without the parameter is
unchanged: the 1.0 phone, its browser saves and offline visits.

Measured locally (Chrome, Windows 11): ready 3.3 s after navigation, about
25 M instructions/s, 873 MiB WASM heap.

First-boot guest work matters more than host speed here. Without provisioned
keys, lockdownd generates its device RSA pair in Security's bignum code,
which took 51-95% of guest instructions until about 5.3-6.0 G instructions.
A snapshot taken before that point makes every visit repeat it. With the keys
provisioned and the snapshot taken after the remaining bignum work (none
after ~5.9 G), a Calculator launch from the home screen takes 0.29 G guest
instructions, against 0.39 G from a snapshot taken during that work.

The second lever is compositing. iPhone OS 3's AppleMBX attaches to the
core's PowerVR MBX model ("Detected mbx,s5l8900x", AppleH1CLCD swap device),
so `arm-io/mbx` stays matched and SpringBoard keeps Apple's MBX2D path
instead of `CA_ENABLE_MBX2D=0` software rendering. Calculator launch:

| Compositing | Guest instructions to a settled Calculator | At ~25 MIPS |
| --- | --- | --- |
| Core Animation software renderer | 0.29 G | ~12 s |
| MBX (default) | <=0.09 G (82% of the final frame at 0.04 G) | <=4 s |

The traced launch completed 133/133 2D packets and 355/355 3D renders, with
none rejected. The final frame matches the software-rendered one. Slide to unlock,
taps, app launch (Calculator) and the Home button work. The guest runs on
the interpreter only.

## Current limits

- **No browser saves.** The root volume lives outside guest RAM, behind the
  memory-disk bridge, and checkpoints capture RAM only. Save, Export and
  Import stay off, and the page never takes the 1.0 profile. Every visit
  starts again from the shipped snapshot.
- **Own seed only.** The 1.0 seed is never used for 3.1.3. With
  `prepare_ios3_web.py --seed`, the firmware gets its own seed, recorded from
  a 3.1.3 app workload (see `tools/jit/README.md`). 510 regions run it with a
  machine and disk equal to the interpreter's over 600 M instructions.
  Steady-state WASM time for the Calculator launch window is 4.34 s on the
  interpreter and 3.82 s with the seed (1.13x). Kernel code runs
  interpreted. iPhone OS 3's memory-disk SVC handler limits batching, and so
  the seed, to User mode, as S5LBox does. Before that rule, the handler had
  switched batching off entirely, so no seed region could ever run.
- **No offline package** for local firmware, and no network or Wi-Fi in the
  guest.
- The disk is streamed into the WASM heap without Cache Storage or SHA-256
  checks, so it only loads from this site.
