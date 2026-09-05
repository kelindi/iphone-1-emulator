#!/usr/bin/env python3
import struct, sys
b = open(sys.argv[1], "rb").read()
def s(v):
    t = v.rstrip(b"\0")
    if t and all(32 <= c < 127 for c in t): return repr(t.decode())
    if len(v) % 4 == 0 and len(v) <= 32:
        return " ".join("0x%08x" % x for x in struct.unpack("<%dI" % (len(v)//4), v))
    return "<%d bytes> %s" % (len(v), v[:16].hex())
def walk(off, depth):
    np, nc = struct.unpack("<II", b[off:off+8]); off += 8
    name = "?"; props = []
    for _ in range(np):
        pn = b[off:off+32].rstrip(b"\0").decode(); ln, = struct.unpack("<I", b[off+32:off+36]); ln &= 0x7fffffff
        val = b[off+36:off+36+ln]; off += 36 + ((ln + 3) & ~3)
        if pn == "name": name = val.rstrip(b"\0").decode()
        else: props.append((pn, val))
    print("  " * depth + name)
    for pn, val in props: print("  " * depth + "    %-24s %s" % (pn, s(val)))
    for _ in range(nc): off = walk(off, depth + 1)
    return off
walk(0, 0)
