#!/usr/bin/env python3
"""Compact a clean, non-journaled HFSX work-image COPY without deleting files.

This deliberately supports only the audited 1A543a work-image layout: 4 KiB
blocks, inline extents, empty overflow/attributes trees, no startup fork.
Mounted checkpoint RAM disks are not valid inputs. Never overwrites a path.

Usage: hfsx_compact.py INPUT OUTPUT --size-mib 168
"""
import argparse
import hashlib
import pathlib
import struct

import hfsx_extract as H


class UnsupportedImage(ValueError):
    pass


def require(condition, message):
    if not condition:
        raise UnsupportedImage(message)


def validate_empty_attributes(data):
    """An initialized but empty attributes B-tree has no embedded file extents.

    Preserve its complete fork; accepting nonempty attributes would require
    decoding and relocating their possible out-of-line extent records first.
    The original OS initializes this header/map after mounting a work image.
    """
    require(len(data) >= 512 and data[8] == 1 and data[9] == 0
            and H.u16(data, 10) == 3 and H.u32(data, 0) == 0
            and H.u32(data, 4) == 0, "unsupported attributes header/map chain")
    size = H.u16(data, 32)
    require(size >= 512 and size & (size - 1) == 0 and len(data) % size == 0,
            "invalid attributes node size")
    offsets = [H.u16(data, size - 2 * (i + 1)) for i in range(4)]
    require(offsets[0] == 14 and offsets[1] - offsets[0] >= 106
            and offsets[1] < offsets[2] < offsets[3] <= size - 8,
            "invalid attributes header record bounds")
    require(H.u16(data, 14) == 0 and all(H.u32(data, off) == 0
            for off in (16, 20, 24, 28)), "nonempty attributes tree")
    total = H.u32(data, 36)
    require(total == len(data) // size and H.u32(data, 40) == total - 1,
            "attributes node accounting is not empty")
    bitmap = data[offsets[2]:offsets[3]]
    require(len(bitmap) * 8 >= total, "short attributes node map")
    allocated = {i for i in range(total) if bitmap[i // 8] & (0x80 >> (i % 8))}
    require(allocated == {0}, "attributes node map is not empty")


def compact(source, target_bytes):
    """Return validated image bytes and a report; source bytes stay unchanged."""
    require(len(source) >= 4096, "truncated volume")
    header = bytearray(source[1024:1536])
    require(header[:2] == b"HX" and H.u16(header, 2) == 5, "requires HFSX version 5")
    flags = H.u32(header, 4)
    require(flags & 0x100 and not flags & 0x2000 and H.u32(header, 12) == 0,
            "requires clean, unmounted, non-journaled volume")
    block_size, total = H.u32(header, 40), H.u32(header, 44)
    require(block_size == 4096, "requires 4 KiB allocation blocks")
    require(len(source) == total * block_size, "volume/image size mismatch")
    require(0 < target_bytes < len(source) and target_bytes % block_size == 0,
            "target must be a smaller positive block-aligned size")
    # Alternate headers may be older; use the primary header only after verifying
    # that the trailer describes the same volume geometry and identity.
    alternate = source[-1024:-512]
    require(alternate[:2] == header[:2] and alternate[40:48] == header[40:48]
            and alternate[80:112] == header[80:112], "alternate header mismatch")
    owned, forks = set(), []

    def read_fork(fork):
        result = b"".join(source[s * block_size:(s + n) * block_size]
                          for s, n in fork.extents)[:fork.logical]
        require(len(result) == fork.logical, "truncated fork")
        return result

    def add_fork(raw, offset, name):
        require(offset + 80 <= len(raw), "truncated fork descriptor")
        fork = H.Fork(raw, offset)
        require(fork.covered() == fork.total_blocks, f"overflow extents: {name}")
        require(fork.logical <= fork.total_blocks * block_size, f"short extents: {name}")
        ended = False
        for index in range(8):
            start, count = struct.unpack_from(">II", raw, offset + 16 + index * 8)
            if count == 0:
                require(start == 0, f"nonzero empty extent: {name}")
                ended = True
                continue
            require(not ended, f"non-contiguous extent descriptors: {name}")
            require(0 < start and start + count < total, f"extent outside volume: {name}")
            span = set(range(start, start + count))
            require(not span & owned, f"overlapping allocation: {name}")
            owned.update(span)
        forks.append((raw, offset, fork, name, hashlib.sha256(read_fork(fork)).digest()))
        return fork

    special = {}
    for name, offset in (("allocation", 112), ("extents", 192), ("catalog", 272),
                         ("attributes", 352), ("startup", 432)):
        special[name] = add_fork(header, offset, name)
    require(not special["startup"].total_blocks, "startup fork is unsupported")
    if special["attributes"].total_blocks:
        validate_empty_attributes(read_fork(special["attributes"]))
    overflow = read_fork(special["extents"])
    require(len(overflow) >= 40 and overflow[8] == 1 and H.u32(overflow, 24) == 0
            and H.u32(overflow, 20) == 0, "nonempty extents-overflow tree")
    catalog = bytearray(read_fork(special["catalog"]))
    require(len(catalog) >= 40 and catalog[8] == 1, "invalid catalog header")
    node_size = H.u16(catalog, 32)
    require(node_size >= 512 and node_size & (node_size - 1) == 0
            and len(catalog) % node_size == 0, "invalid catalog node size")
    leaf, previous, visited, records = H.u32(catalog, 24), 0, set(), 0
    while leaf:
        require(leaf not in visited, "catalog leaf cycle")
        visited.add(leaf)
        base = leaf * node_size
        node = catalog[base:base + node_size]
        require(len(node) == node_size and node[8] == 0xff and H.u32(node, 4) == previous,
                "invalid catalog leaf chain")
        count = H.u16(node, 10)
        table = node_size - 2 * (count + 1)
        require(table >= 14, "invalid catalog offset table")
        for index in range(count):
            start = H.u16(node, node_size - 2 * (index + 1))
            end = H.u16(node, node_size - 2 * (index + 2))
            require(14 <= start < end <= table and end - start >= 10,
                    "invalid catalog record bounds")
            raw = node[start:end]
            key_len, name_len = H.u16(raw, 0), H.u16(raw, 6)
            require(key_len == 6 + name_len * 2 and key_len + 4 <= len(raw),
                    "invalid catalog key")
            record = H.parse_record(raw)
            require(record is not None and record[2] in (1, 2, 3, 4),
                    "unsupported catalog record")
            if record[2] == 2:
                require(record[3] + 248 <= len(raw), "truncated catalog file record")
                for delta, label in ((88, "data"), (168, "resource")):
                    add_fork(catalog, base + start + record[3] + delta,
                             f"{record[0]}/{record[1]} {label}")
            records += 1
        previous, leaf = leaf, H.u32(node, 0)
    require(previous == H.u32(catalog, 28) and records == H.u32(catalog, 20),
            "incomplete catalog leaf chain")
    bitmap = read_fork(special["allocation"])
    require(len(bitmap) * 8 >= total, "short allocation bitmap")
    allocated = {i for i in range(total) if bitmap[i // 8] & (0x80 >> (i % 8))}
    require(allocated == owned | {0, total - 1}, "unowned or unallocated blocks")
    require(total - len(allocated) == H.u32(header, 48), "free-block count mismatch")
    new_total = target_bytes // block_size
    require(new_total - len(allocated) >= 1024, "target must leave at least 4 MiB free")
    order = [0] + sorted(owned)
    mapping = {old: new for new, old in enumerate(order)}
    mapping[total - 1] = new_total - 1
    output = bytearray(target_bytes)
    for old, new in mapping.items():
        output[new * block_size:(new + 1) * block_size] = source[
            old * block_size:(old + 1) * block_size]
    for raw, offset, fork, name, digest in forks:
        for index, (start, count) in enumerate(fork.extents):
            require(mapping[start + count - 1] == mapping[start] + count - 1,
                    f"non-contiguous relocated extent: {name}")
            struct.pack_into(">I", raw, offset + 16 + index * 8, mapping[start])

    def write_fork(fork, data):
        pos = 0
        for start, count in fork.extents:
            take = min(len(data) - pos, count * block_size)
            if take <= 0:
                break
            output[start * block_size:start * block_size + take] = data[pos:pos + take]
            pos += take
        require(pos == len(data), "short output fork")

    write_fork(H.Fork(header, 272), catalog)
    new_bitmap = bytearray(special["allocation"].logical)
    for index in [*range(len(order)), new_total - 1]:
        new_bitmap[index // 8] |= 0x80 >> (index % 8)
    write_fork(H.Fork(header, 112), new_bitmap)
    struct.pack_into(">III", header, 44, new_total, new_total - len(allocated), len(order))
    output[1024:1536] = header
    output[-1024:-512] = header
    for raw, offset, fork, name, digest in forks:
        if raw is header and name in ("allocation", "catalog"):
            continue
        relocated = H.Fork(raw, offset)
        data = b"".join(output[s * block_size:(s + n) * block_size]
                        for s, n in relocated.extents)[:relocated.logical]
        require(hashlib.sha256(data).digest() == digest, f"content changed: {name}")
    return output, {"verified_file_forks": len(forks) - 5,
                    "source_bytes": len(source), "output_bytes": len(output),
                    "free_bytes": (new_total - len(allocated)) * block_size}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=pathlib.Path)
    parser.add_argument("destination", type=pathlib.Path)
    parser.add_argument("--size-mib", type=int, required=True)
    args = parser.parse_args()
    try:
        require(not args.destination.exists(), "destination already exists")
        output, report = compact(args.source.read_bytes(), args.size_mib * 1024 * 1024)
        with args.destination.open("xb") as stream:
            stream.write(output)
    except (UnsupportedImage, OSError) as error:
        parser.exit(1, f"compaction refused: {error}\n")
    print(report)


if __name__ == "__main__":
    main()
