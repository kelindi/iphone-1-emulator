"""Read-only HFS+/HFSX file extractor for a retained rootfs work image.

The project has never carried a userspace image/symbol map, which is why guest
PCs below 0x10000000 have stayed unresolved. This pulls one named file out of a
retained work image so its Mach-O can be disassembled.

It walks the catalog B-tree's leaf chain and matches on the node name rather
than implementing HFS key ordering, which avoids a whole class of comparison
bugs for a read-only lookup. Nothing here writes to the image.

Usage:
  python hfsx_extract.py <image> <name> [out]        # extract by leaf name
  python hfsx_extract.py <image> --list <substring>  # list matching names
"""
import struct
import sys

BE32 = ">I"
BE16 = ">H"
BE64 = ">Q"


def u16(b, o): return struct.unpack_from(BE16, b, o)[0]
def u32(b, o): return struct.unpack_from(BE32, b, o)[0]
def u64(b, o): return struct.unpack_from(BE64, b, o)[0]


class Fork:
    def __init__(self, buf, off):
        self.logical = u64(buf, off)
        self.total_blocks = u32(buf, off + 12)
        self.extents = []
        for i in range(8):
            start = u32(buf, off + 16 + i * 8)
            count = u32(buf, off + 16 + i * 8 + 4)
            if count:
                self.extents.append((start, count))

    def covered(self):
        return sum(c for _, c in self.extents)


class Volume:
    def __init__(self, path):
        self.f = open(path, "rb")
        self.f.seek(1024)
        vh = self.f.read(512)
        sig = vh[0:2]
        if sig not in (b"H+", b"HX"):
            raise SystemExit("not HFS+/HFSX: signature %r" % sig)
        self.block_size = u32(vh, 40)
        self.total_blocks = u32(vh, 44)
        self.catalog = Fork(vh, 272)
        if self.catalog.covered() != self.catalog.total_blocks:
            raise SystemExit(
                "catalog has extents-overflow spill (%d of %d blocks in the "
                "fork); not supported by this read-only helper"
                % (self.catalog.covered(), self.catalog.total_blocks))

    def read_fork(self, fork, length=None):
        want = fork.logical if length is None else length
        out = bytearray()
        for start, count in fork.extents:
            if len(out) >= want:
                break
            self.f.seek(start * self.block_size)
            out += self.f.read(count * self.block_size)
        return bytes(out[:want])


def leaf_records(node, node_size):
    kind = node[8]
    if kind != 0xFF:  # kBTLeafNode == -1
        return
    nrecs = u16(node, 10)
    for i in range(nrecs):
        off = u16(node, node_size - 2 * (i + 1))
        end = u16(node, node_size - 2 * (i + 2))
        if off >= end or end > node_size:
            continue
        yield node[off:end]


def parse_record(rec):
    key_len = u16(rec, 0)
    parent = u32(rec, 2)
    name_len = u16(rec, 6)
    name = rec[8:8 + name_len * 2].decode("utf-16-be", "replace")
    data_off = 2 + key_len
    if data_off & 1:
        data_off += 1
    if data_off + 2 > len(rec):
        return None
    rtype = u16(rec, data_off)
    return parent, name, rtype, data_off, rec


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    image, target = sys.argv[1], sys.argv[2]
    vol = Volume(image)
    cat = vol.read_fork(vol.catalog)

    node_size = u16(cat, 14 + 18)
    root = u32(cat, 14 + 2)
    first_leaf = u32(cat, 14 + 10)
    total_nodes = u32(cat, 14 + 22)
    sys.stderr.write(
        "catalog: block=%d nodes=%d node_size=%d root=%d first_leaf=%d\n"
        % (vol.block_size, total_nodes, node_size, root, first_leaf))

    listing = target == "--list"
    lsdir = target == "--ls"
    parent_filter = int(sys.argv[3]) if lsdir else None
    needle = (sys.argv[3] if listing else target)
    out_path = (sys.argv[3] if (not listing and not lsdir and
                                len(sys.argv) > 3) else None)
    # Optional 4th arg: parent CNID, to pick one of several same-named
    # files (e.g. a framework's symlink vs. its Versions/A binary).
    want_parent = (int(sys.argv[4]) if (not listing and not lsdir and
                                        len(sys.argv) > 4) else None)

    node_index = first_leaf
    seen = 0
    hits = []
    while node_index and seen <= total_nodes:
        seen += 1
        base = node_index * node_size
        node = cat[base:base + node_size]
        if len(node) < node_size:
            break
        for rec in leaf_records(node, node_size):
            parsed = parse_record(rec)
            if not parsed:
                continue
            parent, name, rtype, data_off, raw = parsed
            # A folder record carries its own CNID at +8, which is what a
            # child's parentID refers to; print it so a directory can be
            # walked without implementing key ordering.
            own_id = None
            if rtype == 1 and data_off + 12 <= len(raw):
                own_id = u32(raw, data_off + 8)
            if lsdir:
                if parent == parent_filter:
                    hits.append((own_id if own_id else parent, name, rtype))
                continue
            if listing:
                if needle.lower() in name.lower():
                    hits.append((own_id if own_id else parent, name, rtype))
                continue
            if name == needle and rtype == 2:  # kHFSPlusFileRecord
                if want_parent is not None and parent != want_parent:
                    continue
                fork = Fork(raw, data_off + 88)
                sys.stderr.write(
                    "found '%s' parent=%d size=%d blocks=%d extents=%s\n"
                    % (name, parent, fork.logical, fork.total_blocks,
                       fork.extents))
                if fork.covered() != fork.total_blocks:
                    sys.stderr.write("  extents-overflow spill; skipping\n")
                    continue
                data = vol.read_fork(fork)
                if out_path:
                    with open(out_path, "wb") as o:
                        o.write(data)
                    sys.stderr.write("wrote %d bytes to %s\n"
                                     % (len(data), out_path))
                else:
                    sys.stdout.write("%d bytes, magic %s\n"
                                     % (len(data), data[:4].hex()))
                return
        node_index = u32(node, 0)  # fLink

    if listing or lsdir:
        for ident, name, rtype in hits:
            kind = {1: "dir ", 2: "file"}.get(rtype, "t=%d" % rtype)
            print("id=%-8d %s  %s" % (ident, kind, name))
        print("(%d matches)" % len(hits))
    else:
        raise SystemExit("no file record named %r found" % needle)


if __name__ == "__main__":
    main()
