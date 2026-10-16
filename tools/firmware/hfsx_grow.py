#!/usr/bin/env python3
"""Grow an HFS+/HFSX image so the guest can allocate on its root volume.

usage: hfsx_grow.py IMAGE MEGABYTES

Apple ships the iPhone OS system volume sized exactly to its contents
(freeBlocks == 0): on hardware `/` stays read-only and everything writable
lives on a second partition this emulator does not have. This edits IMAGE in
place, following Apple TN1150 and the procedure S5LBox documents in
docs/BOOTLOG.md ("The volume had zero free blocks"):

1. extend the file and raise volumeHeader.totalBlocks;
2. free the old last block, which held the alternate volume header and
   belongs to no file, and allocate the new last block for it;
3. recount freeBlocks from the bitmap instead of deriving it;
4. write the alternate volume header 1024 bytes before the new end.

The allocation file is not resized, so growth stops where its bitmap ends.
"""
import struct
import sys

VH_OFFSET = 1024


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    path, megabytes = sys.argv[1], int(sys.argv[2])
    with open(path, "r+b") as f:
        f.seek(VH_OFFSET)
        vh = bytearray(f.read(512))
        if vh[:2] not in (b"H+", b"HX"):
            sys.exit("not an HFS+/HFSX volume")
        block_size, total, free = struct.unpack_from(">III", vh, 40)
        f.seek(0, 2)
        if f.tell() != total * block_size:
            sys.exit("image size does not match totalBlocks; refusing")

        alloc_logical = struct.unpack_from(">Q", vh, 112)[0]
        extents = [struct.unpack_from(">II", vh, 112 + 16 + i * 8) for i in range(8)]
        extents = [e for e in extents if e[1]]
        bitmap = bytearray()
        for start, count in extents:
            f.seek(start * block_size)
            bitmap += f.read(count * block_size)
        bitmap = bitmap[:alloc_logical]
        if len(bitmap) != alloc_logical:
            sys.exit("allocation file extends past its fork extents; unsupported")

        grow = megabytes * 1024 * 1024 // block_size
        new_total = total + grow
        if grow <= 0:
            sys.exit("nothing to grow")
        if new_total > len(bitmap) * 8:
            sys.exit(f"bitmap holds {len(bitmap) * 8} blocks; at most "
                     f"{(len(bitmap) * 8 - total) * block_size >> 20} MB of growth")

        def set_bit(n, used):
            mask = 0x80 >> (n % 8)
            if used:
                bitmap[n // 8] |= mask
            else:
                bitmap[n // 8] &= ~mask & 0xFF

        for n in range(total, new_total):
            if bitmap[n // 8] & (0x80 >> (n % 8)):
                sys.exit("bitmap surplus bits are not zero; refusing")
        set_bit(total - 1, False)       # old alternate-header block
        set_bit(new_total - 1, True)    # new alternate-header block

        new_free = sum(1 for n in range(new_total)
                       if not bitmap[n // 8] & (0x80 >> (n % 8)))
        struct.pack_into(">II", vh, 44, new_total, new_free)

        # Extend, clear the old alternate header, then write the new copies.
        f.truncate(new_total * block_size)
        f.seek(total * block_size - 1024)
        f.write(b"\0" * 512)
        offset = 0
        for start, count in extents:
            chunk = bitmap[offset:offset + count * block_size]
            f.seek(start * block_size)
            f.write(chunk)
            offset += count * block_size
        f.seek(VH_OFFSET)
        f.write(vh)
        f.seek(new_total * block_size - 1024)
        f.write(vh)
    print(f"{path}: totalBlocks {total} -> {new_total}, "
          f"freeBlocks {free} -> {new_free} ({new_free * block_size >> 20} MB free)")


if __name__ == "__main__":
    main()
