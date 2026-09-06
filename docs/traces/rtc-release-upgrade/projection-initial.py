from pathlib import Path
import json,hashlib,re
p=Path(__file__).resolve().parent;r=json.loads((p/'run-1/report.json').read_text());target=p/'release-projection/web';template=(target/'service-worker.template.js').read_text();sha=lambda b:hashlib.sha256(b).hexdigest()
projection={str(f.relative_to(target)):sha(f.read_bytes()) for f in target.rglob('*') if f.is_file()};tested=(p/'run-1/web/service-worker.js').read_text();new=(target/'service-worker.js').read_text()
def parse(s):
 generation=re.search("const CACHE = 'iphone-1-shell-([^']+)';",s)[1];m=re.search(r'^const FILES = (.+);$',s,re.M);files=json.loads(m[1]);normalized=s.replace(generation,'__VERSION__').replace(m[1],'__FILES__');assert normalized==template;return generation,files
oldgen,oldfiles=parse(tested);newgen,newfiles=parse(new);assert oldgen==r['variants']['B']['generation'];assert oldfiles==r['variants']['B']['hashes'];assert newfiles=={n:projection[n] for n in newfiles};assert set(oldfiles)==set(newfiles)
changed=[n for n in oldfiles if oldfiles[n]!=newfiles[n]];assert set(changed)=={'app.js','manifest.json'}
assert (p/'run-1/web/app.js').read_bytes()==(target/'app.js').read_bytes()+b"\ndocument.documentElement.dataset.testBundle = 'B';\n"
m=json.loads((p/'run-1/web/manifest.json').read_text());assert m.pop('testBundle')=='B';assert m==json.loads((target/'manifest.json').read_text())
for n in ['dist/emu.js','dist/emu.wasm','seed-jit.js','jit/seed.wasm','jit/regions.json','jit/abi.json','index.html','phone.css','emu-worker.js','storage.js','assets.js']:
 assert sha((p/'run-1/web'/n).read_bytes())==projection[n]
previous=json.loads((p/'source-pins.json').read_text())['previous'];changedRelease=[n for n in projection if projection[n]!=previous[n]];assert set(changedRelease)=={'dist/emu.js','dist/emu.wasm','seed-jit.js','service-worker.js'}
result={'passed':True,'testedBGeneration':oldgen,'testedBSWSHA256':sha(tested.encode()),'releaseGeneration':newgen,'releaseSWSHA256':sha(new.encode()),'templateSHA256':sha(template.encode()),'sameTemplateVerified':True,'actualTestedCoreLoaderSeedAndUIExact':True,'diagnosticMarkerDifferences':changed,'releaseChangesVsActualPrevious':changedRelease,'note':'Release projection removes only diagnostic app/manifest generation markers and regenerates service-worker bytes with the unchanged maintained build-offline.py. The exact tested RTC core, streaming loader, seed, ABI, UI and R2 manifest semantics are retained. This projected release SW byte sequence has not itself been browser-tested; root owns final integration/smoke.','projection':projection}
(p/'release-projection.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:v for k,v in result.items() if k!='projection'},indent=2))
