"""Post-run metadata/identity/overhead audit; no guest or runtime mutation."""
import json,math,statistics,hashlib
from pathlib import Path
root=Path('.context/input-delivery/run-1');r=json.loads((root/'report.json').read_text());c=json.loads((root/'controller.json').read_text());rows=r['samples'];assert len(rows)==60 and c['passed'] and not c['remainingProcesses']
def stats(a):
 a=sorted(a);return dict(count=len(a),medianMs=statistics.median(a),p95Ms=a[math.ceil(.95*len(a))-1],maxMs=a[-1])
for name,digest in r['expectedAssets'].items():
 hits=[a for a in r['assets'] if a['url'].split('/')[-1]==name];assert len(hits)==1 and hits[0]['sha256']==digest
assert r['jit']['enabled'] and r['jit']['retired']>0
frames=[f for row in rows for f in row['frames']]
for row in rows:
 assert [f['sequence'] for f in row['frames']]==sorted(set(f['sequence'] for f in row['frames']))
 assert row['firstButton']['sequence']==next(f['sequence'] for f in row['frames'] if f['changed']>=16)
 assert row['firstResult']['sequence']==next(f['sequence'] for f in row['frames'] if f['comparison']['passed'])
 for f in row['frames']:
  assert f['sourceCanvasExact'] and row['down']<=f['receivedAt']<=f['sourceCopiedAt']<=f['beforeNativeAt']<=f['canvasDoneAt']<=f['observerDoneAt']
metrics={}
for key in ['9','C']:
 a=[x for x in rows if x['key']==key];assert len(a)==30
 sources={'receiptButton':lambda x:x['firstButton']['receivedAt']-x['down'],'canvasButton':lambda x:x['firstButton']['canvasDoneAt']-x['down'],'rafButton':lambda x:x['rafFirstButton']['at']-x['down'],'canvasResult':lambda x:x['firstResult']['canvasDoneAt']-x['down'],'rafResult':lambda x:x['rafFirstResult']['at']-x['down'],'releaseCanvasResult':lambda x:x['firstResult']['canvasDoneAt']-x['up'],'releaseRafResult':lambda x:x['rafFirstResult']['at']-x['up']}
 metrics[key]={name:stats([f(x) for x in a]) for name,f in sources.items()}
 for name,s in metrics[key].items():
  for field,value in s.items():assert abs(r['summary'][key][name][field]-value)<1e-6
out=dict(passed=True,scope='Same-run measured rows only; source/canvas ROI identity. Observer overhead is included and not subtracted. Receipt is early page listener invocation, not worker send or browser message enqueue time.',reportSHA256=hashlib.sha256((root/'report.json').read_bytes()).hexdigest(),metrics=metrics,actualFrameCount=len(frames),overhead={name:stats([f[end]-f[start]for f in frames])for name,start,end in [('sourceCopy','receivedAt','sourceCopiedAt'),('nativePut','beforeNativeAt','canvasDoneAt'),('postPutObserver','canvasDoneAt','observerDoneAt'),('receiptToCanvas','receivedAt','canvasDoneAt')]},cadence={'pulse':stats([x['up']-x['down']for x in rows]),'cropTransport':stats([x['transportMs']for x in rows]),'cropWrite':stats([x['cropWriteMs']for x in rows]),'betweenDowns':stats([b['down']-a['down']for a,b in zip(rows,rows[1:])]),'sameActualRafButtonFrame':sum(x['firstButton']['sequence']==x['rafFirstButton']['sequence']for x in rows),'sameActualRafResultFrame':sum(x['firstResult']['sequence']==x['rafFirstResult']['sequence']for x in rows),'rafFirstButtonMinusActual':stats([x['rafFirstButton']['at']-x['firstButton']['canvasDoneAt']for x in rows]),'sameFrameRafLag':stats([e['at']-e['canvasDoneAt']for x in rows for e in x['raf']]),'savedAtValues':sorted(set(x['savedAt']for x in rows))},assetsExact=6,metadataFirstEventSelection=True)
(root/'analysis.json').write_text(json.dumps(out,indent=2));print(json.dumps(out,indent=2))
