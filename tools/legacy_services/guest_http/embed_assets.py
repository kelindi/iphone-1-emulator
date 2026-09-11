"""Embed only the two owned local YouTube fixture files; no remote input."""
import hashlib
import json
from pathlib import Path


def embed(directory=Path(__file__).resolve().parent):
    rows = ['/* Generated from owned assets/clip-1 files by embed_assets.py. */',
            '#ifndef OWN_YOUTUBE_ASSETS_H', '#define OWN_YOUTUBE_ASSETS_H']
    manifest = {}
    for ext, name in [('mp4', 'own_demo_mp4'), ('jpg', 'own_demo_jpg')]:
        path = directory / 'assets' / ('clip-1.' + ext)
        data = path.read_bytes()
        if not 0 < len(data) < 30000:
            raise ValueError('asset must fit the bounded HTTP response')
        manifest[path.name] = {'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
        rows.append('static const unsigned char ' + name + '[] = {')
        rows.extend(' ' + ','.join('0x%02x' % c for c in data[i:i+16]) + ','
                    for i in range(0, len(data), 16))
        rows.append('};')
    rows.append('#endif')
    (directory / 'youtube_assets.h').write_text('\n'.join(rows) + '\n')
    (directory / 'assets' / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    return manifest


if __name__ == '__main__':
    print(json.dumps(embed(), indent=2))
