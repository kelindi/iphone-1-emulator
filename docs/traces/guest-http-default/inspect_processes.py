"""Bounded read-only process audit of a retained v39 OS1 snapshot, raw or gzip."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import subprocess
import threading
import time

BASE = Path(__file__).resolve().parent
ROOT = BASE.parents[1]
KERNEL_SHA = "42e9f3c18d4a4d5c956acdcded0c3f8f247fe3e7fc96d14cfa3aedcd77c292c9"


def digest(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as f:
        for chunk in iter(lambda: f.read(1048576), b""):
            h.update(chunk)
    return h.hexdigest()


def run(snapshot, output, names):
    output.mkdir(parents=True, exist_ok=False)
    kernel = ROOT / "web/firmware/kernel_1A543a.macho"
    if digest(kernel) != KERNEL_SHA:
        raise ValueError("process layout requires the exact original OS1 kernel")
    report = {"passed": False, "snapshot": str(snapshot.resolve()),
              "snapshotSHA256": digest(snapshot), "requiredNames": names,
              "kernelSHA256": KERNEL_SHA, "wallCapSeconds": 30,
              "sourceSHA256": {str(p): digest(p) for p in
                               (BASE / "processes.c", BASE / "processes",
                                BASE / "probe-source/peek.c", BASE / "probe-source/peek.h")}}
    feed = {"bytes": 0}
    raw_hash = hashlib.sha256()
    start = time.monotonic()
    with (output / "stdout.json").open("wb") as stdout, (output / "stderr.log").open("wb") as stderr:
        proc = subprocess.Popen([str(BASE / "processes"), "-", *names],
                                stdin=subprocess.PIPE, stdout=stdout, stderr=stderr)
        def write():
            try:
                with snapshot.open("rb") as test:
                    compressed = test.read(2) == b"\x1f\x8b"
                with (gzip.open(snapshot, "rb") if compressed else snapshot.open("rb")) as stream:
                    for chunk in iter(lambda: stream.read(1048576), b""):
                        feed["bytes"] += len(chunk)
                        if feed["bytes"] > 512 * 1024 * 1024:
                            raise ValueError("snapshot capacity exceeded")
                        raw_hash.update(chunk)
                        proc.stdin.write(chunk)
                proc.stdin.close()
                feed["rawSHA256"] = raw_hash.hexdigest()
            except Exception as error:
                feed["error"] = repr(error)
                try:
                    proc.stdin.close()
                except Exception:
                    pass
        thread = threading.Thread(target=write, daemon=True)
        thread.start()
        try:
            proc.wait(timeout=30)
        except subprocess.TimeoutExpired:
            report["timeout"] = True
            proc.kill()
            proc.wait()
        thread.join(timeout=5)
        report["feedThreadAlive"] = thread.is_alive()
        report["returncode"] = proc.returncode
    report["elapsedSeconds"] = time.monotonic() - start
    report["feed"] = feed
    try:
        report["processAudit"] = json.loads((output / "stdout.json").read_text())
    except Exception as error:
        report["parseError"] = repr(error)
    report["passed"] = (proc.returncode == 0 and not report["feedThreadAlive"] and
                        "error" not in feed and report.get("processAudit", {}).get("passed") is True)
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("snapshot", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--require", action="append", default=[])
    args = parser.parse_args()
    raise SystemExit(run(args.snapshot, args.output, args.require))
