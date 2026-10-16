#!/usr/bin/env python3
"""Prepare iPhone OS 3.1.3 (7E18) for the browser from your own IPSW.

usage:
  prepare_ios3_web.py IPSW OUTDIR --keys KEYS.json [--iphone1 PATH] [--steps N]
                      [--seed]

IPSW     iPhone1,1_3.1.3_7E18_Restore.ipsw
OUTDIR   normally web/firmware/7E18 (ignored by git; never publish it)
KEYS     this build's published firmware keys as JSON, either {"keys": [...]}
         or a bare list of {"image", "filename", "iv", "key"} records, the
         format used by community key archives. Keys are not hardcoded here.
--iphone1  native tools/iphone1 build. With it, the script boots the prepared
           volume, slides to unlock, dismisses SpringBoard's first-run alert
           and waits for the first-boot Security bignum work to finish before
           saving, so the browser starts on the home screen in seconds and
           never repeats that work. Without it, the browser cold boots (about
           3 G instructions to the lock screen).
--seed     also record this firmware's executed code during a scripted app
           workload from the snapshot (tools/iphone1 --record-ops) and compile
           the browser's guarded seed for it into OUTDIR/jit (needs node and
           the built web/dist runtime). Without it the page runs 3.1.3 on
           the interpreter only.

Steps, each from a measured failure (see plans/ios3-m68.md):
  1. decrypt the kernelcache and M68 device tree (IMG3, final block included);
  2. decrypt and extract the root volume;
  3. grow it by 96 MB, rewrite fstab to /dev/md0 and seed lockdownd's
     FactoryActivated record (SpringBoard keeps Apple's MBX2D compositing:
     the boot matches arm-io/mbx);
  4. disable SpringBoard's auto-lock and dim for the mobile user, and
     provision lockdownd's device RSA key pair (it otherwise generates one in
     Security's bignum code, 51-95% of guest instructions for >2 G);
  5. optionally boot it natively and keep the paired snapshot and disk.
"""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import img3_decrypt  # noqa: E402
import unlzss  # noqa: E402

KERNEL_SHA256 = "f36a88d611d3b906ae858f377e21853b40b214b2bea99cb2f988e380698e6ce9"
KERNEL_MEMBER = "kernelcache.release.s5l8900x"
DT_MEMBER = "Firmware/all_flash/all_flash.m68ap.production/DeviceTree.m68ap.img3"
FSTAB = "/dev/md0 / hfs rw,update 0 0"


def find_key(keys, name):
    for record in keys:
        if record.get("image") == name or record.get("filename") == name:
            return record
    raise SystemExit(f"no key for {name} in the keys file")


def decrypt_img3(data, record):
    tags = {n: (d, b) for n, d, b in img3_decrypt.tags(data)}
    payload, body = tags["DATA"]
    span = (len(payload) + 15) // 16 * 16
    from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
    dec = Cipher(algorithms.AES(bytes.fromhex(record["key"])),
                 modes.CBC(bytes.fromhex(record["iv"]))).decryptor()
    return (dec.update(body[:span]) + dec.finalize())[:len(payload)]


def kernel_from(complzss):
    import struct
    adler, size, cmp = struct.unpack(">III", complzss[8:20])
    out = unlzss.decompress(complzss[0x180:0x180 + cmp], size)
    if len(out) != size or unlzss.adler32(out) != adler:
        raise SystemExit("kernelcache did not decompress cleanly")
    return bytes(out)


def run(*args):
    subprocess.run([str(a) for a in args], check=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("ipsw", type=Path)
    ap.add_argument("outdir", type=Path)
    ap.add_argument("--keys", type=Path, required=True)
    ap.add_argument("--iphone1", type=Path)
    ap.add_argument("--steps", type=int, default=7_000_000_000)
    ap.add_argument("--seed", action="store_true")
    a = ap.parse_args()

    keys = json.loads(a.keys.read_text())
    keys = keys["keys"] if isinstance(keys, dict) else keys
    out = a.outdir
    work = out / "work"
    work.mkdir(parents=True, exist_ok=True)

    with zipfile.ZipFile(a.ipsw) as z:
        kc = z.read(KERNEL_MEMBER)
        dt = z.read(DT_MEMBER)
        dmg = max((i for i in z.infolist() if i.filename.endswith(".dmg")),
                  key=lambda i: i.file_size)
        z.extract(dmg, work)

    kernel = kernel_from(decrypt_img3(kc, find_key(keys, KERNEL_MEMBER)))
    if hashlib.sha256(kernel).hexdigest() != KERNEL_SHA256:
        raise SystemExit("this is not the iPhone1,1 7E18 kernel the gate accepts")
    (out / "kernel_7E18.macho").write_bytes(kernel)
    (out / "devicetree_m68ap.bin").write_bytes(
        decrypt_img3(dt, find_key(keys, "DeviceTree.m68ap.img3")))
    print("kernel and device tree ready")

    rootfs_key = find_key(keys, Path(dmg.filename).name)["key"]
    py = sys.executable
    run(py, HERE / "vfdecrypt.py", work / dmg.filename, rootfs_key, work / "rootfs.dmg")
    run(py, HERE / "udif.py", "extract", work / "rootfs.dmg", work / "rootfs.img", "Apple_HFSX")
    run(py, HERE / "hfsx_grow.py", work / "rootfs.img", "96")
    disk = out / "disk_7E18.img"
    run(py, HERE / "rootfs_work.py", work / "rootfs.img", disk,
        "--fstab", FSTAB,
        "--no-autolock-mobile", "--device-keys",
        "--add", "/private/var/root/Library/Lockdown/data_ark.plist=" +
        str(HERE / "data_ark_activated_3x.plist"))
    shutil.rmtree(work)
    work.mkdir()
    print("root volume ready")

    manifest = {
        "kind": "ios3",
        "name": "iPhone OS 3.1.3 (7E18)",
        "kernel": {"path": "kernel_7E18.macho", "size": len(kernel)},
        "devicetree": {"path": "devicetree_m68ap.bin",
                       "size": (out / "devicetree_m68ap.bin").stat().st_size},
        "disk": {"path": disk.name, "size": disk.stat().st_size},
    }
    if a.iphone1:
        snap = out / "home_7E18.snap"
        # --rootfs is written in place, so the disk shipped is exactly the
        # one the snapshot was taken against.
        # The lock screen is drawn by ~3.0 G instructions and sleeps by
        # itself around 4.5-5.0 G, so unlock at 3.4 G while it is lit. Dismiss
        # the "Edit Home Screen" alert, then let the remaining first-boot
        # Security bignum work finish on the home screen (measured: none after
        # ~5.9 G with provisioned device keys) and save at 7 G.
        gesture = ["--at", "3400000000", "touch", "down", "50", "430"]
        for i in range(1, 8):
            gesture += ["--at", str(3_400_000_000 + i * 5_000_000),
                        "touch", "move", str(50 + i * 36), "430"]
        gesture += ["--at", "3440000000", "touch", "up", "302", "430",
                    "--at", "5000000000", "tap", "160", "332"]
        run(a.iphone1, "--board", "m68", "--ios3",
            "--kernel", out / "kernel_7E18.macho",
            "--devicetree", out / "devicetree_m68ap.bin",
            "--rootfs", disk, "--no-spin-stop", *gesture,
            "--screenshot", out / "home_check.ppm",
            "--save-at", a.steps, snap, "--steps", a.steps + 1)
        manifest["snapshot"] = {"path": snap.name, "size": snap.stat().st_size}
        manifest["disk"]["size"] = disk.stat().st_size
    if a.seed:
        if not a.iphone1:
            raise SystemExit("--seed needs --iphone1")
        # Open Calculator, Home, swipe the home screen, Settings, Notes and
        # Clock, each followed by Home, from the shipped snapshot. The guest
        # writes to a scratch copy, so the shipped disk stays paired.
        t = a.steps
        events = [(10_000_000, ["tap", "268", "248"]), (300_000_000, ["press", "menu"])]
        events.append((520_000_000, ["touch", "down", "40", "240"]))
        for i in range(1, 7):
            events.append((520_000_000 + i * 6_000_000,
                           ["touch", "move", str(40 + i * 40), "240"]))
        events += [(560_000_000, ["touch", "up", "280", "240"]),
                   (800_000_000, ["press", "menu"]),
                   (1_000_000_000, ["tap", "40", "330"]), (1_350_000_000, ["press", "menu"]),
                   (1_550_000_000, ["tap", "122", "248"]), (1_900_000_000, ["press", "menu"]),
                   (2_100_000_000, ["tap", "195", "248"]), (2_400_000_000, ["press", "menu"])]
        script = []
        for offset, action in events:
            script += ["--at", str(t + offset), *action]
        scratch = work / "record.img"
        shutil.copyfile(disk, scratch)
        recording = work / "seed-input.json"
        run(a.iphone1, "--board", "m68", "--ios3",
            "--kernel", out / "kernel_7E18.macho",
            "--devicetree", out / "devicetree_m68ap.bin",
            "--rootfs", scratch, "--load", out / "home_7E18.snap",
            "--no-spin-stop", "--record-ops", recording, "--record-from", t,
            *script, "--steps", t + 2_600_000_000)
        import gzip
        (out / "seed-input.json.gz").write_bytes(gzip.compress(recording.read_bytes()))
        env = dict(os.environ, SEED_INPUT=str((out / "seed-input.json.gz").resolve()),
                   JIT_OUTPUT=str((out / "jit").resolve()))
        subprocess.run(["node", str(HERE.parent / "jit" / "generate-seed.mjs")],
                       check=True, env=env)
        manifest["seed"] = "jit/"
    shutil.rmtree(work, ignore_errors=True)
    manifest["downloadBytes"] = sum(e["size"] for k, e in manifest.items()
                                    if isinstance(e, dict))
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"wrote {out / 'manifest.json'}")


if __name__ == "__main__":
    main()
