"""Rebind the accepted 091 read-only observer to the final maintained executable.

Preparation only: writes private source, never builds or starts a guest.
Execute after the coordination hold, alongside final pin validation.
"""
import hashlib
import json
from pathlib import Path
import re

BASE = Path(__file__).resolve().parent
OLD = BASE.parent / "guest-safari"
ROOT = BASE.parents[1]
BUILD = BASE / "build-quiet-2"


def sha(data):
    return hashlib.sha256(data).hexdigest()


def replace_exact(source, old, new, count):
    if source.count(old) != count:
        raise ValueError("unexpected frozen observer replacement count: " + old)
    return source.replace(old, new)


def prepare():
    # These are the already accepted frozen 091 sources. The old binary/image
    # is never overwritten or reused as the final daemon.
    pins = json.loads((OLD / "source-pins.json").read_text())
    retained = {}
    for name in ("probe.c", "identity.h", "peek.c", "peek.h",
                 "snapshot_transport.py", "test_snapshot_transport.c"):
        path = OLD / name
        data = path.read_bytes()
        if sha(data) != pins[str(path)]:
            raise ValueError("frozen source mismatch: " + name)
        retained[name] = data
    build = json.loads((BUILD / "build.json").read_text())
    if not build["passed"] or build["diagnosticLogging"]:
        raise ValueError("passed final quiet build required")
    own = ROOT / "tools/legacy_services/guest_http"
    for name, expected in build["sourceSHA256"].items():
        if sha((own / name).read_bytes()) != expected:
            raise ValueError("maintained source changed: " + name)
    executable = (BUILD / "iphone1-local-http").read_bytes()
    audit = build["audit"]
    if not audit["passed"] or sha(executable) != audit["sha256"]:
        raise ValueError("final executable mismatch")
    entry = int(audit["entry"], 16)
    text = audit["text"]
    if not (text["address"] <= entry and entry + 64 <= text["address"] + text["size"]
            and (entry & 3) == 0 and (entry & 4095) + 64 <= 4096):
        raise ValueError("entry prefix must be complete ARM code within one mapped page")
    offset = text["fileOffset"] + entry - text["address"]
    prefix = executable[offset:offset + 64]
    number = int(audit["ownSymbols"]["_number"], 16)
    addresses = {row["symbol"]: int(row["originalExportVA"], 16)
                 for row in audit["imports"]}
    source = retained.pop("probe.c").decode()
    source = replace_exact(source, "0x1400u", hex(entry) + "u", 2)
    source = replace_exact(source, "0x20a8u", hex(number) + "u", 1)
    for symbol, previous in (("_bind", "0x3000e8e0u"),
                             ("_listen", "0x30030500u"),
                             ("_write", "0x30009860u"),
                             ("__exit", "0x300099f0u")):
        source = replace_exact(source, previous, hex(addresses[symbol]) + "u", 1)
    declaration = "static const unsigned char own_prefix[]={" + ",".join(map(str, prefix)) + "};"
    source, count = re.subn(r"static const unsigned char own_prefix\[\]=\{[^}]+\};", declaration, source)
    if count != 1:
        raise ValueError("missing frozen code-prefix declaration")
    retained["probe.c"] = source.encode()
    output = BASE / "probe-source"
    output.mkdir(exist_ok=False)
    for name, data in retained.items():
        (output / name).write_bytes(data)
    watches = [entry, number, *(addresses[x] for x in ("_bind", "_listen", "_write", "__exit"))]
    (output / "watch-pcs.json").write_text(json.dumps(list(map(hex, watches)), indent=2) + "\n")
    report = {"passed": True, "finalExecutableSHA256": audit["sha256"],
              "entry": hex(entry), "number": hex(number), "prefixHex": prefix.hex(),
              "sourceSHA256": {name: sha(data) for name, data in retained.items()},
              "scope": "Only address/prefix rebinding of accepted observer; no machine or guest changes."}
    (output / "preparation.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    prepare()
