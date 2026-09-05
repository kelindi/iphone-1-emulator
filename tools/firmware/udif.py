#!/usr/bin/env python3
"""Read an Apple UDIF (.dmg) and expand its blkx tables to a raw disk image.

Second of the two steps from an IPSW member to `firmware/rootfs.img`, after
`vfdecrypt.py` has removed the `encrcdsa` layer. See docs/BOOT_CHAIN.md.

Pass a blkx filter to extract one partition rather than the whole disk. That
matters for the 7E18 rootfs: the full image is 846,324 sectors including the
driver descriptor map, the Apple Partition Map, an ATAPI driver and free space,
but the emulator wants the filesystem alone. `Apple_HFSX` selects sectors
72..846312 -- 846,240 sectors, exactly 433,274,880 bytes.

Usage:
  python udif.py list <in.dmg>
  python udif.py extract <in.dmg> <out.raw> [blkx-filter]
"""
import binascii
import bz2
import plistlib
import struct
import sys
import zlib

KOLY = b"koly"
MISH = b"mish"

T_ZERO, T_RAW, T_IGNORE = 0x00000000, 0x00000001, 0x00000002
T_ADC, T_ZLIB, T_BZ2, T_LZFSE, T_LZMA = (0x80000004, 0x80000005, 0x80000006,
                                         0x80000007, 0x80000008)
T_COMMENT, T_END = 0x7FFFFFFE, 0xFFFFFFFF
NAMES = {T_ZERO: "ZERO", T_RAW: "RAW", T_IGNORE: "IGNORE", T_ADC: "ADC",
         T_ZLIB: "zlib", T_BZ2: "bzip2", T_LZFSE: "lzfse", T_LZMA: "lzma",
         T_COMMENT: "comment", T_END: "END"}


def read_koly(f, fsz):
    f.seek(fsz - 512)
    k = f.read(512)
    if k[:4] != KOLY:
        raise SystemExit("no koly trailer at EOF-512")
    return {
        "version": struct.unpack(">I", k[0x04:0x08])[0],
        "dataForkOffset": struct.unpack(">Q", k[0x18:0x20])[0],
        "dataForkLength": struct.unpack(">Q", k[0x20:0x28])[0],
        "xmlOffset": struct.unpack(">Q", k[0xD8:0xE0])[0],
        "xmlLength": struct.unpack(">Q", k[0xE0:0xE8])[0],
        "sectorCount": struct.unpack(">Q", k[0x1EC:0x1F4])[0],
    }


def parse_blkx(blob):
    if blob[:4] != MISH:
        raise SystemExit("blkx blob is not 'mish'")
    hdr = {
        "sectorNumber": struct.unpack(">Q", blob[0x08:0x10])[0],
        "sectorCount": struct.unpack(">Q", blob[0x10:0x18])[0],
        "dataOffset": struct.unpack(">Q", blob[0x18:0x20])[0],
        "nchunks": struct.unpack(">I", blob[0xC8:0xCC])[0],
    }
    chunks = []
    off = 0xCC
    for _ in range(hdr["nchunks"]):
        t, _c, sn, sc, co, cl = struct.unpack(">IIQQQQ", blob[off:off + 40])
        chunks.append((t, sn, sc, co, cl))
        off += 40
    hdr["chunks"] = chunks
    return hdr


def load(f, fsz):
    k = read_koly(f, fsz)
    f.seek(k["xmlOffset"])
    pl = plistlib.loads(f.read(k["xmlLength"]))
    entries = []
    for name, _attr, data in [(e["Name"], e.get("Attributes"), e["Data"])
                              for e in pl["resource-fork"]["blkx"]]:
        entries.append((name, parse_blkx(data)))
    return k, entries


def cmd_list(path):
    with open(path, "rb") as f:
        import os
        k, entries = load(f, os.path.getsize(path))
        print(k)
        total = 0
        for name, b in entries:
            kinds = {}
            for t, _sn, sc, _co, _cl in b["chunks"]:
                kinds[NAMES.get(t, hex(t))] = kinds.get(NAMES.get(t, hex(t)), 0) + sc
            end = b["sectorNumber"] + b["sectorCount"]
            total = max(total, end)
            print(f"\n  {name}")
            print(f"    sectors {b['sectorNumber']} .. {end}  "
                  f"(count {b['sectorCount']}, bytes {b['sectorCount']*512})")
            print(f"    chunks {b['nchunks']}  kinds(sectors) {kinds}")
        print(f"\n  highest sector covered: {total}  = {total*512} bytes")


def cmd_extract(path, outp, only=None):
    import os
    fsz = os.path.getsize(path)
    with open(path, "rb") as f:
        k, entries = load(f, fsz)
        dfo = k["dataForkOffset"]
        if only:
            entries = [(n, b) for n, b in entries if only in n]
            if not entries:
                raise SystemExit(f"no blkx matching {only!r}")
            base = min(b["sectorNumber"] for _n, b in entries)
            total_sectors = max(b["sectorNumber"] + b["sectorCount"]
                                for _n, b in entries) - base
        else:
            base = 0
            total_sectors = max(b["sectorNumber"] + b["sectorCount"]
                                for _n, b in entries)
        print(f"base sector {base}; expanding to {total_sectors} sectors "
              f"= {total_sectors*512} bytes")
        with open(outp, "wb") as o:
            o.truncate(total_sectors * 512)
            for name, b in entries:
                for t, sn, sc, co, cl in b["chunks"]:
                    if t in (T_END, T_COMMENT):
                        continue
                    abs_off = (b["sectorNumber"] + sn - base) * 512
                    want = sc * 512
                    if t in (T_ZERO, T_IGNORE):
                        o.seek(abs_off)
                        o.write(b"\x00" * want)
                        continue
                    f.seek(dfo + co)
                    raw = f.read(cl)
                    if t == T_RAW:
                        out = raw
                    elif t == T_ZLIB:
                        out = zlib.decompress(raw)
                    elif t == T_BZ2:
                        out = bz2.decompress(raw)
                    else:
                        raise SystemExit(f"unsupported chunk type {hex(t)} in {name}")
                    if len(out) != want:
                        raise SystemExit(
                            f"{name}: chunk at sector {sn} produced {len(out)} "
                            f"bytes, expected {want}")
                    o.seek(abs_off)
                    o.write(out)
                print(f"  done {name}")
    print(f"wrote {os.path.getsize(outp)} bytes -> {outp}")


if __name__ == "__main__":
    if sys.argv[1] == "list":
        cmd_list(sys.argv[2])
    else:
        cmd_extract(sys.argv[2], sys.argv[3],
                    sys.argv[4] if len(sys.argv) > 4 else None)
