#!/usr/bin/env python3
"""Python port of the repo's tools/unlzss.c (core/src/firmware/lzss.c).

Expands an Apple "complzss" kernelcache to a raw Mach-O. Used here because
this Windows machine has no C compiler to build the native tool. Mirrors the C
version, including zero-filling a short stream to the declared size.
"""
import struct
import sys

N, F, THRESHOLD, HEADER = 4096, 18, 2, 0x180


def decompress(src, cap):
    ring = bytearray(b" " * (N - F) + b"\0" * (F + F - 1))
    out = bytearray()
    r, flags, i, n = N - F, 0, 0, len(src)
    while True:
        flags >>= 1
        if not flags & 0x100:
            if i >= n:
                break
            flags = src[i] | 0xFF00
            i += 1
        if flags & 1:
            if i >= n or len(out) >= cap:
                break
            c = src[i]
            i += 1
            out.append(c)
            ring[r] = c
            r = (r + 1) & (N - 1)
        else:
            if i + 1 >= n:
                break
            off = src[i] | ((src[i + 1] & 0xF0) << 4)
            length = (src[i + 1] & 0x0F) + THRESHOLD
            i += 2
            for k in range(length + 1):
                if len(out) >= cap:
                    return out
                c = ring[(off + k) & (N - 1)]
                out.append(c)
                ring[r] = c
                r = (r + 1) & (N - 1)
    return out


def adler32(data):
    a, b = 1, 0
    for x in data:
        a = (a + x) % 65521
        b = (b + a) % 65521
    return (b << 16) | a


def main():
    if len(sys.argv) < 3:
        sys.exit("usage: unlzss.py <complzss.bin> <out.macho>")
    buf = open(sys.argv[1], "rb").read()
    if buf[:8] != b"complzss" or len(buf) < HEADER:
        sys.exit("not a complzss blob")
    adler, unc, cmp = struct.unpack(">III", buf[8:20])
    if HEADER + cmp > len(buf):
        sys.exit("declared compressed size exceeds the file")
    out = decompress(buf[HEADER:HEADER + cmp], unc)
    print(f"uncompressed : {unc} bytes\ncompressed   : {cmp} bytes")
    print(f"produced     : {len(out)} bytes")
    a = adler32(out)
    print(f"adler32 check: 0x{a:08x} {'MATCHES' if a == adler else 'MISMATCH'}")
    if len(out) < unc:
        print(f"NOTE         : {unc - len(out)} bytes short; zero-filling (known upstream discrepancy)")
        out += b"\0" * (unc - len(out))
    open(sys.argv[2], "wb").write(out)
    print("wrote        :", sys.argv[2])


if __name__ == "__main__":
    main()
