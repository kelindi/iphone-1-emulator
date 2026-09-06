from pathlib import Path
import json,hashlib,shutil,gzip,difflib
b=Path('.context/jit-sw-cache-v4');d=Path('docs/traces/jit-stream-cache/sw-cache-offline');d.mkdir(exist_ok=True);r=json.loads((b/'run/series.json').read_text());sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
for p in b.iterdir():
 if p.is_file() and p.suffix in ['.py','.mjs','.json','.log']:shutil.copy2(p,d/p.name)
for p in (b/'run').rglob('*'):
 if p.is_file() and p.suffix in ['.json','.log','.png','.rgba']:
  q=d/p.relative_to(b);q.parent.mkdir(parents=True,exist_ok=True)
  if p.name=='trace.json' or p.suffix=='.rgba':
   with p.open('rb') as src,gzip.open(str(q)+'.gz','wb',compresslevel=6) as out:shutil.copyfileobj(src,out)
  else:shutil.copy2(p,q)
for folder in ['stopped-server-smoke']:
 for n in ['report.json']:
  p=b/folder/n;q=d/folder/n;q.parent.mkdir(exist_ok=True);shutil.copy2(p,q)
manual={'passed':True,'scope':'All10actual captured guest stages viewed: originalHome and savedSettings fullpage, four cycle guestPNGs, autosaveSettings guestPNG, allthree return fullpagePNGs. No reconstructed/rendered substitutions.','frames':[{'phase':x['phase'],'name':f['name'],'pngSha256':sha(b/'run'/f"{x['index']}-{x['phase']}"/(f['name']+'.png')),'rawSha256':f['rawSha256'],'strictPassed':f['stable']['passed'] and f['reference']['passed']} for x in r['runs'] for f in x['report']['frames']]};(b/'manual-review.json').write_text(json.dumps(manual,indent=2)+'\n');shutil.copy2(b/'manual-review.json',d/'manual-review.json')
summary={'passed':r['passed'],'checks':sum(len(x['report']['checks']) for x in r['runs']),'frames':10,'processIdentitiesClosed':sum(len(x['scopedProcessesClosed']) for x in r['runs']),'scope':'Offline mechanism/functionality, no speed claim; explicit setup prefetched firmware, so ordinary-first-load/upgrade remains separate. Physical origin shutdown coversbrowsermanagedSWupdate; contextoffline plus3directnoncefailedrequests preserved. Remote R2 origin itself was not physically shut down.','nativeAcceptedBytes':r['runs'][1]['report']['cacheReceipt']['analysis']['seedAccepted'][0]['hits'][0]['args']['consumedCacheSize'],'saved':r['runs'][0]['report']['closingSaved'],'durationsSeconds':[x['elapsedSeconds'] for x in r['runs']],'productionUnchanged':True};(b/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');shutil.copy2(b/'summary.json',d/'summary.json')
p=Path('.context/jit-stream-cache/promotion');old=Path('web/seed-jit.js').read_text();candidate=(p/'web/seed-jit.js').read_text();test=(p/'web/tests/jit-loader.test.mjs').read_text();patch=''.join(difflib.unified_diff(old.splitlines(True),candidate.splitlines(True),fromfile='a/web/seed-jit.js',tofile='b/web/seed-jit.js'))+''.join(difflib.unified_diff([],test.splitlines(True),fromfile='/dev/null',tofile='b/web/tests/jit-loader.test.mjs'));(p/'promotion.patch').write_text(patch)
print(summary)
