"""Corrupt retained evidence; never execute or modify the guest."""
from pathlib import Path
import json
import shutil
import subprocess
import sys

root = Path(__file__).resolve().parent
original = (root / 'run-1/trace.log').read_text()
cases = {
    'wrong-active-second': ('COMMIT seconds1262304805', 'COMMIT seconds1262304806'),
    'wrong-fraction': ('COMMIT seconds1262304805 fraction26798', 'COMMIT seconds1262304805 fraction26799'),
    'premature-pulse': ('ticks5064026798 cycles12543421356', 'ticks5059000000 cycles12543421356'),
    'missing-pending-byte': ('mask1111111', 'mask1111110'),
    'changed-wire-byte': ('register59 value25 accepted1', 'register59 value26 accepted1'),
    'wrong-retirement-count': ('END retired45152141', 'END retired45152142'),
    'unsupported-write': ('pcc005a2ec unknownWrites0', 'pcc005a2ec unknownWrites1'),
}
results = []
for name, (old, new) in cases.items():
    assert old in original, (name, old)
    directory = root / 'artifact-negatives' / name
    directory.mkdir(parents=True, exist_ok=False)
    (directory / 'trace.log').write_text(original.replace(old, new, 1))
    shutil.copy2(root / 'run-1/run.json', directory / 'run.json')
    run = subprocess.run([sys.executable, str(root / 'audit.py'), str(directory)], capture_output=True, text=True)
    (directory / 'audit-output.log').write_text(run.stdout + run.stderr)
    assert run.returncode != 0, name
    results.append({'case': name, 'exitCode': run.returncode})
(root / 'artifact-negatives/report.json').write_text(json.dumps({'passed': True, 'guestRuns': 0, 'cases': results}, indent=2) + '\n')
print(json.dumps(results))
