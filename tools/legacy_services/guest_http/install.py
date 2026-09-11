"""Install two own files on a NEW clean HFSX copy and audit every old fork.

The source hash is mandatory. This tool never repairs clean flags, changes a
mounted snapshot, splits catalog nodes, or replaces existing guest files.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import sys
import tempfile

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[1] / "firmware"))
import hfsx_extract as H
import rootfs_work as W

PROGRAM_PATH = "/usr/libexec/iphone1-local-http"
PLIST_NAME = "com.iphone1.local-demo.http.plist"
PLIST_PATH = "/System/Library/LaunchDaemons/" + PLIST_NAME
PARENTS = ("/usr/libexec", "/System/Library/LaunchDaemons")


def digest(path):
    result = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for part in iter(lambda: stream.read(1048576), b""):
            result.update(part)
    return result.hexdigest()


def inventory(path):
    work = W.Work(path)
    try:
        records, objects, directories = {}, [], {2: "/"}
        for _, node in work._leaf_chain():
            for _, _, raw in work._leaf_records(node):
                item = H.parse_record(raw)
                if item is None:
                    raise ValueError("unparsed catalog record")
                parent, name, kind, offset, raw = item
                key = (parent, name, kind)
                if key in records:
                    raise ValueError("duplicate catalog record")
                records[key] = raw
                if kind in (1, 2):
                    objects.append(item)
        for _ in range(len(objects) + 1):
            changed = False
            for parent, name, kind, offset, raw in objects:
                if kind == 1 and parent in directories:
                    ident = H.u32(raw, offset + 8)
                    value = directories[parent].rstrip("/") + "/" + name
                    if ident not in directories:
                        directories[ident] = value
                        changed = True
            if not changed:
                break
        paths, forks = {}, {}
        for parent, name, kind, offset, raw in objects:
            if kind == 1 and H.u32(raw, offset + 8) == 2:
                continue  # Root volume record remains in the complete raw inventory.
            if parent not in directories:
                raise ValueError("unresolved catalog parent")
            guest = directories[parent].rstrip("/") + "/" + name
            paths[guest] = ((parent, name, kind), offset, raw[offset:])
            if kind == 2:
                values = []
                for at in (88, 168):
                    fork = H.Fork(raw, offset + at)
                    if fork.covered() != fork.total_blocks:
                        raise ValueError("ordinary fork requires unsupported overflow extents")
                    data = work.vol.read_fork(fork)
                    if len(data) != fork.logical:
                        raise ValueError("short ordinary fork")
                    values.append({"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()})
                forks[guest] = values
        return records, paths, forks
    finally:
        work.vol.f.close()


def preflight(source, additions, expected_source):
    if digest(source) != expected_source:
        raise ValueError("source image hash mismatch")
    with Path(source).open("rb") as stream:
        stream.seek(1024)
        primary = stream.read(512)
        stream.seek(-1024, 2)
        alternate = stream.read(512)
    if primary[:4] != b"HX\x00\x05" or H.u32(primary, 4) != 0x100:
        raise ValueError("a clean, unmounted, nonjournaled HFSX source is required")
    if H.u32(primary, 40) != 4096 or H.u32(primary, 44) * 4096 != Path(source).stat().st_size:
        raise ValueError("unsupported raw-image geometry")
    different = [i for i in range(0, 512, 4) if primary[i:i + 4] != alternate[i:i + 4]]
    if (H.u32(alternate, 4) not in (0, 0x100) or
            not set(different) <= {4, 20, 32, 36, 48, 52, 64, 68}):
        raise ValueError("unqualified alternate-header disagreement")
    work = W.Work(source)
    try:
        nodes = [(ident, [r[2] for r in work._leaf_records(node)])
                 for ident, node in work._leaf_chain()]
        ident = work.next_catalog_id
        proof = []
        for guest, host, mode in additions:
            parent = work._resolve_dir(str(Path(guest).parent))
            if not parent or work.find_file_path(guest):
                raise ValueError("existing parent and absent destination required: " + guest)
            name = Path(guest).name
            entries = [W.Work._make_key(parent, name) + bytes(248),
                       W.Work._make_key(ident, "") + struct.pack(">HHIH", 4, 0, parent, len(name)) + name.encode("utf-16-be")]
            for row in entries:
                key = work._key(row)
                chosen = None
                for node_id, rows in nodes:
                    chosen = (node_id, rows)
                    if not rows or work._key(rows[-1]) >= key:
                        break
                if chosen is None:
                    raise ValueError("empty catalog leaf chain")
                node_id, rows = chosen
                if not rows or key <= work._key(rows[0]) or any(work._key(r) == key for r in rows):
                    raise ValueError("insertion would change a first key or duplicate a key")
                at = next((i for i, r in enumerate(rows) if work._key(r) > key), len(rows))
                rows.insert(at, row)
                used = 14 + sum(map(len, rows)) + 2 * (len(rows) + 1)
                if used > work.node_size:
                    raise ValueError("catalog leaf has insufficient space")
                proof.append({"path": guest, "node": node_id, "usedAfter": used,
                              "capacity": work.node_size, "existingFirstKeyUnchanged": True})
            ident += 1
        return {"catalog": proof, "primarySHA256": hashlib.sha256(primary).hexdigest(),
                "alternateSHA256": hashlib.sha256(alternate).hexdigest(),
                "headerDifferentWords": different,
                "policy": "Known source hash binds both headers; preserve any stale alternate, never normalize it."}
    finally:
        work.vol.f.close()


def install(source, target, expected_source, program, expected_program):
    source, target, program = Path(source), Path(target), Path(program)
    if target.exists() or target.with_suffix(target.suffix + ".json").exists() or source.resolve() == target.resolve():
        raise ValueError("destination must be new")
    if digest(program) != expected_program:
        raise ValueError("own executable hash mismatch")
    additions = [(PROGRAM_PATH, program, 0o755), (PLIST_PATH, HERE / PLIST_NAME, 0o644)]
    proof = preflight(source, additions, expected_source)
    before, before_paths, before_forks = inventory(source)
    for parent in PARENTS:
        data = before_paths[parent][2]
        if H.u16(data, 42) != 0o40755 or H.u32(data, 32) != 0 or H.u32(data, 36) != 0:
            raise ValueError("unexpected existing parent ownership")
    template = before_paths["/private/var/root/Library/Preferences/.GlobalPreferences.plist"][2]
    resource = H.Fork(template, 168)
    if resource.logical or resource.total_blocks or resource.covered():
        raise ValueError("file template has a nonempty resource fork")
    target.parent.mkdir(parents=True, exist_ok=True)
    fd, name = tempfile.mkstemp(prefix="local-demo-", suffix=".partial", dir=target.parent)
    temporary = Path(name)
    try:
        with os.fdopen(fd, "wb") as output, source.open("rb") as original:
            shutil.copyfileobj(original, output, 1048576)
        work = W.Work(temporary)
        try:
            for guest, host, mode in additions:
                work.add_file(guest, host, mode=mode, uid=0, gid=0)
        finally:
            work.vol.f.close()
        after, after_paths, after_forks = inventory(temporary)
        if set(after_paths) - set(before_paths) != {PROGRAM_PATH, PLIST_PATH}:
            raise ValueError("unexpected added path")
        if not set(before_paths) <= set(after_paths) or not set(before) <= set(after):
            raise ValueError("removed original catalog record")
        expected_keys = set()
        for guest, _, _ in additions:
            key, offset, data = after_paths[guest]
            expected_keys.add(key)
            ident = H.u32(data, 8)
            expected_keys.add((ident, "", 4))
            name = Path(guest).name
            expected_thread = (W.Work._make_key(ident, "") +
                               struct.pack(">HHIH", 4, 0, key[0], len(name)) + name.encode("utf-16-be"))
            if after.get((ident, "", 4)) != expected_thread:
                raise ValueError("new file thread is not exact")
        if set(after) - set(before) != expected_keys:
            raise ValueError("unexpected new catalog records")
        allowed = {before_paths[parent][0]: before_paths[parent][1] for parent in PARENTS}
        for key, raw in before.items():
            new = after[key]
            if key in allowed:
                at = allowed[key] + 4
                if H.u32(new, at) != H.u32(raw, at) + 1 or new[:at] + new[at + 4:] != raw[:at] + raw[at + 4:]:
                    raise ValueError("unexpected parent metadata change")
            elif new != raw:
                raise ValueError("original catalog record changed")
        for guest, forks in before_forks.items():
            if after_forks[guest] != forks:
                raise ValueError("original file fork changed: " + guest)
        for guest, host, mode in additions:
            data = after_paths[guest][2]
            if H.u16(data, 42) != 0o100000 | mode or H.u32(data, 32) or H.u32(data, 36):
                raise ValueError("new file ownership mismatch")
            if after_forks[guest][0] != {"bytes": host.stat().st_size, "sha256": digest(host)}:
                raise ValueError("new file bytes mismatch")
            if after_forks[guest][1]["bytes"]:
                raise ValueError("unexpected new resource fork")
        if digest(source) != expected_source or digest(program) != expected_program:
            raise ValueError("input changed during installation")
        report = {"passed": True, "sourceSHA256": expected_source, "sourceUnchanged": True,
                  "outputSHA256": digest(temporary), "outputBytes": temporary.stat().st_size,
                  "programSHA256": expected_program, "unchangedExistingFileForks": 2 * len(before_forks),
                  "preservedCatalogRecords": len(before), "newCatalogRecords": len(after) - len(before),
                  "newFiles": [x[0] for x in additions], "preflight": proof}
        with temporary.open("rb") as output:
            os.fsync(output.fileno())
        os.link(temporary, target)  # Atomic no-clobber publication, never a source hardlink.
        with target.with_suffix(target.suffix + ".json").open("x") as receipt:
            json.dump(report, receipt, indent=2)
            receipt.write("\n")
        return report
    finally:
        temporary.unlink(missing_ok=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--source-sha256", required=True)
    parser.add_argument("--build", type=Path, required=True, help="Successful quiet build.py output directory")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    build = json.loads((args.build / "build.json").read_text())
    if not build.get("passed") or build.get("diagnosticLogging") or not build.get("audit", {}).get("passed"):
        raise SystemExit("a passed quiet own-program build is required")
    if (build.get("programName") != "iphone1-local-http" or
            set(build.get("sourceSHA256", {})) != {"server.c", "http.c", "standalone_runtime.c", "http.h", "build.py", "macho_audit.py"}):
        raise SystemExit("this installer accepts only the maintained local-demo program")
    for name, expected in build["sourceSHA256"].items():
        if digest(HERE / name) != expected:
            raise SystemExit("own source changed since the reviewed build: " + name)
    print(json.dumps(install(args.source, args.output, args.source_sha256,
                             args.build / "iphone1-local-http", build["audit"]["sha256"]), indent=2))
