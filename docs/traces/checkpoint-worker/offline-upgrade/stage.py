"""Stage the archived upgrade inputs without duplicating immutable binaries."""
from pathlib import Path
import hashlib
import json
import shutil
import sys

archive = Path(__file__).resolve().parent
destination = Path(sys.argv[1]).resolve()
current = Path('web').resolve()
hashes = json.loads((archive / 'source-hashes.json').read_text())
assert not destination.exists(), 'Choose a fresh reproduction directory'
sources = []
for label, entries in hashes.items():
    for name, metadata in entries.items():
        source = archive / label / name if metadata['included'] else current / name
        data = source.read_bytes()
        assert len(data) == metadata['bytes'], (label, name, 'size')
        assert hashlib.sha256(data).hexdigest() == metadata['sha256'], (label, name, 'hash')
        sources.append((label, name, source, metadata['included']))
for label, name, source, included in sources:
    target = destination / label / name
    target.parent.mkdir(parents=True, exist_ok=True)
    if included:
        shutil.copy2(source, target)
    else:
        target.symlink_to(source)
(destination / 'new-shell' / 'firmware').symlink_to(current / 'firmware', target_is_directory=True)
print(destination)
