#!/usr/bin/env python3
"""One cache generation covers matching frontend, WASM, and firmware manifest."""
import hashlib
import json
from pathlib import Path

root = Path(__file__).resolve().parent.parent
web = root / 'web'
files = ['index.html', 'phone.css', 'icon.svg', 'app.js', 'storage.js',
         'phone-view.js', 'phone-model.js', 'vendor/three/three.module.js', 'vendor/three/three.core.js',
         'checkpoint-worker.js', 'assets.js', 'emu-worker.js', 'manifest.json', 'dist/emu.js', 'dist/emu.wasm']
# The seed and its C ABI metadata must travel with the matching interpreter.
# A build with acceleration disabled can omit generated seed artifacts; the
# controller then falls back to the interpreter without blocking startup.
if (web / 'seed-jit.js').exists():
    files.append('seed-jit.js')
seed_files = ['jit/seed.wasm', 'jit/regions.json', 'jit/abi.json']
if any((web / name).exists() for name in seed_files):
    if not all((web / name).exists() for name in seed_files):
        raise SystemExit('Incomplete browser instruction seed; rebuild before packaging offline')
    files.extend(seed_files)
template = (web / 'service-worker.template.js').read_text()
digest = hashlib.sha256(template.encode())
for name in files:
    digest.update(name.encode())
    digest.update((web / name).read_bytes())
version = digest.hexdigest()[:24]
hashes = {name: hashlib.sha256((web / name).read_bytes()).hexdigest() for name in files}
output = template.replace('__VERSION__', version).replace('__FILES__', json.dumps(hashes))
(web / 'service-worker.js').write_text(output)
print('Offline bundle:', version)
