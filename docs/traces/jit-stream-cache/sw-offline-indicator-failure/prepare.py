from pathlib import Path
import json,hashlib,shutil,subprocess
r=Path.cwd();b=r/'.context/jit-sw-cache-v2';w=r/'web';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
files=['index.html','phone.css','icon.svg','app.js','storage.js','checkpoint-worker.js','assets.js','emu-worker.js','manifest.json','dist/emu.js','dist/emu.wasm','seed-jit.js','jit/seed.wasm','jit/regions.json','jit/abi.json','service-worker.template.js']
manifest=json.loads((w/'manifest.json').read_text())
files += [name for name in manifest['assetSha256'] if not name.startswith('https://')]
originalPins={n:sha(w/n) for n in files};candidate=r/'.context/jit-stream-cache/seed-jit.js';original=(w/'seed-jit.js').read_text();new=candidate.read_text();assert original[original.index('class SeedJIT'):]==new[new.index('class SeedJIT'):]
for v in ['original','candidate']:
 root=b/v;site=root/'web'
 for n,h in originalPins.items():
  assert sha(w/n)==h;dest=site/n;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(w/n,dest)
 (root/'tools').mkdir(exist_ok=True);shutil.copy2(r/'tools/build-offline.py',root/'tools/build-offline.py')
 if v=='candidate':shutil.copy2(candidate,site/'seed-jit.js')
 subprocess.run(['python3',str(root/'tools/build-offline.py')],check=True)
for n,h in originalPins.items():assert sha(w/n)==h,'Production changed during snapshot: '+n
pins=dict(origin='http://127.0.0.1:8196',productionSource=originalPins,sites={v:{n:sha(b/v/'web'/n) for n in files+['service-worker.js']} for v in ['original','candidate']},generatorSha256=sha(r/'tools/build-offline.py'),candidateLoaderSha256=sha(candidate),sourceScope='Current production app/index/CSS/worker copied byte-exact; only seed loader and generated SW bundle differ. No production mutation.')
(b/'site-pins.json').write_text(json.dumps(pins,indent=2)+'\n')
for n,old in [('cache-server.mjs','.context/jit-return-visit-v3/cache-server.mjs'),('frame-packet.mjs','.context/jit-return-visit-v3/frame-packet.mjs'),('frames.mjs','.context/jit-return-visit-v3/frames.mjs'),('analyze-cache.py','.context/jit-return-visit-v3/analyze-cache.py'),('check-receipt.py','.context/jit-return-visit-v3/check-receipt.py')]:shutil.copy2(r/old,b/n)
print(json.dumps({'passed':True,'sites':{v:sha(b/v/'web'/'service-worker.js') for v in ['original','candidate']}}))
