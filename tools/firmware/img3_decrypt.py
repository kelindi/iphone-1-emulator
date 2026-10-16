#!/usr/bin/env python3
"""Decrypt the DATA payload of an IMG3 container with a published IV/key.

usage: img3_decrypt.py IN.img3 OUT IV_HEX KEY_HEX

S5L8900 IMG3 images (iPhone OS 2.x/3.x) encrypt DATA with AES-CBC. The last
partial block is encrypted too: the cipher runs over the tag's padding, so
decrypt the length rounded up to 16 bytes and then truncate. Treating the
tail as plaintext corrupts the last bytes (the 7E18 kernelcache then
decompresses 42 bytes short with an Adler-32 mismatch).
"""
import struct
import sys

from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes


def tags(buf):
    """Yield (name, data, padded data) for each tag."""
    if buf[:4] != b"3gmI":
        sys.exit("not an IMG3 container")
    off = 20
    while off + 12 <= len(buf):
        magic, total, length = struct.unpack_from("<4sII", buf, off)
        body = buf[off + 12:off + total]
        yield magic[::-1].decode("latin-1"), body[:length], body
        if total < 12:
            break
        off += total


def main():
    if len(sys.argv) != 5:
        sys.exit(__doc__)
    src, dst, iv, key = sys.argv[1:]
    found = {name: (data, body) for name, data, body in tags(open(src, "rb").read())}
    if "DATA" not in found:
        sys.exit("no DATA tag")
    data, body = found["DATA"]
    span = (len(data) + 15) // 16 * 16
    if span > len(body):
        sys.exit("DATA tag has no room for its final padded block")
    dec = Cipher(algorithms.AES(bytes.fromhex(key)), modes.CBC(bytes.fromhex(iv))).decryptor()
    out = (dec.update(body[:span]) + dec.finalize())[:len(data)]
    open(dst, "wb").write(out)
    kind = found.get("TYPE", (b"", b""))[0][::-1]
    print(f"{src}: TYPE={kind!r} {len(out)} bytes, starts {out[:8]!r}")


if __name__ == "__main__":
    main()
