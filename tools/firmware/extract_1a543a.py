#!/usr/bin/env python3
"""Extract the iPhone1,1 1A543a firmware artifacts from an IPSW."""
import argparse
import hashlib
import re
import subprocess
import sys
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from unwrap8900 import unwrap


def rootfs_key(args):
    if args.rootfs_key:
        return args.rootfs_key
    if not args.keys_file:
        raise SystemExit("rootfs key required (--rootfs-key or --keys-file)")
    for line in Path(args.keys_file).read_text().splitlines():
        if "RootFSKey" in line:
            m = re.search(r"RootFSKey[^0-9a-fA-F]*([0-9a-fA-F]{72})", line)
            if m:
                return m.group(1)
    raise SystemExit("no matching RootFSKey line found")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("ipsw", type=Path)
    ap.add_argument("outdir", type=Path)
    ap.add_argument("--rootfs-key")
    ap.add_argument("--keys-file", type=Path)
    ap.add_argument("--unlzss", type=Path)
    a = ap.parse_args()
    a.outdir.mkdir(parents=True, exist_ok=True)
    ipsw = a.outdir / "ipsw"
    ipsw.mkdir(exist_ok=True)
    with zipfile.ZipFile(a.ipsw) as z:
        z.extractall(ipsw)

    ksrc = next(ipsw.rglob("kernelcache.restore.release.s5l8900xrb"), None)
    if not ksrc:
        raise SystemExit("kernelcache.restore.release.s5l8900xrb not found")
    _fmt, _kind, compressed = unwrap(ksrc.read_bytes())
    kc = a.outdir / "kernelcache.complzss"
    kc.write_bytes(compressed)
    unlzss = a.unlzss
    if unlzss is None:
        candidate = a.outdir.parent / "build" / "tools" / "unlzss"
        if candidate.exists():
            unlzss = candidate
    if unlzss is None:
        candidate = HERE.parent.parent / "build" / "tools" / "unlzss"
        if candidate.exists():
            unlzss = candidate
    macho = a.outdir / "kernel.macho"
    if unlzss:
        subprocess.run([str(unlzss), str(kc), str(macho)], check=True)
    else:
        print(f"run: unlzss {kc} > {macho}")

    dsrc = next(ipsw.rglob("DeviceTree.m68ap.img2"), None)
    if not dsrc:
        raise SystemExit("DeviceTree.m68ap.img2 not found")
    _fmt, _kind, dt = unwrap(dsrc.read_bytes())
    (a.outdir / "devicetree.bin").write_bytes(dt)

    dmgs = list(ipsw.rglob("*.dmg"))
    if not dmgs:
        raise SystemExit("no DMG found")
    encrypted = max(dmgs, key=lambda p: p.stat().st_size)
    decrypted = a.outdir / "rootfs.dmg"
    subprocess.run([sys.executable, str(HERE / "vfdecrypt.py"),
                    str(encrypted), rootfs_key(a), str(decrypted)], check=True)
    subprocess.run([sys.executable, str(HERE / "udif.py"), "extract",
                    str(decrypted), str(a.outdir / "rootfs.img"), "Apple_HFSX"],
                   check=True)
    print("extracted firmware to", a.outdir)


if __name__ == "__main__":
    main()
