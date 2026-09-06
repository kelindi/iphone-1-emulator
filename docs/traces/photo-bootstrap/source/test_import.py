"""Run a prebuilt sanitizer host contract in isolated directories; never a guest."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile

CASES = ["fresh", "short", "eintr", "uid", "dir-symlink", "dir-file", "foreign",
         "temp", "file-symlink", "fifo", "extra-byte", "zero", "sync", "close",
         "race", "same-race", "unlink", "read", "write"]


def run(binary):
    rows = []
    for case in CASES:
        with tempfile.TemporaryDirectory(prefix="iphone1-photo-contract-") as tmp:
            result = subprocess.run([str(binary.resolve()), tmp, case], capture_output=True,
                                    text=True, timeout=15)
            row = {"case": case, "exitCode": result.returncode,
                   "stdout": result.stdout, "stderr": result.stderr}
            rows.append(row)
            if result.returncode:
                raise AssertionError(row)
            root = Path(tmp)
            folder = root / "Media/DCIM/100APPLE"
            if case in ("foreign", "race"):
                assert (folder / "IMG_0001.JPG").read_bytes() == b"foreign"
            if case == "temp":
                assert (folder / ".IPHONE1-PHOTO-V1-IMG_0001.JPG.part").read_bytes() == b"foreign"
            if case in ("zero", "sync", "close", "write"):
                assert not list(folder.iterdir()), "failed write must not publish or leave its own temp"
            if case == "uid":
                assert not list(root.iterdir())
            if case == "dir-file":
                assert (root / "Media").read_bytes() == b"foreign"
            if case == "dir-symlink":
                assert (root / "Media").is_symlink()
            if case == "file-symlink":
                assert (folder / "IMG_0001.JPG").is_symlink()
            if case == "unlink":
                assert (folder / ".IPHONE1-PHOTO-V1-IMG_0001.JPG.part").exists()
            row["preservationChecksPassed"] = True
    return {"passed": True, "cases": rows, "scope": "Own algorithm and real host filesystem only; no OS1 execution"}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    args = parser.parse_args()
    print(json.dumps(run(args.binary), indent=2))
