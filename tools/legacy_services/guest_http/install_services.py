"""Copy-only Weather gateway install; exactly one selected routing data fork may change."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import plistlib
import re
import xml.etree.ElementTree as ET
import shutil
import struct
import tempfile
import install as base
H, W = base.H, base.W
digest, inventory = base.digest, base.inventory
PROGRAM_PATH = "/usr/libexec/iphone1-weather-http"
PLIST_PATH = "/System/Library/LaunchDaemons/com.iphone1.weather-http.plist"
HOSTS_PATH = "/private/etc/hosts"
PARENTS = ("/usr/libexec", "/System/Library/LaunchDaemons")

def routed_hosts(old, expected):
    if hashlib.sha256(old).hexdigest() != expected:
        raise ValueError("original hosts hash mismatch")
    if len(old) > 65536 or b"\0" in old:
        raise ValueError("unsupported hosts contents")
    # Reject existing active aliases (including case/dotted spelling) rather
    # than changing or competing with a user's existing resolver policy.
    for line in old.splitlines():
        fields = line.split(b"#", 1)[0].split()
        if any(v.lower().rstrip(b".") == b"iphone-wu.apple.com" for v in fields[1:]):
            raise ValueError("Weather hosts route already exists")
    return old + (b"" if not old or old.endswith(b"\n") else b"\n") + b"# Original iPhone local Weather demo\n127.0.0.1 iphone-wu.apple.com\n"

RESOLVERS_PATH = "/System/Library/SystemConfiguration/IPMonitor.bundle/Resolvers.plist"
RESOLVER_ENTRY = {"ServerAddresses": ["127.0.0.1"], "SupplementalMatchDomains": ["iphone-wu.apple.com"]}
RESOLVER_INSERT = b"\n<dict><key>ServerAddresses</key><array><string>127.0.0.1</string></array><key>SupplementalMatchDomains</key><array><string>iphone-wu.apple.com</string></array></dict>\n"

def routed_resolvers(old, expected, *, youtube_auth=False):
    entry = {"ServerAddresses": ["127.0.0.1"], "SupplementalMatchDomains": ["iphone-wu.apple.com"] + (["dm5.google.com"] if youtube_auth else [])}
    insertion = RESOLVER_INSERT.replace(b"<string>iphone-wu.apple.com</string>", b"<string>iphone-wu.apple.com</string><string>dm5.google.com</string>") if youtube_auth else RESOLVER_INSERT
    if hashlib.sha256(old).hexdigest() != expected:
        raise ValueError("original resolvers hash mismatch")
    if len(old) > 65536 or b"\0" in old or b"<!ENTITY" in old or re.search(rb"<!DOCTYPE[^>]*\[", old):
        raise ValueError("unsupported resolvers XML")
    try:
        root = ET.fromstring(old)
        if root.tag != "plist" or len(root) != 1 or root[0].tag != "array":
            raise ValueError("resolvers must be a plist array")
        for node in root.iter("dict"):
            keys = list(node)[::2]
            if len(node) % 2 or any(k.tag != "key" for k in keys) or len({k.text for k in keys}) != len(keys):
                raise ValueError("duplicate or malformed resolver dictionary keys")
        parsed = plistlib.loads(old)
        if not isinstance(parsed, list) or any(not isinstance(v, dict) for v in parsed):
            raise ValueError("resolvers must contain dictionaries")
        for item in parsed:
            domains = item.get("SupplementalMatchDomains", [])
            if not isinstance(domains, list) or any(not isinstance(v, str) for v in domains):
                raise ValueError("malformed resolver domains")
            if any(v.lower().rstrip(".") in entry["SupplementalMatchDomains"] for v in domains):
                raise ValueError("owned resolver route already exists")
        end = re.search(rb"</array>\s*</plist>\s*\Z", old)
        if not end:
            raise ValueError("unsupported outer resolver XML ending")
        new = old[:end.start()] + insertion + old[end.start():]
        if plistlib.loads(new) != parsed + [entry]:
            raise ValueError("resolver semantic preservation failed")
        return new
    except (ET.ParseError, plistlib.InvalidFileException, TypeError, IndexError) as error:
        raise ValueError("malformed resolver XML") from error

def preflight(source, additions, expected_source, *, reuse_catalog_ids=False):
    proof = base.preflight(source, [], expected_source)
    work = W.Work(source)
    try:
        nodes = [(ident, [r[2] for r in work._leaf_records(node)]) for ident, node in work._leaf_chain()]
        used_ids = work._catalog_object_ids()
        next_id = work.next_catalog_id
        bitmap = bytearray(work.alloc)
        proof["catalog"] = []
        proof["allocations"] = []
        for guest, host, _ in additions:
            parent = work._resolve_dir(str(Path(guest).parent))
            if not parent or work.find_file_path(guest):
                raise ValueError("existing parent and absent destination required: " + guest)
            ident = next((n for n in range(16, next_id) if n not in used_ids), next_id) if reuse_catalog_ids else next_id
            used_ids.add(ident)
            if ident == next_id:
                next_id += 1
            name = Path(guest).name
            entries = [W.Work._make_key(parent, name) + bytes(248), W.Work._make_key(ident, "") + struct.pack(">HHIH", 4, 0, parent, len(name)) + name.encode("utf-16-be")]
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
                    raise ValueError("insertion changes first key or duplicates key")
                at = next((i for i,r in enumerate(rows) if work._key(r) > key), len(rows))
                rows.insert(at, row)
                used = 14 + sum(map(len, rows)) + 2 * (len(rows) + 1)
                if used > work.node_size:
                    raise ValueError("catalog leaf has insufficient space; use guest bootstrap alternative")
                proof["catalog"].append({"path":guest,"node":node_id,"usedAfter":used,"capacity":work.node_size,"cnid":ident})
            blocks = (Path(host).stat().st_size + work.bs - 1) // work.bs
            run = 0
            for block in range(work.vol.total_blocks):
                run = 0 if bitmap[block >> 3] & (0x80 >> (block & 7)) else run + 1
                if run == blocks:
                    start = block + 1 - blocks
                    for b in range(start, block + 1):
                        bitmap[b >> 3] |= 0x80 >> (b & 7)
                    proof["allocations"].append({"path":guest,"start":start,"blocks":blocks})
                    break
            else:
                if blocks:
                    raise ValueError("insufficient contiguous free blocks")
        return proof
    finally:
        work.vol.f.close()

def install(source, target, expected_source, program, expected_program, plist, expected_plist, expected_hosts=None, *, expected_resolvers=None, youtube_auth=False):
    if youtube_auth and expected_resolvers is None:
        raise ValueError("YouTube auth requires resolver configuration")
    if (expected_hosts is None) == (expected_resolvers is None):
        raise ValueError("exactly one hosts or resolvers hash is required")
    route_path = HOSTS_PATH if expected_hosts is not None else RESOLVERS_PATH
    route_hash = expected_hosts if expected_hosts is not None else expected_resolvers
    route_builder = routed_hosts if expected_hosts is not None else lambda old, expected: routed_resolvers(old, expected, youtube_auth=youtube_auth)
    source, target, program = Path(source), Path(target), Path(program)
    if target.exists() or target.with_suffix(target.suffix + ".json").exists() or source.resolve() == target.resolve():
        raise ValueError("destination must be new")
    if digest(program) != expected_program:
        raise ValueError("own executable hash mismatch")
    additions = [(PROGRAM_PATH, program, 0o755), (PLIST_PATH, Path(plist), 0o644)]
    if digest(plist) != expected_plist:
        raise ValueError("own plist hash mismatch")
    payload = plistlib.loads(Path(plist).read_bytes())
    if payload.get("ProgramArguments") != [PROGRAM_PATH] or payload.get("Label") != "com.iphone1.weather-http":
        raise ValueError("unexpected Weather launch definition")
    proof = preflight(source, additions, expected_source)
    work = W.Work(source)
    try:
        old_hosts = work.read_file_path(route_path)
        hit = work.find_file_path(route_path)
        new_hosts = route_builder(old_hosts, route_hash)
        if len(new_hosts) > hit[0].total_blocks * work.bs:
            raise ValueError("routing append exceeds existing blocks; use own guest bootstrap configuration")
    finally:
        work.vol.f.close()
    before, before_paths, before_forks = inventory(source)
    hosts_record = before_paths[route_path][2]
    if H.u16(hosts_record, 42) & 0o170000 != 0o100000:
        raise ValueError("routing file must be an existing regular file at its real path")
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
            work.replace_file_path(route_path, new_hosts)
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
            elif key == before_paths[route_path][0]:
                at = before_paths[route_path][1] + 88
                if new[:at] + new[at+8:] != raw[:at] + raw[at+8:] or H.u64(new, at) != len(new_hosts):
                    raise ValueError("unexpected hosts catalog metadata change")
            elif new != raw:
                raise ValueError("original catalog record changed")
        for guest, forks in before_forks.items():
            if guest == route_path:
                if after_forks[guest][0] != {"bytes": len(new_hosts), "sha256": hashlib.sha256(new_hosts).hexdigest()} or after_forks[guest][1] != forks[1]:
                    raise ValueError("hosts replacement differs from exact appended bytes")
            elif after_forks[guest] != forks:
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
        if digest(plist) != expected_plist:
            raise ValueError("plist changed during installation")
        report = {"passed": True, "sourceSHA256": expected_source, "sourceUnchanged": True,
                  "outputSHA256": digest(temporary), "outputBytes": temporary.stat().st_size,
                  "programSHA256": expected_program, "unchangedExistingFileForks": 2 * len(before_forks) - 1,
                  "hostsPath": HOSTS_PATH, "hostsBeforeSHA256": expected_hosts,
                  "hostsAfterSHA256": hashlib.sha256(new_hosts).hexdigest(),
                  "hostsBeforeHex": old_hosts.hex(), "hostsAfterHex": new_hosts.hex(),
                  "preservedCatalogRecords": len(before), "newCatalogRecords": len(after) - len(before),
                  "newFiles": [x[0] for x in additions], "preflight": proof}
        if expected_resolvers is not None:
            for key in list(report):
                if key.startswith("hosts"):
                    del report[key]
            report.update(resolversPath=route_path, resolversBeforeSHA256=route_hash,
                          resolversAfterSHA256=hashlib.sha256(new_hosts).hexdigest(),
                          resolversBeforeHex=old_hosts.hex(), resolversAfterHex=new_hosts.hex(), youtubeAuth=youtube_auth)
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
    p = argparse.ArgumentParser(description=__doc__)
    for key in ("source", "output", "program", "plist"):
        p.add_argument("--"+key, type=Path, required=True)
    for key in ("source-sha256", "program-sha256", "plist-sha256"):
        p.add_argument("--"+key, required=True)
    route = p.add_mutually_exclusive_group(required=True)
    route.add_argument("--hosts-sha256")
    route.add_argument("--resolvers-sha256")
    p.add_argument("--youtube-auth", action="store_true", help="Route only the additional original YouTube device-auth host")
    a = p.parse_args()
    print(json.dumps(install(a.source,a.output,a.source_sha256,a.program,a.program_sha256,a.plist,a.plist_sha256,a.hosts_sha256,expected_resolvers=a.resolvers_sha256,youtube_auth=a.youtube_auth),indent=2))
