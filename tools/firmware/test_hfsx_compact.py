"""Synthetic, redistributable HFSX fixtures for the bounded compactor."""
import hashlib
import struct
import unittest

import hfsx_compact as C
import hfsx_extract as H

BS = 4096


def fork(buf, off, logical, extents):
    struct.pack_into(">QII", buf, off, logical, 0, sum(n for _, n in extents))
    for i, extent in enumerate(extents):
        struct.pack_into(">II", buf, off + 16 + i * 8, *extent)


def fixture():
    image = bytearray(8 * 1024 * 1024)
    header = bytearray(512)
    header[:2] = b"HX"
    struct.pack_into(">H", header, 2, 5)
    struct.pack_into(">I", header, 4, 0x100)
    used = [0, 2, 3, 5, 6, 20, 30, 35, 2047]
    struct.pack_into(">III", header, 40, BS, 2048, 2048 - len(used))
    fork(header, 112, BS, [(2, 1)])
    fork(header, 192, BS, [(3, 1)])
    fork(header, 272, 2 * BS, [(5, 2)])
    image[1024:1536] = header
    image[-1024:-512] = header
    for i in used:
        image[2 * BS + i // 8] |= 0x80 >> (i % 8)
    image[3 * BS + 8] = 1  # Empty overflow B-tree header.
    cat = bytearray(2 * BS)
    cat[8] = 1
    struct.pack_into(">III", cat, 20, 1, 1, 1)  # Count, first leaf, last leaf.
    struct.pack_into(">H", cat, 32, BS)
    cat[BS + 8] = 0xff
    struct.pack_into(">H", cat, BS + 10, 1)
    name = "Fixture".encode("utf-16-be")
    record = bytearray(8 + len(name) + 248)
    struct.pack_into(">HIH", record, 0, 6 + len(name), 2, len(name) // 2)
    record[8:8 + len(name)] = name
    data = 8 + len(name)
    struct.pack_into(">H", record, data, 2)
    fork(record, data + 88, 5000, [(20, 1), (30, 1)])
    fork(record, data + 168, 333, [(35, 1)])
    cat[BS + 14:BS + 14 + len(record)] = record
    struct.pack_into(">HH", cat, len(cat) - 4, 14 + len(record), 14)
    image[5 * BS:7 * BS] = cat
    image[20 * BS:21 * BS] = b"A" * BS
    image[30 * BS:31 * BS] = b"B" * BS
    image[35 * BS:36 * BS] = b"R" * BS
    return image


def with_empty_attributes():
    image = fixture()
    header = bytearray(image[1024:1536])
    fork(header, 352, 2 * BS, [(100, 1), (150, 1)])
    struct.pack_into(">I", header, 48, H.u32(header, 48) - 2)
    image[1024:1536] = header
    image[-1024:-512] = header
    for block in (100, 150):
        image[2 * BS + block // 8] |= 0x80 >> (block % 8)
    tree = bytearray(2 * BS)
    tree[8] = 1
    struct.pack_into(">H", tree, 10, 3)
    struct.pack_into(">H", tree, 32, BS)
    struct.pack_into(">II", tree, 36, 2, 1)
    for index, offset in enumerate((14, 120, 248, BS - 8)):
        struct.pack_into(">H", tree, BS - 2 * (index + 1), offset)
    tree[248] = 0x80
    # Free-node bytes may retain old content; preserve them too.
    tree[BS:] = b"X" * BS
    image[100 * BS:101 * BS] = tree[:BS]
    image[150 * BS:151 * BS] = tree[BS:]
    return image, tree


class CompactTests(unittest.TestCase):
    def test_fragmented_data_and_resource_contents_and_headers(self):
        source = fixture()
        before = hashlib.sha256(source).digest()
        result, report = C.compact(source, 6 * 1024 * 1024)
        self.assertEqual(hashlib.sha256(source).digest(), before)
        self.assertEqual(report["verified_file_forks"], 2)
        self.assertEqual(result[1024:1536], result[-1024:-512])
        self.assertEqual(H.u32(result, 1024 + 44), 1536)
        # Independent expected dense block placement: data at 5/6, resource 7.
        self.assertEqual(result[5 * BS:6 * BS], b"A" * BS)
        self.assertEqual(result[6 * BS:6 * BS + 904], b"B" * 904)
        self.assertEqual(result[7 * BS:7 * BS + 333], b"R" * 333)
        cat = result[3 * BS:5 * BS]
        record = next(H.leaf_records(cat[BS:], BS))
        parsed = H.parse_record(record)
        self.assertEqual(H.Fork(record, parsed[3] + 88).extents, [(5, 1), (6, 1)])
        self.assertEqual(H.Fork(record, parsed[3] + 168).extents, [(7, 1)])
        bitmap = result[BS:2 * BS]
        allocated = {i for i in range(1536) if bitmap[i // 8] & (0x80 >> (i % 8))}
        self.assertEqual(allocated, set(range(8)) | {1535})
        self.assertEqual(H.u32(result, 1024 + 48), 1536 - 9)

    def test_empty_fragmented_attributes_preserved(self):
        source, tree = with_empty_attributes()
        before = hashlib.sha256(source).digest()
        result, report = C.compact(source, 6 * 1024 * 1024)
        self.assertEqual(hashlib.sha256(source).digest(), before)
        relocated = H.Fork(result, 1024 + 352)
        self.assertNotEqual(relocated.extents, [(100, 1), (150, 1)])
        actual = b"".join(result[start * BS:(start + count) * BS]
                          for start, count in relocated.extents)[:relocated.logical]
        self.assertEqual(actual, tree)
        self.assertEqual(report["verified_file_forks"], 2)
        self.assertEqual(result[1024:1536], result[-1024:-512])

    def test_nonempty_or_inconsistent_attributes_rejected(self):
        for offset, value in ((14, 1), (16, 1), (20, 1), (24, 1), (28, 1)):
            with self.subTest(offset=offset):
                source, _ = with_empty_attributes()
                struct.pack_into(">H" if offset == 14 else ">I", source,
                                 100 * BS + offset, value)
                with self.assertRaisesRegex(C.UnsupportedImage, "nonempty attributes"):
                    C.compact(source, 6 * 1024 * 1024)

    def test_attributes_map_and_bounds_rejected(self):
        mutations = [(0, ">I", 1), (32, ">H", 1023), (36, ">I", 3),
                     (40, ">I", 0), (248, "B", 0xc0), (BS - 8, ">H", BS)]
        for offset, fmt, value in mutations:
            with self.subTest(offset=offset):
                source, _ = with_empty_attributes()
                struct.pack_into(fmt, source, 100 * BS + offset, value)
                with self.assertRaisesRegex(C.UnsupportedImage, "attributes"):
                    C.compact(source, 6 * 1024 * 1024)

    def test_dirty_image_rejected(self):
        source = fixture()
        struct.pack_into(">I", source, 1028, 0)
        with self.assertRaisesRegex(C.UnsupportedImage, "unmounted"):
            C.compact(source, 6 * 1024 * 1024)

    def test_bitmap_disagreement_rejected(self):
        source = fixture()
        source[2 * BS + 20 // 8] &= ~(0x80 >> (20 % 8))
        with self.assertRaisesRegex(C.UnsupportedImage, "unallocated"):
            C.compact(source, 6 * 1024 * 1024)

    def test_extents_overflow_rejected(self):
        source = fixture()
        struct.pack_into(">I", source, 3 * BS + 24, 1)
        with self.assertRaisesRegex(C.UnsupportedImage, "overflow"):
            C.compact(source, 6 * 1024 * 1024)

    def test_too_little_free_space_rejected(self):
        with self.assertRaisesRegex(C.UnsupportedImage, "4 MiB"):
            C.compact(fixture(), 4 * 1024 * 1024)

    def test_overlapping_forks_rejected(self):
        source = fixture()
        # Resource extent points at a data extent.
        struct.pack_into(">I", source, 6 * BS + 14 + 22 + 168 + 16, 20)
        with self.assertRaisesRegex(C.UnsupportedImage, "overlapping"):
            C.compact(source, 6 * 1024 * 1024)

    def test_leaf_cycle_rejected(self):
        source = fixture()
        struct.pack_into(">I", source, 6 * BS, 1)
        with self.assertRaisesRegex(C.UnsupportedImage, "cycle"):
            C.compact(source, 6 * 1024 * 1024)


if __name__ == "__main__":
    unittest.main()
