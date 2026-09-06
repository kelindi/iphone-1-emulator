"""Post-run pin audit, then lossless archive of only our new disposable image."""
import gzip
import hashlib
import json
import os
from pathlib import Path

BASE = Path(__file__).resolve().parent


def digest(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for chunk in iter(lambda: stream.read(1048576), b""):
            h.update(chunk)
    return h.hexdigest()


pins = json.loads((BASE / "source-pins.json").read_text())
post = {path: {"expected": expected, "actual": digest(path)} for path, expected in pins.items()}
passed = all(row["actual"] == row["expected"] for row in post.values())
(BASE / "native-run-1/post-pins.json").write_text(json.dumps({"passed": passed, "files": post}, indent=2) + "\n")
if not passed:
    raise ValueError("a frozen native source/input changed")
raw = BASE / "installed/default-local-demo.img"
output = raw.with_suffix(raw.suffix + ".gz")
partial = raw.with_suffix(raw.suffix + ".gz.partial")
if output.exists() or partial.exists() or raw.stat().st_nlink != 1:
    raise ValueError("only our single-link newly generated raw image may be archived")
expected = json.loads(raw.with_suffix(raw.suffix + ".json").read_text())["outputSHA256"]
if digest(raw) != expected:
    raise ValueError("installed image mismatch")
raw_bytes = raw.stat().st_size
with raw.open("rb") as source, partial.open("xb") as sink:
    with gzip.GzipFile(fileobj=sink, mode="wb", compresslevel=1, mtime=0, filename="") as compressed:
        for chunk in iter(lambda: source.read(1048576), b""):
            compressed.write(chunk)
    sink.flush()
    os.fsync(sink.fileno())
roundtrip = hashlib.sha256()
count = 0
with gzip.open(partial, "rb") as stream:
    for chunk in iter(lambda: stream.read(1048576), b""):
        count += len(chunk)
        if count > raw_bytes:
            raise ValueError("archive expansion bound")
        roundtrip.update(chunk)
if count != raw_bytes or roundtrip.hexdigest() != expected or digest(raw) != expected:
    raise ValueError("lossless archive comparison failed")
os.link(partial, output)
partial.unlink()
report = {"passed": True, "rawPath": str(raw), "rawBytes": count, "rawSHA256": expected,
          "gzipPath": str(output), "gzipBytes": output.stat().st_size,
          "gzipSHA256": digest(output), "roundtripExact": True,
          "postRunPins": len(post), "removedOnlyNewVerifiedRawDuplicate": True}
(BASE / "installed/compression.json").write_text(json.dumps(report, indent=2) + "\n")
raw.unlink()
print(json.dumps(report, indent=2))
