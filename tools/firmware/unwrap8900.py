#!/usr/bin/env python3
"""Unwrap an S5L8900 '8900' container (iPhone OS 1.x era).

Observed on iPhone1,1 1.0 (1A543a): every 8900 container in the IPSW and on
the root filesystem has format byte 0x04 and a PLAINTEXT payload at +0x800.
The payload is either an IMG2 ('Img2' little-endian magic, data at +0x400)
or a bare 'complzss' kernelcache.

    0x00  '8900'
    0x04  '1.0'
    0x07  format (0x03 / 0x04)
    0x0c  payload length (LE32)
    0x800 payload

Usage: unwrap8900.py <in> <out>
"""
import struct
import sys


def unwrap(data):
    assert data[:4] == b"8900", data[:4]
    fmt = data[7]
    size, = struct.unpack("<I", data[0xc:0x10])
    body = data[0x800:0x800 + size]
    if body[:4] == b"2gmI":
        typ = body[4:8][::-1].decode()
        pad_len, data_len = struct.unpack("<II", body[0x14:0x1c])
        return fmt, "img2:" + typ, body[0x400:0x400 + data_len]
    return fmt, body[:8].decode("latin1"), body


if __name__ == "__main__":
    data = open(sys.argv[1], "rb").read()
    fmt, kind, body = unwrap(data)
    print(f"8900 format={fmt} kind={kind} payload={len(body)} head={body[:16].hex()}")
    open(sys.argv[2], "wb").write(body)
