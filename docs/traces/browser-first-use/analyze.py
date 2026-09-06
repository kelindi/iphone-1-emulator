import json,sys,collections,gzip
from pathlib import Path
p=Path(sys.argv[1] if len(sys.argv)>1 else '.context/first-use/run-2')
r=json.loads((p/'report.json').read_text());tr=json.loads((p/'trace.json').read_text() if (p/'trace.json').exists() else gzip.decompress((p/'trace.json.gz').read_bytes()));ev=tr['traceEvents'];st=json.loads((p/'profiles/stages.json').read_text())['stages']
markers={e['args']['sync_id']:e['ts'] for e in ev if e.get('name')=='clock_sync' and 'sync_id' in e.get('args',{})}
scope_names={'wasm.ExecuteCompilationUnits','wasm.TopTierCompilation','wasm.CompileLazy','wasm.CompileBaseline','wasm.CompileJSToWasmWrapper','wasm.CompileWasmImportCallWrapper'}
scopes=[e for e in ev if e.get('ph')=='X' and e.get('name') in scope_names]
def union(intervals):
 total=0;end=-1
 for lo,hi in sorted(intervals):
  total+=max(0,hi-max(lo,end));end=max(end,hi)
 return total
out={'traceComplete':r['traceComplete'],'events':len(ev),'markers':markers,'method':'Thread-CPU intervals are unioned per PID/TID; nested scopes not added. Boundary-crossing scopes contribute zero to lower CPU bound and full tdur to conservative upper bound. No function/module identity inferred from absent args.','stages':[],'allScopes':collections.Counter(e['name'] for e in scopes)}
for s in st:
 label=s['label'];lo=markers[label+'-start'];hi=markers[label+'-end'];within=[e for e in scopes if e['ts']>=lo and e['ts']+e.get('dur',0)<=hi];cross=[e for e in scopes if e['ts']<hi and e['ts']+e.get('dur',0)>lo and e not in within]
 groups=collections.defaultdict(list);wall=collections.defaultdict(list)
 for e in within:
  if 'tts' in e and 'tdur' in e:groups[(e['pid'],e['tid'])].append((e['tts'],e['tts']+e['tdur']))
 for e in within+cross:wall[(e['pid'],e['tid'])].append((max(lo,e['ts']),min(hi,e['ts']+e['dur'])))
 lower=sum(union(v) for v in groups.values())/1000
 upper_groups={k:list(v) for k,v in groups.items()}
 for e in cross:
  if 'tts' in e and 'tdur' in e:upper_groups.setdefault((e['pid'],e['tid']),[]).append((e['tts'],e['tts']+e['tdur']))
 upper=sum(union(v) for v in upper_groups.values())/1000
 a=next(x for x in r['actions'] if x['label']+'-'+x['kind']==label)
 byname={}
 for name in sorted(scope_names):
  g=collections.defaultdict(list)
  for e in within:
   if e['name']==name and 'tts' in e and 'tdur' in e:g[(e['pid'],e['tid'])].append((e['tts'],e['tts']+e['tdur']))
  lower_name=sum(union(v) for v in g.values())/1000
  byname[name]={'containedCpuMs':lower_name,'upperCpuMs':lower_name+sum(e.get('tdur',0) for e in cross if e['name']==name)/1000,'containedCount':sum(e['name']==name for e in within)}
 out['stages'].append({'byNameInclusiveDoNotSum':byname,'label':label,'readyMs':a['releaseToReadyMs'],'traceSpanMs':(hi-lo)/1000,'retired':a['after']['retired']-a['before']['retired'],'generated':a['after']['jit']['retired']-a['before']['jit']['retired'],'calls':a['after']['jit']['calls']-a['before']['jit']['calls'],'compileThreadCpuLowerMs':lower,'compileThreadCpuUpperMs':upper,'compileWallPerThreadSumMs':sum(union(v) for v in wall.values())/1000,'compileGlobalWallUnionMs':union([interval for intervals in wall.values() for interval in intervals])/1000,'compileContainedScopes':dict(collections.Counter(e['name'] for e in within)),'compileCrossingScopes':len(cross),'missingThreadFields':sum('tts' not in e or 'tdur' not in e for e in within+cross),'schedulerThreads':s['threadCpu'],'schedulerProcesses':s['processCpu'],'exitedThreads':s['exitedThreads']})
(p/'analysis.json').write_text(json.dumps(out,indent=2)+'\n')
for s in out['stages']:
 print(s['label'],s['readyMs'],s['retired'],s['compileThreadCpuLowerMs'],s['compileThreadCpuUpperMs'],[(t['comm'],t['cpuMs']) for t in s['schedulerThreads'] if t['cpuMs']>50])
