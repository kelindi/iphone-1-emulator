import struct
import unittest
import tempfile
from pathlib import Path
from unittest.mock import patch
import build
from build import validate_arm_command
try:
    from audit_r9 import audit_words
except ModuleNotFoundError as error:
    if error.name != "capstone":
        raise
    audit_words = None


class BuildContract(unittest.TestCase):
    def test_default_runtime_and_custom_ownership(self):
        with tempfile.TemporaryDirectory() as temporary, \
             patch.object(build, "sha", return_value="pinned"), \
             patch.object(build.subprocess, "check_output", return_value="clang"), \
             patch.object(build.subprocess, "run") as run, \
             patch.object(build, "audit", return_value={"passed": True}):
            run.return_value.returncode = 0
            run.return_value.stdout = run.return_value.stderr = ""
            for label, options, expected in (
                ("default", {}, {"server.c", "http.c", "standalone_runtime.c"}),
                ("custom", {"sources": [build.SOURCE / "http.c"], "expected_imports": ["_write"]}, {"http.c"}),
            ):
                report = build.build(Path(temporary) / label, Path("clang"), Path("ld"), Path("lib"), **options)
                commands = [item["argv"] for item in report["commands"] if "-c" in item["argv"]]
                self.assertEqual({Path(command[command.index("-c") + 1]).name for command in commands}, expected)
                self.assertTrue(all(command.count("-ffixed-r9") == 1 for command in commands))
                self.assertEqual("standalone_runtime.c" in report["sourceSHA256"], label == "default")

    def test_required_flag(self):
        validate_arm_command(["clang", "-ffixed-r9", "-c", "own.c"])
        for flags in ([], ["-fcall-saved-r9"], ["-ffixed-r9"] * 2,
                      ["-ffixed-r9", "-fcall-saved-r9"]):
            with self.assertRaises(ValueError):
                validate_arm_command(["clang", *flags, "-c", "own.c"])

    @unittest.skipUnless(audit_words, "Capstone is required for decoded ARM audit controls")
    def test_destination_and_implicit_writes(self):
        # MOV r9,#60; conditional MOV; POP {r9,pc}; LDM r0!,{r9};
        # LDR r0,[r9],#4 also writes the base register.
        for word in (0xe3a0903c, 0x03a0903c, 0xe8bd8200, 0xe8b00200, 0xe4990004):
            with self.assertRaises(ValueError):
                audit_words(struct.pack("<I", word))
        # Reads alone preserve the original thread pointer.
        self.assertEqual(audit_words(struct.pack("<2I", 0xe1a00009, 0xe92d0200)), 2)

    @unittest.skipUnless(audit_words, "Capstone is required for decoded ARM audit controls")
    def test_alignment(self):
        with self.assertRaises(ValueError):
            audit_words(b"\0")


if __name__ == "__main__":
    unittest.main()
