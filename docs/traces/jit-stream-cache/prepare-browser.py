from pathlib import Path
import shutil,json,hashlib,difflib
r=Path.cwd();base=r/'.context/jit-stream-cache';site=base/'site';site.mkdir(exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
files=['index.html','phone.css','icon.svg','app.js','storage.js','checkpoint-worker.js','assets.js','emu-worker.js','manifest.json','seed-jit.js','service-worker.js','dist/emu.js','dist/emu.wasm','jit/seed.wasm','jit/regions.json','jit/abi.json']
manifest=json.loads((r/'web/manifest.json').read_text());files+=list(manifest['assetSha256'])
for name in files:
 p=site/name;p.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(r/'web'/name,p)
shutil.copy2(base/'seed-jit.js',site/'seed-jit.js')
p=site/'emu-worker.js';old=p.read_text();new='let diagnosticRetired = 0;\n'+old.replace('const ran = api.run(50000);','const ran = api.run(50000); diagnosticRetired += ran;').replace("case 'idle':", "case 'profileCounters': send('profileCounters', {id:message.id, retired:diagnosticRetired, ticks:api.ticks(), cycles:String(api.cycles()), jit:jit?.stats(), at:performance.now()}); return;\n      case 'idle':")
assert old.count('const ran = api.run(50000);')==1 and old.count("case 'idle':")==1;p.write_text(new)
(base/'worker-observer.patch').write_text(''.join(difflib.unified_diff(old.splitlines(True),new.splitlines(True),fromfile='a/web/emu-worker.js',tofile='b/diagnostic/emu-worker.js')))
refs=base/'references';refs.mkdir(exist_ok=True)
referenceRoot=r/'.context/jit-lazy-groups/quiet-first-use/1-original'
for label in ['C0','H1']:
 for kind in ['settings','home']:
  for ext in ['rgba','png']:shutil.copy2(referenceRoot/f'{label}-{kind}.{ext}',refs/f'{label}-{kind}.{ext}')
pins=dict(site={name:sha(site/name) for name in files},fixture={'path':'.context/graphics-speed/fixed-dismiss.snap','sha256':sha(r/'.context/graphics-speed/fixed-dismiss.snap')},references={p.name:sha(p) for p in sorted(refs.iterdir())},referenceProvenance=str(referenceRoot.relative_to(r)),chrome={'path':'/opt/google/chrome/chrome','sha256':sha(Path('/opt/google/chrome/chrome'))},productionControllerSha256=sha(r/'web/seed-jit.js'),productionWorkerSha256=sha(r/'web/emu-worker.js'))
assert pins['site']['app.js']==sha(r/'web/app.js')
assert pins['site']['dist/emu.wasm']=='b3237ef3843412aea164d3db111299650ea8433bf16311417116799959ab6606'
assert pins['site']['jit/seed.wasm']=='d9c661556ab17bb8433ed9b0c3ecfd52e2fcafe58221b4cb84a920e4976b3881'
(base/'browser-pins.json').write_text(json.dumps(pins,indent=2)+'\n')
print('Frozen',len(files),'files; original production app, observer-only worker, isolated loader')
