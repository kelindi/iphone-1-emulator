"""Freeze V3 from accepted V2 sources, changing only directory/origins/profile identity."""
from pathlib import Path
import shutil,json,hashlib
r=Path.cwd();old=r/'.context/jit-return-visit-v2';b=Path(__file__).resolve().parent
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
pins=json.loads((old/'site-pins.json').read_text());proof=json.loads((old/'frozen-pins.json').read_text())
assert not (b/'profiles').exists() and not (b/'references').exists() and not (b/'training').exists() and not (b/'measured').exists()
for name in ['run-browser.mjs','run-series.py','cache-server.mjs','frames.mjs','frame-packet.mjs','check-receipt.py','analyze-cache.py']:
 assert sha(old/name)==proof['sources'][name],name
for v in ['original','candidate']:
 for name,expected in pins['sites'][v].items():
  source=old/v/name;assert sha(source)==expected;(b/v/name).parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,b/v/name)
pins['origins']={'original':'http://127.0.0.1:8191','candidate':'http://127.0.0.1:8192'}
pins['methodRevision']='v3 fresh profiles/origins after external-workload-contaminated V2; byte-exact V2 compact phase/cap unchanged'
(b/'site-pins.json').write_text(json.dumps(pins,indent=2)+'\n')
files=['cache-server.mjs','analyze-cache.py','check-receipt.py','frames.mjs','frames.test.mjs','frame-packet.mjs','frame-packet.test.mjs','transport-diagnostic.mjs','run-series.py','run-browser.mjs','training-review.py','audit-screens.mjs']
rows=[]
for name in files:
 before=(old/name).read_text();after=before.replace('.context/jit-return-visit-v2','.context/jit-return-visit-v3');(b/name).write_text(after)
 assert after.replace('.context/jit-return-visit-v3','.context/jit-return-visit-v2')==before,name
 rows.append(dict(name=name,sourceSha256=sha(old/name),sha256=sha(b/name),directoryOnlyProjectionExact=True))
(b/'projection-proof.json').write_text(json.dumps(dict(passed=True,scope='V2 final frozen measured/training sources identical after directory normalization; sites identical except explicit new origins metadata.',files=rows),indent=2)+'\n')
print('V3 sources/sites frozen; profiles/references absent; directory-only projection exact')
