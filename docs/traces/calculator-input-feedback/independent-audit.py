from pathlib import Path
import json,hashlib,math,statistics,copy,collections
from urllib.parse import urlparse
p=Path('.context/input-feedback');r=json.loads((p/'run-2/report.json').read_text());pins=json.loads((p/'pins.json').read_text());sha=lambda x:hashlib.sha256(x.read_bytes()).hexdigest()
def stats(x):
 x=sorted(x);return {'count':len(x),'medianMs':statistics.median(x),'p95Ms':x[math.ceil(.95*len(x))-1],'maxMs':max(x)}
def eq(a,b):assert abs(a-b)<1e-6,(a,b)
def audit(d):
 assert d['errors']==[] and len(d['samples'])==60 and len(d['warmup'])==2
 assert d['correctnessPassed']is True and d['latencyPassed']is False and d['passed']is False
 assert d['jit']['enabled']and d['jit']['retired']>0
 for file in ['dist/emu.wasm','jit/seed.wasm','app.js','emu-worker.js','seed-jit.js','manifest.json']:
  name=file.split('/')[-1];expected=pins['web/'+file];assert d['expectedAssets'][name]==expected
  rows=[x for x in d['assets']if urlparse(x['url']).path=='/'+file];assert rows and all(x['sha256']==expected for x in rows)
 for n,x in enumerate(d['samples']):
  assert x['round']==n//2+1;assert x['key']==('9'if n%2==0 else'C');assert x['expected']==('9'if n%2==0 else'0')
  assert not x['timedOut'] and x['down']<x['up'];eq(x['pulseMs'],x['up']-x['down'])
  assert x['confirmation']['passed']and x['confirmation']['maxRGBDelta']<=2 and x['confirmation']['alphaExact']
  samples=x['samples'];assert samples and all(y['at']>=x['down']for y in samples)
  assert all(a['at']<b['at']and int(a['frame'])<int(b['frame'])for a,b in zip(samples,samples[1:]))
  for y in samples:assert y['comparison']['passed']==(y['comparison']['maxRGBDelta']<=2 and y['comparison']['alphaExact'])
  button=next(y['at']for y in samples if y['changed']>=16);result=next(y['at']for y in samples if y['comparison']['passed']);eq(x['firstButton'],button);eq(x['firstResult'],result)
  eq(x['downToButtonMs'],button-x['down']);eq(x['downToResultMs'],result-x['down']);eq(x['releaseToResultMs'],result-x['up'])
 out={}
 for k in ['9','C']:
  rows=[x for x in d['samples']if x['key']==k];assert len(rows)==30;out[k]={}
  for field,name in [('downToButtonMs','button'),('downToResultMs','result'),('releaseToResultMs','releaseResult')]:
   st=stats([x[field]for x in rows]);out[k][name]=st
   for key,value in st.items():eq(d['summary'][k][name][key],value)
  assert d['summary'][k]['missingButtonFeedback']==0;assert out[k]['button']['p95Ms']>150
 return out
summary=audit(r);negatives=[]
mutations={'wrongExpected':lambda x:x['samples'][0].update(expected='0'),'highRGBFalsePass':lambda x:x['samples'][0]['confirmation'].update(maxRGBDelta=3),'missingAlpha':lambda x:x['samples'][0]['confirmation'].update(alphaExact=False),'droppedPress':lambda x:x['samples'].pop(),'wrongReleaseLatency':lambda x:x['samples'][0].update(releaseToResultMs=0),'falseEarlierHighlight':lambda x:x['samples'][0].update(firstButton=x['samples'][0]['firstButton']-1),'badAssetHash':lambda x:x['assets'][0].update(sha256='0'*64),'wrongPercentile':lambda x:x['summary']['9']['button'].update(p95Ms=150)}
for name,mutate in mutations.items():
 x=copy.deepcopy(r);mutate(x)
 try:audit(x)
 except(AssertionError,StopIteration):negatives.append({'name':name,'rejected':True})
 else:raise AssertionError('negative accepted '+name)
intervals=[b['at']-a['at']for x in r['samples']for a,b in zip(x['samples'],x['samples'][1:])];gaps=[int(b['frame'])-int(a['frame'])for x in r['samples']for a,b in zip(x['samples'],x['samples'][1:])]
report={'passed':True,'run2ReportSha256':sha(p/'run-2/report.json'),'sourceMatchesPin':sha(Path('web/tests/input-feedback.mjs'))==pins['web/tests/input-feedback.mjs'],'requiredAssets':6,'presses':60,'perKey':summary,'negativeControls':negatives,'cadence':{'betweenNewFrameSamplesMs':stats(intervals),'sequenceGaps':dict(collections.Counter(gaps)),'intervalsSkippingAtLeastOneFrame':sum(x>1 for x in gaps),'firstButtonIsFirstObservedFrame':sum(x['firstButton']==x['samples'][0]['at']for x in r['samples']),'firstButtonEqualsFirstResult':sum(x['firstButton']==x['firstResult']for x in r['samples']),'pulseMs':stats([x['pulseMs']for x in r['samples']]),'savedAtValues':sorted(set(x['savedAt']for x in r['samples']))},'scope':'Independent source/log-derived result and percentile audit.60 per-press comparison records pass strict<=2RGB/exactalpha; individual rawpixel crops were not retained, so this cannot independently recompute all60 pixel comparisons. Original independently hashed references are verified by browser source. RAF observation delays are included; no hardware/guest-vs-host attribution or retroactive threshold change.'}
assert report['sourceMatchesPin'];(p/'independent-audit.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:v for k,v in report.items()if k!='negativeControls'},indent=2))
