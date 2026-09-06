#!/usr/bin/env python3
"""Refresh checked-in asset hashes after intentionally rebuilding firmware."""
import hashlib
import json
from pathlib import Path
from urllib.parse import urlparse

root = Path(__file__).resolve().parents[2] / 'web'
path = root / 'manifest.json'
manifest = json.loads(path.read_text())
hashes = {}
remote_origin = 'https://iphone.files.garden'
for key in ('kernel', 'devicetree', 'rootfs', 'snapshot'):
    names = manifest.get(key, [])
    for name in [names] if isinstance(names, str) else names:
        parsed = urlparse(name)
        if parsed.scheme:
            if f'{parsed.scheme}://{parsed.netloc}' != remote_origin:
                raise ValueError(f'Asset uses an unapproved host: {name}')
            source = (root / 'firmware' / Path(parsed.path).name).resolve()
        else:
            source = (root / name).resolve()
        if root.resolve() not in source.parents:
            raise ValueError(f'Asset is outside web root: {name}')
        digest = hashlib.sha256()
        with source.open('rb') as file:
            for chunk in iter(lambda: file.read(1048576), b''):
                digest.update(chunk)
        hashes[name] = digest.hexdigest()
manifest['assetSha256'] = hashes
path.write_text(json.dumps(manifest, indent=2) + '\n')
print(f'Updated {len(hashes)} asset hashes in {path}')
