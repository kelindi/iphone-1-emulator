"""Identical complete-work compilation CPU interval accounting in every arm."""
import json,sys,collections
from pathlib import Path
names={'wasm.ExecuteCompilationUnits','wasm.TopTierCompilation','wasm.CompileLazy','wasm.CompileBaseline','wasm.CompileJSToWasmWrapper','wasm.CompileWasmImportCallWrapper'}
def union(rows):
 total=0;end=-1
 for lo,hi in sorted(rows):total+=max(0,hi-max(lo,end));end=max(end,hi)
 return total
def analyze(events):
 markers={e['args']['sync_id']:e['ts']for e in events if e.get('name')=='clock_sync'and'sync_id'in e.get('args',{})};lo=markers['navigation-start'];hi=markers['fixed-work-end'];assert hi>lo
 scopes=[e for e in events if e.get('ph')=='X'and e.get('name')in names and e['ts']<hi and e['ts']+e.get('dur',0)>lo];assert scopes,'No compilation evidence'
 lower=collections.defaultdict(list);upper=collections.defaultdict(list);missing=[]
 for e in scopes:
  if 'tts'not in e or 'tdur'not in e:missing.append(e);continue
  key=(e['pid'],e['tid']);interval=(e['tts'],e['tts']+e['tdur']);upper[key].append(interval)
  if e['ts']>=lo and e['ts']+e['dur']<=hi:lower[key].append(interval)
 return {'scope':'navigation-start through fixed-work-end; all processes/threads, foreground and background; no inferred module identity','cpuLowerMs':sum(union(v)for v in lower.values())/1000,'cpuUpperMs':sum(union(v)for v in upper.values())/1000,'missingThreadFields':len(missing),'scopes':dict(collections.Counter(e['name']for e in scopes)),'traceSpanMs':(hi-lo)/1000}
if __name__=='__main__':
 base=Path('.context/jit-mixed-coverage/measured');rows=[]
 for folder in sorted(base.glob('[1-4]-*')):
  r=json.loads((folder/'report.json').read_text());assert r['traceComplete']['dataLossOccurred']is False
  a=analyze(json.loads((folder/'trace.json').read_text())['traceEvents']);a['arm']=folder.name;rows.append(a)
 result={'arms':rows,'pairs':[],'passed':len(rows)==4}
 for control,candidate in [(0,1),(3,2)]:
  a,b=rows[control],rows[candidate];cap=a['cpuLowerMs']*1.25+100;passed=not(a['missingThreadFields']or b['missingThreadFields'])and b['cpuUpperMs']<=cap;result['pairs'].append({'control':a['arm'],'candidate':b['arm'],'candidateUpperMs':b['cpuUpperMs'],'controlLowerMs':a['cpuLowerMs'],'capMs':cap,'passed':passed});result['passed']&=passed
 (base/'compilation.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
