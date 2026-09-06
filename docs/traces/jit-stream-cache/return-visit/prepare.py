from pathlib import Path
import shutil,json,hashlib
r=Path.cwd();b=r/'.context/jit-return-visit';old=r/'.context/jit-stream-cache';pins=json.loads((old/'browser-pins.json').read_text());sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();out={}
for variant in ['original','candidate']:
 site=b/variant;site.mkdir(exist_ok=True)
 for name,expected in pins['site'].items():
  source=old/'site'/name;assert sha(source)==expected,name;p=site/name;p.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,p)
 loader=old/('original-seed-jit.js' if variant=='original' else 'seed-jit.js');shutil.copy2(loader,site/'seed-jit.js')
 out[variant]={name:sha(site/name) for name in pins['site']}
assert [n for n in out['original'] if out['original'][n]!=out['candidate'][n]]==['seed-jit.js']
proof=dict(sites=out,fixture=pins['fixture'],chrome=pins['chrome'],origins={'original':'http://127.0.0.1:8187','candidate':'http://127.0.0.1:8188'},headers={'seed':'Cache-Control: no-cache; strong SHA256ETag; actualmtime; correct304'},productionControllerSha256=sha(r/'web/seed-jit.js'),productionAppSha256=sha(r/'web/app.js'),productionWorkerSha256=sha(r/'web/emu-worker.js'),scope='Only loader differs; both observer workers match, original b3237 core/d9c661 seed and normalSave')
(b/'site-pins.json').write_text(json.dumps(proof,indent=2)+'\n');shutil.copy2(old/'cache-server.mjs',b/'cache-server.mjs');shutil.copy2(old/'analyze-cache.py',b/'analyze-cache.py')
p=b/'analyze-cache.py';s=p.read_text().replace('def analyze_events(events,session):',"def analyze_events(events,session,seed_url='http://127.0.0.1:8186/jit/seed.wasm'):").replace("x['url']=='http://127.0.0.1:8186/jit/seed.wasm'","x['url']==seed_url");p.write_text(s)
print('Frozen both sites; loader-only diff, unchanged production')
