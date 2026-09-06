from pathlib import Path
import hashlib, json, os, subprocess

root = Path('.context/jit-register-layout').resolve()
sha = lambda path: hashlib.sha256(Path(path).read_bytes()).hexdigest()
runtime_sha = '603679fbc7961ddad95ac8822257074fecde5940bc013246d520ea3b6240c84f'
assert sha(root / 'runtime/emu.wasm') == runtime_sha
assert (root / 'seed/regions.json').read_bytes() == Path('web/jit/regions.json').read_bytes()
fixtures = [
    ('mbx', '.context/graphics-speed/fixed-dismiss.snap',
     '764486ea35ba9832f35c8118397f361026ed21119295321e0a29519509de1244',
     '.context/jit-instruction-guards/guest-proof.json'),
    ('software', '.context/lockdown-root/usb-ready-home.snap',
     '28591a3003b8677469b2e506185792e26ac128ba9383b7f110959f4f4fb0c82d',
     '.context/jit-instruction-guards/software-proof.json'),
]
report = {'scope': 'Fresh candidate full raw600M replay against previously verified same-runtime interpreter/original checkpoints; no normalization. Same original generated counters required. Concurrent functional work, no timing claim.',
          'runtimeSha256': runtime_sha, 'seedSha256': sha(root / 'seed/seed.wasm'), 'runs': []}
for name, fixture, fixture_sha, reference_path in fixtures:
    assert sha(fixture) == fixture_sha
    reference = json.loads(Path(reference_path).read_text())
    assert reference['passed'] and len(reference['checkpoints']) == 6
    expected = [row['hash'] for row in reference['checkpoints']]
    assert [row['hash'] for row in reference['baseline']] == expected
    env = dict(os.environ, REPLAY_MODULE=(root / 'runtime/emu.js').as_uri(),
               REPLAY_JIT=str(root / 'seed'), FIXTURE_SNAPSHOT=fixture,
               REPLAY_SEEDED_ONLY='1', REPLAY_EXPECTED_HASHES=json.dumps(expected),
               INSTRUCTIONS='600000000')
    with (root / (name + '-replay.log')).open('w') as log:
        code = subprocess.run(['node', str(root / 'replay.mjs')], env=env,
                              stdout=log, stderr=subprocess.STDOUT).returncode
    assert code == 0, (name, code)
    rows = [json.loads(line) for line in (root / (name + '-replay.log')).read_text().splitlines()
            if line.startswith('{') and '"done"' in line]
    assert len(rows) == 6 and [row['hash'] for row in rows] == expected
    for actual, original in zip(rows, reference['baseline']):
        for key in ['done', 'hash', 'tlb', 'generated', 'calls', 'regions']:
            assert actual[key] == original[key], (name, key, actual, original)
    report['runs'].append({'fixture': name, 'fixtureSha256': fixture_sha,
                           'referenceSource': reference_path,
                           'referenceSha256': sha(reference_path),
                           'checkpoints': rows, 'allRawAndCountersEqual': True})
    print(name, 'six full raw and counter checkpoints PASS', flush=True)
    (root / 'proof.json').write_text(json.dumps(report, indent=2) + '\n')
report['passed'] = True
(root / 'proof.json').write_text(json.dumps(report, indent=2) + '\n')
