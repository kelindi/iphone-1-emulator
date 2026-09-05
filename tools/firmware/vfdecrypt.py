#!/usr/bin/env python3
"""Decrypt an Apple `encrcdsa` v2 ("vfdecrypt") disk image.

The root filesystem inside an iPhone OS 3.x IPSW is an encrypted DMG, so this
is the first of the two steps that turn a shipped IPSW member into the
`firmware/rootfs.img` the emulator accepts. `udif.py` is the second. Neither
existed when the firmware inputs were lost on 2026-07-26, which is why
regenerating them took a working afternoon instead of three commands; see
docs/BOOT_CHAIN.md for the whole sequence.

The key is supplied on the command line and is never stored here. It is
published per build and per device on The iPhone Wiki; you download your own,
for firmware you are entitled to use.

Layout, all big-endian, confirmed against the real 7E18 rootfs DMG and against
docs/activation.md:423-428:

    0x00  magic 'encrcdsa'
    0x08  version      = 2
    0x0c  encIvSize    = 16
    0x34  blockSize    = 4096
    0x38  dataSize     (u64)
    0x40  dataOffset   (u64)

Key blob is 36 bytes: 16-byte AES-128 key || 20-byte HMAC-SHA1 key.
Per block i: IV = HMAC-SHA1(hmac_key, BE32(i))[:16]; AES-128-CBC decrypt.

Usage: python vfdecrypt.py <in.dmg> <key-hex-72> <out.dmg>
"""
import hashlib
import hmac
import os
import struct
import sys

from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes


def main():
    inp, keyhex, outp = sys.argv[1], sys.argv[2], sys.argv[3]
    kb = bytes.fromhex(keyhex)
    if len(kb) != 36:
        sys.exit(f"key must be 36 bytes (72 hex chars), got {len(kb)}")
    aes_key, hmac_key = kb[:16], kb[16:]

    fsz = os.path.getsize(inp)
    with open(inp, "rb") as f:
        h = f.read(0x48)
        if h[:8] != b"encrcdsa":
            sys.exit(f"not an encrcdsa image: {h[:8]!r}")
        ver = struct.unpack(">I", h[8:12])[0]
        block_size = struct.unpack(">I", h[0x34:0x38])[0]
        data_size = struct.unpack(">Q", h[0x38:0x40])[0]
        data_off = struct.unpack(">Q", h[0x40:0x48])[0]
        print(f"version={ver} blockSize={block_size} "
              f"dataSize={data_size} dataOffset=0x{data_off:x} filesize={fsz}")

        span = fsz - data_off
        if span % block_size:
            sys.exit(f"payload span {span} is not a multiple of {block_size}")
        nblocks = span // block_size
        print(f"blocks={nblocks}, will truncate output to dataSize={data_size}")

        f.seek(data_off)
        written = 0
        with open(outp, "wb") as o:
            for i in range(nblocks):
                blk = f.read(block_size)
                if len(blk) != block_size:
                    sys.exit(f"short read at block {i}")
                iv = hmac.new(hmac_key, struct.pack(">I", i),
                              hashlib.sha1).digest()[:16]
                dec = Cipher(algorithms.AES(aes_key), modes.CBC(iv)).decryptor()
                plain = dec.update(blk) + dec.finalize()
                remain = data_size - written
                if remain <= 0:
                    break
                if len(plain) > remain:
                    plain = plain[:remain]
                o.write(plain)
                written += len(plain)
                if i % 5000 == 0:
                    print(f"  block {i}/{nblocks}", flush=True)
    print(f"wrote {written} bytes -> {outp}")


if __name__ == "__main__":
    main()
