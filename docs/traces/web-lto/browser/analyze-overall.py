from pathlib import Path
import json,math,statistics,hashlib
b=Path('.context/web-lto/browser');s=json.loads((b/'continuation/series.json').read_text());assert s['passed']
first=json.loads((b/'measured/1-original/report-partial.json').read_text());reports=[first]+[r['report'] for r in s['runs']]
def stats(a):return {'n':len(a),'medianMs':statistics.median(a),'p95Ms':sorted(a)[math.ceil(.95*len(a))-1],'minMs':min(a),'maxMs':max(a),'samplesMs':a}
rows=[]
for i,r in enumerate(reports,1):
 row={'arm':i,'variant':r['variant'],'stages':len(r['actions']),'first':{},'warm':{}}
 for kind in ['settings','home']:
  a=[a for a in r['actions']if a['kind']==kind];assert len(a)==11
  row['first'][kind]=a[0]['releaseToReadyMs'];row['warm'][kind]=stats([x['releaseToReadyMs']for x in a[1:]])
 rows.append(row)
pooled={v:{kind:stats([a['releaseToReadyMs']for r in reports if r['variant']==v for a in r['actions']if a['kind']==kind and a['round']>1])for kind in ['settings','home']}for v in ['original','candidate']}
pairs=[]
for a,bidx in [(0,1),(3,2)]:
 pair={'control':a+1,'candidate':bidx+1,'warm':{}}
 for kind in ['settings','home']:
  pair['warm'][kind]={k+'GainPercent':100*(1-rows[bidx]['warm'][kind][k]/rows[a]['warm'][kind][k])for k in ['medianMs','p95Ms']}
 pairs.append(pair)
out={'continuationPassed':True,'originalSeriesPassed':False,'firstControlRecoveredWithFailedClosureAndTruncatedVideo':True,'rows':rows,'pooled':pooled,'pairs':pairs,'scope':'Fresh-profile core-only LTO. First original arm finalizer failed after all22 stages; recovered artifact checks pass but graceful closure/full video unavailable. Three unstarted arms continued separately. No uninterrupted successful ABBA claim.'}
(b/'overall-metrics.json').write_text(json.dumps(out,indent=2)+'\n')
print(json.dumps({'rows':[{**r,'warm':{k:{x:v for x,v in t.items()if x!='samplesMs'}for k,t in r['warm'].items()}}for r in rows],'pooled':{k:{a:{x:v for x,v in z.items()if x!='samplesMs'}for a,z in t.items()}for k,t in pooled.items()},'pairs':pairs},indent=2))
