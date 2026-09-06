from pathlib import Path
import importlib.util,json,copy
root=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('cache_analyzer',root/'analyze-cache.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
actual=json.loads((root/'mechanism/2-persistent/cache-events.json').read_text())['events']
positive,_=module.analyze_events(actual,2);assert len(positive['seedAccepted'])==1
scope=positive['seedAccepted'][0]['scope'];hit=positive['seedAccepted'][0]['hits'][0]
def erase(name):return [e for e in copy.deepcopy(actual) if e['name']!=name]
def modify(name,field,value):
 out=copy.deepcopy(actual)
 for e in out:
  if e['name']==name:e[field]=value
 return out
negative={
 'missingSucceeded':erase('wasm.OnCompilationSucceeded'),
 'missingDigest':erase('v8.wasm.compileDigestForConsume'),
 'missingFinish':erase('wasm.FinishAsyncCompile'),
 'missingHit':erase('v8.wasm.moduleCacheHit'),
 'duplicateHit':copy.deepcopy(actual)+[copy.deepcopy(hit)],
 'differentPID':modify('wasm.Deserialize','pid',-1),
 'differentThread':modify('wasm.Deserialize','tid',-1),
 'outOfScopeDeserialize':modify('wasm.Deserialize','ts',scope['ts']+scope['dur']+1),
 'wrongURL':modify('v8.wasm.moduleCacheHit','args',{'url':'http://127.0.0.1:8186/other.wasm'}),
 'succeededOutsideFinish':modify('wasm.OnCompilationSucceeded','ts',scope['ts']+1),
}
for name in ['v8.wasm.moduleCacheInvalid','v8.wasm.moduleCacheInvalidDigest']:
 negative[name]=copy.deepcopy(actual)+[dict(name=name,ph='I',pid=scope['pid'],tid=scope['tid'],ts=scope['ts']+scope['dur']-1,args={})]
results=[]
for name,events in negative.items():
 result,_=module.analyze_events(events,2);assert not result['seedAccepted'],name
 results.append(dict(name=name,rejected=True))
(root/'analyzer-negatives.json').write_text(json.dumps(dict(passed=True,positiveActualTrace=True,negativeCases=results),indent=2)+'\n')
print('Actual trace positive and',len(results),'deliberate negatives pass')
