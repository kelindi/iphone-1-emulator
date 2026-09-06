"""Synthetic copy-only install tests; no Apple filesystem or executable needed."""
import contextlib
import io
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest import mock

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parents[1] / "firmware"))
import install as I
import test_hfsx_compact as F


def record(parent, name, kind, ident):
    data = bytearray(88 if kind == 1 else 248)
    struct.pack_into(">H", data, 0, kind)
    struct.pack_into(">I", data, 8, ident)
    struct.pack_into(">H", data, 42, 0o40755 if kind == 1 else 0o100644)
    return I.W.Work._make_key(parent, name) + data


def fixture():
    image = F.fixture()
    rows = [record(1, "FixtureVolume", 1, 2), record(2, "usr", 1, 3),
            record(3, "libexec", 1, 4), record(2, "private", 1, 5),
            record(5, "var", 1, 6), record(6, "root", 1, 7),
            record(7, "Library", 1, 8), record(8, "Preferences", 1, 1452),
            record(2, "System", 1, 9), record(9, "Library", 1, 10),
            record(10, "LaunchDaemons", 1, 11),
            record(1452, ".GlobalPreferences.plist", 2, 1453)]
    template = bytearray(rows[-1])
    F.fork(template, len(template) - 248 + 88, 4, [(20, 1)])
    rows[-1] = bytes(template)
    rows.sort(key=lambda row: (I.H.u32(row, 2), row[8:2 + I.H.u16(row, 0)]))
    cat = bytearray(image[5 * F.BS:7 * F.BS])
    cat[F.BS:] = bytes(F.BS)
    cat[F.BS + 8] = 255
    cat[51] = 0xbc
    struct.pack_into(">H", cat, F.BS + 10, len(rows))
    struct.pack_into(">I", cat, 20, len(rows))
    pos = 14
    for i, row in enumerate(rows):
        struct.pack_into(">H", cat, 2 * F.BS - 2 * (i + 1), pos)
        cat[F.BS + pos:F.BS + pos + len(row)] = row
        pos += len(row)
    struct.pack_into(">H", cat, 2 * F.BS - 2 * (len(rows) + 1), pos)
    image[5 * F.BS:7 * F.BS] = cat
    image[20 * F.BS:20 * F.BS + 4] = b"KEEP"
    for start in (1024, len(image) - 1024):
        struct.pack_into(">I", image, start + 64, 1500)
    return image


class InstallTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.source = self.root / "source.img"
        self.source.write_bytes(fixture())
        self.program = self.root / "own-program"
        self.program.write_bytes(b"INDEPENDENT TEST DATA" * 350)
        self.output = self.root / "new.img"

    def tearDown(self):
        self.temporary.cleanup()

    def install(self, source_hash=None, program_hash=None):
        with contextlib.redirect_stdout(io.StringIO()):
            return I.install(self.source, self.output, source_hash or I.digest(self.source),
                             self.program, program_hash or I.digest(self.program))

    def test_two_files_all_old_records_and_forks_preserved(self):
        before = self.source.read_bytes()
        report = self.install()
        self.assertTrue(report["passed"])
        self.assertEqual(report["newCatalogRecords"], 4)
        self.assertEqual(report["unchangedExistingFileForks"], 2)
        self.assertEqual(self.source.read_bytes(), before)
        records, paths, forks = I.inventory(self.output)
        self.assertEqual(forks[I.PROGRAM_PATH][0]["sha256"], I.digest(self.program))
        for path, mode in ((I.PROGRAM_PATH, 0o100755), (I.PLIST_PATH, 0o100644)):
            data = paths[path][2]
            self.assertEqual(I.H.u16(data, 42), mode)
            self.assertEqual(data[32:40], bytes(8))
        self.assertEqual(len(records), report["preservedCatalogRecords"] + 4)

    def test_wrong_source_or_program_hash_before_mutation(self):
        before = self.source.read_bytes()
        for key in ("source_hash", "program_hash"):
            with self.subTest(key=key), self.assertRaises(ValueError):
                self.install(**{key: "0" * 64})
            self.assertFalse(self.output.exists())
            self.assertEqual(self.source.read_bytes(), before)

    def test_dirty_primary_rejected_without_fake_clean_flags(self):
        raw = bytearray(self.source.read_bytes())
        struct.pack_into(">I", raw, 1028, 0)
        self.source.write_bytes(raw)
        with self.assertRaisesRegex(ValueError, "clean"):
            self.install()
        self.assertEqual(self.source.read_bytes(), raw)
        self.assertFalse(self.output.exists())

    def test_known_stale_alternate_preserved_and_unknown_fork_mismatch_rejected(self):
        raw = bytearray(self.source.read_bytes())
        struct.pack_into(">I", raw, len(raw) - 1020, 0)
        self.source.write_bytes(raw)
        self.install()
        installed = self.output.read_bytes()
        old_header = raw[-1024:-512]
        new_header = installed[-1024:-512]
        changed = {i for i in range(0, 512, 4) if old_header[i:i + 4] != new_header[i:i + 4]}
        self.assertEqual(changed, {32, 48, 64})
        self.assertEqual(I.H.u32(new_header, 32), I.H.u32(old_header, 32) + 2)
        self.assertEqual(I.H.u32(new_header, 48), I.H.u32(old_header, 48) - 3)
        self.assertEqual(I.H.u32(new_header, 64), I.H.u32(old_header, 64) + 2)
        self.assertEqual(I.H.u32(new_header, 4), 0)  # Historical flag distinction survives.
        for at in changed:
            self.assertEqual(new_header[at:at + 4], installed[1024 + at:1028 + at])
        self.output.unlink()
        self.output.with_suffix(".img.json").unlink()
        raw[-1024 + 112] ^= 1
        self.source.write_bytes(raw)
        with self.assertRaisesRegex(ValueError, "alternate"):
            self.install()
        self.assertFalse(self.output.exists())

    def test_existing_output_or_receipt_or_guest_path_rejected(self):
        self.output.write_bytes(b"KEEP OUTPUT")
        with self.assertRaises(ValueError):
            self.install()
        self.assertEqual(self.output.read_bytes(), b"KEEP OUTPUT")
        self.output.unlink()
        receipt = self.output.with_suffix(".img.json")
        receipt.write_bytes(b"KEEP RECEIPT")
        with self.assertRaises(ValueError):
            self.install()
        self.assertEqual(receipt.read_bytes(), b"KEEP RECEIPT")
        receipt.unlink()
        self.install()
        installed = self.output.read_bytes()
        self.source.write_bytes(installed)
        self.output.unlink()
        receipt.unlink()
        with self.assertRaisesRegex(ValueError, "absent destination"):
            self.install()
        self.assertEqual(self.source.read_bytes(), installed)
        self.assertFalse(self.output.exists())

    def test_zero_length_allocated_resource_fork_rejected(self):
        work = I.W.Work(self.source)
        item = work._find_record(1452, ".GlobalPreferences.plist", 2)
        # _find_record locates a row; map its original catalog bytes exactly.
        needle = item[4]
        at = work.cat.find(needle)
        disk = work.cat_disk_off(at + item[3] + 168)
        work.vol.f.close()
        with self.source.open("r+b") as stream:
            stream.seek(disk + 12)
            stream.write(struct.pack(">III", 1, 20, 1))
        before = self.source.read_bytes()
        with self.assertRaisesRegex(ValueError, "resource fork"):
            self.install()
        self.assertFalse(self.output.exists())
        self.assertEqual(self.source.read_bytes(), before)

    def test_corruption_of_old_fork_discards_only_temporary_copy(self):
        original = I.W.Work.add_file

        def damaged(work, *args, **kwargs):
            original(work, *args, **kwargs)
            with Path(work.path).open("r+b") as stream:
                stream.seek(20 * F.BS)
                stream.write(b"BAD!")

        before = self.source.read_bytes()
        with mock.patch.object(I.W.Work, "add_file", damaged), self.assertRaisesRegex(ValueError, "original file fork changed"):
            self.install()
        self.assertEqual(self.source.read_bytes(), before)
        self.assertFalse(self.output.exists())
        self.assertFalse(list(self.root.glob("*.partial")))


if __name__ == "__main__":
    unittest.main(verbosity=2)
