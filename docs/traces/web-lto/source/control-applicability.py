from pathlib import Path
import json,hashlib,gzip,shutil
p=Path('.context/web-lto');ref=Path('.context/jit-fallback-census/control-100m.json');r=json.loads(ref.read_text());sha=lambda b:hashlib.sha256(b).hexdigest()
assert r['passed'] and r['instructions']==100000000 and r['definitionCount']==r['initialActive']==512
assert sha((p/'control/emu.wasm').read_bytes())==r['moduleSha256']=='7a72add6d886440f64a853511375ba51cbbb23f2263831b6a05c51fde7969088'
assert sha(Path('web/jit/seed.wasm').read_bytes())==r['seedSha256']
fixture=Path(r['fixturePath']);assert sha(fixture.read_bytes())==r['fixtureSha256']
witnesses=[]
for row in r['checkpoints']:
 f=Path('.context/jit-mixed-coverage')/('candidate-raw-'+str(row['done'])+'.snap.gz');h=hashlib.sha256();size=0
 with gzip.open(f,'rb')as stream:
  while True:
   b=stream.read(1048576)
   if not b:break
   h.update(b);size+=len(b)
 assert h.hexdigest()==row['rawSha256'] and size==row['bytes']
 witnesses.append({'path':str(f),'gzipSha256':sha(f.read_bytes()),'rawSha256':h.hexdigest(),'rawBytes':size,'done':row['done']})
manifest=json.loads(Path('web/manifest.json').read_text());inputs={}
for n in ['kernel_1A543a.macho','devicetree_m68ap.bin']:
 f=Path('web/firmware')/n;h=sha(f.read_bytes());assert h==manifest['assetSha256']['https://iphone.files.garden/'+n];inputs[str(f)]=h
for n in ['web/jit/seed.wasm','web/jit/abi.json','web/jit/regions.json',str(fixture),str(ref)]:inputs[n]=sha(Path(n).read_bytes())
shutil.copy2(ref,p/'retained-control-100m.json')
out={'passed':True,'controlReportSha256':sha(ref.read_bytes()),'controlModuleByteIdentical':True,'fixedInputHashes':inputs,'rawWitnesses':witnesses,'checkpointInstructions':[x['done']for x in r['checkpoints']],'scope':'Retained original7a72 seeded report applies to exact same fixture, seed, admission count and 1M run/5M touch-release schedule. Independent historical gzip witnesses match complete raw hashes, although produced by the separate coverage candidate whose JIT counters differ; counters are taken only from the original control report. Candidate interpreter must match raw state with zero JIT retirement; candidate seeded must additionally match original generated/call/region counters. No guest state normalization.','freeBytes':shutil.disk_usage('.').free}
(p/'control-applicability.json').write_text(json.dumps(out,indent=2));print(json.dumps({'passed':True,'witnesses':len(witnesses),'freeBytes':out['freeBytes']}))
