"""Bind current default multipart bytes to the actual unmodified MBX source."""
import hashlib
import json
from pathlib import Path
import plistlib
import sys
import zlib

BASE = Path(__file__).resolve().parent
ROOT = BASE.parents[1]
sys.path.insert(0, str(ROOT / "tools/legacy_services/guest_http"))
import install as I

EXPECTED = "73d0d2a0ed4564dc16c67c7c6efc167725d2833461cd47919faa676595c0ded9"
SOURCE = ROOT / ".context/graphics-speed/rootfs-mbx2d.img"
manifest_path = ROOT / "web/manifest.json"
manifest_bytes = manifest_path.read_bytes()
manifest = json.loads(manifest_bytes)
decoder = zlib.decompressobj(16 + zlib.MAX_WBITS)
raw_hash = hashlib.sha256()
total = 0
parts = []
for url in manifest["rootfs"]:
    path = ROOT / "web/firmware" / url.rsplit("/", 1)[1]
    part_hash = hashlib.sha256()
    count = 0
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(65536), b""):
            part_hash.update(chunk)
            count += len(chunk)
            expanded = decoder.decompress(chunk, 176160769 - total)
            total += len(expanded)
            if total > 176160768 or decoder.unconsumed_tail:
                raise ValueError("rootfs decompressed capacity exceeded")
            raw_hash.update(expanded)
    actual = part_hash.hexdigest()
    if actual != manifest["assetSha256"][url]:
        raise ValueError("current manifest part mismatch: " + url)
    parts.append({"url": url, "path": str(path), "bytes": count, "sha256": actual})
if not decoder.eof or decoder.unused_data or total != 176160768:
    raise ValueError("incomplete, trailing or wrong-size rootfs stream")
source_hash = I.digest(SOURCE)
if raw_hash.hexdigest() != source_hash or source_hash != EXPECTED:
    raise ValueError("actual manifest rootfs does not match the pristine MBX candidate")
work = I.W.Work(SOURCE)
try:
    springboard_path = "/System/Library/LaunchDaemons/com.apple.SpringBoard.plist"
    springboard_raw = work.read_file_path(springboard_path)
    springboard = plistlib.loads(springboard_raw)
    env = springboard["EnvironmentVariables"]
    if env.get("LK_ENABLE_MBX2D") != "1" or env.get("LK_ENABLE_OGL") != "0":
        raise ValueError("actual accelerated default configuration mismatch")
finally:
    work.vol.f.close()
program = BASE / "build-quiet-2/iphone1-local-http"
additions = [(I.PROGRAM_PATH, program, 0o755),
             (I.PLIST_PATH, I.HERE / I.PLIST_NAME, 0o644)]
preflight = I.preflight(SOURCE, additions, EXPECTED)
records, paths, forks = I.inventory(SOURCE)
report = {"passed": True, "manifestSHA256": hashlib.sha256(manifest_bytes).hexdigest(),
          "source": str(SOURCE), "sourceSHA256": source_hash,
          "actualMultipartRawSHA256": raw_hash.hexdigest(), "rawBytes": total,
          "parts": parts, "springboardPath": springboard_path,
          "springboardSHA256": hashlib.sha256(springboard_raw).hexdigest(),
          "springboard": springboard, "preflight": preflight,
          "catalogRecords": len(records), "fileForks": len(forks) * 2,
          "allOldForks": forks,
          "scope": "Read-only current manifest, rootfs stream and actual pristine MBX filesystem; no copy or guest."}
if manifest_path.read_bytes() != manifest_bytes or I.digest(SOURCE) != EXPECTED:
    raise ValueError("source changed during qualification")
(BASE / "source-qualification.json").write_text(json.dumps(report, indent=2) + "\n")
print(json.dumps({k: v for k, v in report.items() if k != "allOldForks"}, indent=2))
