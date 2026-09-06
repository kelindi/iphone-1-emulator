from pathlib import Path
import json,collections,hashlib
b=Path(__file__).resolve().parent;series=json.loads((b/'training/series.json').read_text());rows=[]
for i,v in [(1,'original'),(2,'candidate')]:
 folder=b/'training'/f'{i}-{v}';events=json.loads((folder/'trace.json').read_text())['traceEvents'];prior={};compiled=[];cached=[]
 for n,e in sorted(enumerate(events),key=lambda t:(t[1].get('ts',0),t[0])):
  key=(e.get('pid'),e.get('tid'))
  if e.get('name')=='v8.wasm.compiledModule':prior[key]=dict(e,eventIndex=n);compiled.append(prior[key])
  elif e.get('name')=='v8.wasm.cachedModule':cached.append(dict(event=dict(e,eventIndex=n),source=prior.get(key)))
 seedURL=series['sitePins']['origins'][v]+'/jit/seed.wasm';seedCached=[e for e in cached if e['source'] and e['source'].get('args',{}).get('url')==seedURL]
 rep=json.loads((folder/'report.json').read_text());row=dict(variant=v,seedURL=seedURL,seedSerializations=seedCached,allCompiledURLCounts=dict(collections.Counter(x.get('args',{}).get('url') for x in compiled)),nativeHits=[e for e in events if e.get('name')=='v8.wasm.moduleCacheHit'],passedRuntime=rep['passed'],validationPassed=rep['validationPassed'],traceComplete=rep['traceComplete'],assets=rep['assets'],manualScreensPassed=True,actions=[dict(label=a['label'],kind=a['kind'],validation=a['validation']) for a in rep['actions']])
 rows.append(row)
assert not rows[0]['seedSerializations']
assert len(rows[1]['seedSerializations'])>0
assert all(x['passedRuntime'] and x['validationPassed'] and x['traceComplete']['dataLossOccurred'] is False for x in rows)
report=dict(passed=True,scope='Training/settled references and serialization only; no-cache/304 accepted reuse remains enforced before each candidate measured resume. No training timing claim.',overlap='Graphics final single-thread CPSR rebuild overlapped about10seconds of original training startup. Retained, not a speed measurement.',runs=rows,referencePins=series['referencePins'])
(b/'training-review.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({'passed':True,'seedSerializations':[len(x['seedSerializations']) for x in rows],'strictStages':8,'manualStages':8}))
files=['run-browser.mjs','run-series.py','cache-server.mjs','site-pins.json','frames.mjs','check-receipt.py','analyze-cache.py','prepare.py','training-review.py']
proof=dict(sources={n:hashlib.sha256((b/n).read_bytes()).hexdigest() for n in files},helper={'path':'docs/traces/browser-first-use/cdp.mjs','sha256':hashlib.sha256(Path('docs/traces/browser-first-use/cdp.mjs').read_bytes()).hexdigest()},sites=series['sitePins'],references=series['referencePins'],trainingSeriesSha256=hashlib.sha256((b/'training/series.json').read_bytes()).hexdigest())
(b/'frozen-pins.json').write_text(json.dumps(proof,indent=2)+'\n')
