"""Embed only the five independently authored, bounded JPEG/THM test fixtures.

Preparation uses the retained deterministic PPM generator and pinned ffmpeg;
this step never reads an Apple asset or opens a filesystem image.
"""
import argparse
import hashlib
import json
from pathlib import Path

NAMES = ["IMG_0001.JPG", "IMG_0001.THM", "IMG_0002.JPG", "IMG_0002.THM", "BAD.JPG"]


def embed(source, output):
    rows = []
    lines = ["/* Generated own test patterns; see assets.json. */"]
    for index, name in enumerate(NAMES):
        data = (source / name).read_bytes()
        if not 4 < len(data) <= 65536 or data[:2] != b"\xff\xd8" or data[-2:] != b"\xff\xd9":
            raise ValueError("bounded JPEG required: " + name)
        rows.append({"name": name, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()})
        lines.append("static const unsigned char photo_data_%d[] = {" % index)
        lines += ["    " + ",".join("0x%02x" % b for b in data[off:off+16]) + "," for off in range(0, len(data), 16)]
        lines.append("};")
    lines.append("static const struct photo_asset photo_assets[5] = {")
    for index, name in enumerate(NAMES):
        lines.append('    {"%s", ".IPHONE1-PHOTO-V1-%s.part", photo_data_%d, sizeof photo_data_%d},' % (name, name, index, index))
    lines.append("};")
    with output.open("x") as stream:
        stream.write("\n".join(lines) + "\n")
    with output.with_suffix(".json").open("x") as stream:
        json.dump({"scope": "Own fixed JPEG test patterns, not camera captures", "files": rows}, stream, indent=2)
        stream.write("\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    embed(args.source, args.output)
