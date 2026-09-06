"""Conservative trace-only binding; never equates an HTTP hit with native reuse."""
from pathlib import Path
import json,collections
names={'v8.wasm.compileConsume','v8.wasm.compileConsumeDone','v8.wasm.compileDigestForConsume','wasm.Deserialize','wasm.FinishAsyncCompile','wasm.OnCompilationSucceeded','v8.wasm.moduleCacheHit','v8.wasm.moduleCacheInvalid','v8.wasm.moduleCacheInvalidDigest','v8.wasm.compiledModule','v8.wasm.cachedModule'}
def analyze_events(events,session):
 threads=collections.defaultdict(list)
 for ix,e in enumerate(events):
  if e.get('name') in names or e.get('ph') in ['B','E']:threads[(e.get('pid'),e.get('tid'))].append((ix,e))
 spans=[];unmatched=[]
 for key,items in threads.items():
  stack=[]
  for ix,e in sorted(items,key=lambda t:(t[1].get('ts',0),t[0])):
   if e.get('ph')=='B':stack.append((ix,e))
   elif e.get('ph')=='E':
    if stack:
     bx,b=stack.pop()
     if b.get('name') in names:spans.append(dict(b,ph='X',dur=e['ts']-b['ts'],beginIndex=bx,endIndex=ix))
    else:unmatched.append(dict(index=ix,event=e))
   elif e.get('ph')=='X' and e.get('name') in names:spans.append(dict(e,beginIndex=ix))
  unmatched.extend(dict(index=ix,event=e) for ix,e in stack if e.get('name') in names)
 def inside(e,s):return e.get('pid')==s.get('pid') and e.get('tid')==s.get('tid') and e.get('ts',-1)>=s['ts'] and e.get('ts',-1)+e.get('dur',0)<=s['ts']+s['dur']
 relevant=[dict(e,eventIndex=n) for n,e in enumerate(events) if e.get('name') in names]
 consumed=[]
 for scope in [s for s in spans if s['name']=='v8.wasm.compileConsume']:
  hits=[e for e in relevant if e['name']=='v8.wasm.moduleCacheHit' and inside(e,scope)]
  if not hits:continue
  desc=[s for s in spans if s['name']=='wasm.Deserialize' and inside(s,scope)]
  success=[]
  for d in desc:
   finish=[s for s in spans if s['name']=='wasm.FinishAsyncCompile' and inside(s,d)]
   succeeded=[s for s in spans if s['name']=='wasm.OnCompilationSucceeded' and inside(s,d) and any(inside(s,f) for f in finish)]
   success.append(dict(scope=d,finish=len(finish),succeeded=len(succeeded),passed=len(finish)==len(succeeded)==1))
  invalid=[e for e in relevant if e['name'] in ['v8.wasm.moduleCacheInvalid','v8.wasm.moduleCacheInvalidDigest'] and inside(e,scope)]
  done=[s for s in spans if s['name']=='v8.wasm.compileConsumeDone' and inside(s,scope)]
   digests=[s for s in spans if s['name']=='v8.wasm.compileDigestForConsume' and inside(s,scope)]
  unique=len(hits)==1
  consumed.append(dict(scope=scope,hits=hits,url=hits[0].get('args',{}).get('url') if unique else None,digestScopes=len(digests),deserialization=success,invalid=invalid,accepted=unique and len(done)==1 and len(digests)==1 and inside(digests[0],done[0]) and len(success)==1 and inside(success[0]['scope'],done[0]) and success[0]['scope']['ts']>=digests[0]['ts']+digests[0]['dur'] and success[0]['passed'] and not invalid))
 report=dict(session=session,eventCounts=dict(collections.Counter(e['name'] for e in relevant)),consumption=consumed,seedAccepted=[x for x in consumed if x['accepted'] and x['url']=='http://127.0.0.1:8186/jit/seed.wasm'],unmatchedRelevantScopes=unmatched,method='Only a unique URL hit within the same complete compileConsume scope as digest and Deserialize containing FinishAsyncCompile/OnCompilationSucceeded can pass. Split callbacks remain unbound.')
 return report,dict(events=relevant,scopes=spans)

if __name__=='__main__':
 root=Path('.context/jit-stream-cache');reports=[]
 for i in [1,2]:
  folder=root/'mechanism'/f'{i}-persistent';raw=json.loads((folder/'trace.json').read_text());report,evidence=analyze_events(raw['traceEvents'],i)
  (folder/'cache-events.json').write_text(json.dumps(evidence,indent=2)+'\n');(folder/'cache-analysis.json').write_text(json.dumps(report,indent=2)+'\n');reports.append(report)
 out=dict(nativeReuseProved=bool(reports[1]['seedAccepted']) and not reports[1]['unmatchedRelevantScopes'],runs=reports)
 (root/'mechanism/cache-analysis.json').write_text(json.dumps(out,indent=2)+'\n');print(json.dumps({'nativeReuseProved':out['nativeReuseProved'],'counts':[r['eventCounts'] for r in reports],'seedAccepted':[len(r['seedAccepted']) for r in reports]},indent=2))
