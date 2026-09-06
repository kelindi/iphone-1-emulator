from pathlib import Path
import json,statistics,math
p=Path('.context/jit-mixed-coverage');first=json.loads((p/'measured/series.json').read_text());last=json.loads((p/'fourth-continuation/report.json').read_text());runs=[{'arm':r['index'],'variant':r['variant'],'path':str(p/'measured'/f"{r['index']}-{r['variant']}"),'report':r['report']}for r in first['runs']]+[{'arm':4,'variant':'original','path':str(p/'fourth-continuation/4-original'),'report':last['browser']}]
def stats(values):
 x=sorted(values);return {'count':len(x),'medianMs':statistics.median(x),'p95Ms':x[math.ceil(.95*len(x))-1],'meanMs':statistics.mean(x),'minMs':min(x),'maxMs':max(x),'valuesMs':values}
result={'functionalPassed':all(r['report']['passed']for r in runs),'compilationPassed':last['passed'],'scope':'Original incomplete3-arm series plus separately authorized unstartedfourth control; diskcleanup gap disclosed, no repeated guest or dropped samples. Fresh-profile/pausedImport comparison, not cachedreturnvisit.','arms':[],'pooled':{},'pairs':[]}
for r in runs:
 actions=r['report']['actions'];result['arms'].append({'arm':r['arm'],'variant':r['variant'],'path':r['path'],'first':{k:next(a['releaseToReadyMs']for a in actions if a['kind']==k)for k in ['settings','home']},'warm':{k:stats([a['releaseToReadyMs']for a in actions if a['kind']==k and a['round']>1])for k in ['settings','home']},'frames':sum(a['validation']['passed']for a in actions)})
for v in ['original','candidate']:
 result['pooled'][v]={k:stats([a['releaseToReadyMs']for r in runs if r['variant']==v for a in r['report']['actions']if a['kind']==k and a['round']>1])for k in ['settings','home']}
for oi,ci in [(0,1),(3,2)]:
 o,c=result['arms'][oi],result['arms'][ci];result['pairs'].append({'originalArm':o['arm'],'candidateArm':c['arm'],'changes':{k:{metric+'GainPercent':(1-c['warm'][k][metric]/o['warm'][k][metric])*100 for metric in ['medianMs','p95Ms','meanMs']}for k in ['settings','home']}})
(p/'browser-summary.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({**result,'arms':[{**r,'warm':{k:{x:y for x,y in st.items()if x!='valuesMs'}for k,st in r['warm'].items()}}for r in result['arms']],'pooled':{v:{k:{x:y for x,y in st.items()if x!='valuesMs'}for k,st in ks.items()}for v,ks in result['pooled'].items()}},indent=2))
