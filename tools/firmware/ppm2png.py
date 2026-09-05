"""Convert a captured guest framebuffer (P6 PPM) to PNG, and count pixels.

Stage 1 of the goal is a recognizable, guest-rendered SpringBoard frame, and
that claim cannot rest on a hash alone -- a hash proves the frame changed, not
that it shows anything. The frame has to be looked at. bootkernel writes P6
PPM; almost nothing displays PPM, so this converts it with the standard
library only (zlib + struct), keeping the check dependency-free.

The nonzero-pixel count is printed because it distinguishes the two failure
modes a thumbnail cannot: an all-black frame, and a frame with a handful of
stray pixels that still reads as black at a glance.

Usage:
  python ppm2png.py <in.ppm> <out.png>
"""
import struct
import sys
import zlib


def read_ppm(path):
    b = open(path, "rb").read()
    fields, off = [], 0
    # A P6 header is "P6 <width> <height> <maxval>" in any whitespace layout,
    # with '#' comments legal between tokens.
    while len(fields) < 4:
        while b[off:off + 1].isspace():
            off += 1
        if b[off:off + 1] == b"#":
            while b[off:off + 1] != b"\n":
                off += 1
            continue
        start = off
        while not b[off:off + 1].isspace():
            off += 1
        fields.append(b[start:off])
    if fields[0] != b"P6":
        raise SystemExit("not a P6 PPM: %r" % fields[0])
    off += 1  # exactly one whitespace byte follows maxval
    width, height = int(fields[1]), int(fields[2])
    return width, height, b[off:off + width * height * 3]


def chunk(tag, data):
    body = tag + data
    return (struct.pack(">I", len(data)) + body
            + struct.pack(">I", zlib.crc32(body) & 0xffffffff))


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    width, height, px = read_ppm(sys.argv[1])
    if len(px) != width * height * 3:
        raise SystemExit("truncated PPM: %d bytes for %dx%d"
                         % (len(px), width, height))

    # PNG wants a filter byte per scanline; 0 means "no filter".
    raw = b"".join(b"\x00" + px[y * width * 3:(y + 1) * width * 3]
                   for y in range(height))
    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR",
                   struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(raw, 9))
           + chunk(b"IEND", b""))
    open(sys.argv[2], "wb").write(png)

    nonzero = sum(1 for i in range(0, len(px), 3)
                  if px[i] or px[i + 1] or px[i + 2])
    print("%dx%d -> %s" % (width, height, sys.argv[2]))
    print("nonzero pixels: %d of %d (%.4f%%)"
          % (nonzero, width * height, 100.0 * nonzero / (width * height)))


main()
