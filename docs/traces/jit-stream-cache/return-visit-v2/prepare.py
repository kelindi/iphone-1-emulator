from pathlib import Path
import shutil,json,hashlib
r=Path.cwd();old=r/'.context/jit-return-visit';b=r/'.context/jit-return-visit-v2';pins=json.loads((old/'site-pins.json').read_text());sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
for v in ['original','candidate']:
 for name,expected in pins['sites'][v].items():
  source=old/v/name;assert sha(source)==expected;(b/v/name).parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,b/v/name)
pins['origins']={'original':'http://127.0.0.1:8189','candidate':'http://127.0.0.1:8190'};pins['methodRevision']='v2 compact byte-exact frame transfer; original v1 failed profiles and sources retained'
(b/'site-pins.json').write_text(json.dumps(pins,indent=2)+'\n')
for name in ['cache-server.mjs','analyze-cache.py','check-receipt.py','frames.mjs','frames.test.mjs','run-series.py','run-browser.mjs','training-review.py']:
 s=(old/name).read_text().replace('.context/jit-return-visit/', '.context/jit-return-visit-v2/').replace("'.context/jit-return-visit'", "'.context/jit-return-visit-v2'");(b/name).write_text(s)
p=b/'run-series.py';s=p.read_text().replace("'frames.mjs','check-receipt.py'", "'frames.mjs','frame-packet.mjs','check-receipt.py'");p.write_text(s)
print('v2 frozen sites; original v1 profiles/references untouched')
