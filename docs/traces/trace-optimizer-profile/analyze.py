from pathlib import Path
import json,re,collections,hashlib
base=Path('.context/trace-optimizer-profile')
def uleb(b,p):
 v=0;s=0
 while True:
  c=b[p];p+=1;v|=(c&127)<<s
  if c<128:return v,p
  s+=7

def exports(path):
 b=Path(path).read_bytes();p=8;out={}
 while p<len(b):
  sec=b[p];n,p=uleb(b,p+1);end=p+n
  if sec==7:
   count,p=uleb(b,p)
   for _ in range(count):
    n,p=uleb(b,p);name=b[p:p+n].decode();p+=n;kind=b[p];ix,p=uleb(b,p+1)
    if kind==0:out[name]=ix
  p=end
 return out
core=exports('web/dist/emu.wasm');seed=exports('web/jit/seed.wasm')
glue=Path('web/dist/emu.js').read_text();mapping={str(core[key]):name for name,key in re.findall(r'Module\["([^"\n]+)"\]=wasmExports\["([^"\n]+)"\]',glue)if key in core}
(base/'core-export-map.json').write_text(json.dumps(mapping,indent=2)+'\n');(base/'seed-export-map.json').write_text(json.dumps(seed,indent=2)+'\n')

run=base/'run-1';directory=run/'profiles'
if not (directory/'stages.json').exists():raise SystemExit(0)
stage=json.loads((directory/'stages.json').read_text())['stages'][0]
regions=json.loads(Path('web/jit/regions.json').read_text());eligible=json.loads(Path('.context/trace-optimizer-feasibility/eligible-loops.json').read_text());eligible_ids={r['region']for r in eligible}
outputs=[]
for role in ['worker','main']:
 profile=json.loads((directory/f'1-settings-{role}.cpuprofile').read_text());nodes={n['id']:n for n in profile['nodes']};times=collections.Counter();counts=collections.Counter(profile['samples']);parents={c:n['id']for n in profile['nodes']for c in n.get('children',[])}
 for n,dt in zip(profile['samples'],profile['timeDeltas']):times[n]+=dt
 wasm=collections.defaultdict(set)
 for n in profile['nodes']:
  f=n['callFrame'];m=re.fullmatch(r'wasm-function\[(\d+)\]',f['functionName'])
  if m:wasm[(f['scriptId'],f['url'])].add(int(m[1]))
 # Bind core through the exact exported emu_run wrapper chain and seed through index range + pipeline; retain scope as inference.
 seedmod=[k for k,v in wasm.items()if max(v)>max(core.values())]
 coremod=[k for k in wasm if k not in seedmod]
 if role=='worker':assert len(seedmod)==len(coremod)==1,(seedmod,coremod)
 cats=collections.Counter();funcs=collections.Counter();regiontimes=collections.Counter();detail=[]
 for n,us in times.items():
  f=nodes[n]['callFrame'];name=f['functionName'];key=(f['scriptId'],f['url']);m=re.fullmatch(r'wasm-function\[(\d+)\]',name);ix=int(m[1])if m else None;symbol=None;region=None
  if name=='(idle)':cat='idle'
  elif name=='(garbage collector)':cat='GC'
  elif name=='(program)':cat='unresolved native/program'
  elif role=='worker'and key in seedmod:
   cat='generated regions';exp=[k for k,v in seed.items()if v==ix];region=int(exp[0][1:])if exp and exp[0].startswith('b')else None
   if region is not None:regiontimes[region]+=us
  elif role=='worker'and key in coremod:cat='core WASM';symbol=mapping.get(str(ix))
  elif name.startswith('js-to-wasm:'):cat='WASM wrapper'
  elif f['url'].endswith('/emu-worker.js'):cat='worker JS'
  elif f['url'].endswith('/seed-jit.js'):cat='seed loader JS'
  else:cat='other JS/native'
  cats[cat]+=us;funcs[(cat,name,symbol,f['scriptId'],f['url'])]+=us
  chain=[];a=parents.get(n)
  while a in nodes:chain.append(nodes[a]['callFrame']);a=parents.get(a)
  detail.append({'frame':f,'selfUs':us,'samples':counts[n],'category':cat,'exportName':symbol,'region':region,'ancestors':chain})
 total=sum(times.values());output={'role':role,'profileSpanMs':(profile['endTime']-profile['startTime'])/1000,'sampledResidencyMs':total/1000,'samples':len(profile['samples']),'maximumGapUs':max(profile['timeDeltas']),'modules':[{'scriptId':k[0],'url':k[1],'minIndex':min(v),'maxIndex':max(v),'inferredModule':'seed'if k in seedmod else 'core'}for k,v in wasm.items()],'categories':[{'category':k,'selfMs':v/1000,'percent':100*v/total}for k,v in cats.most_common()],'topFunctions':[{'category':k[0],'function':k[1],'exportName':k[2],'scriptId':k[3],'url':k[4],'selfMs':v/1000,'percent':100*v/total}for k,v in funcs.most_common()],'regionRanking':[{'region':k,'virtualBase':hex(regions[k]['pc']),'selfMs':v/1000,'percent':100*v/total,'containsEligibleLoop':k in eligible_ids}for k,v in regiontimes.most_common()],'eligibleWholeRegionMs':sum(v for k,v in regiontimes.items()if k in eligible_ids)/1000,'eligibleWholeRegionPercent':100*sum(v for k,v in regiontimes.items()if k in eligible_ids)/total,'details':detail}
 outputs.append(output)
report={'scope':'One sampled warm Settings interval after one unprofiled Settings/Home pair. Exclusive leaf residency, no inclusive double counting. Timing includes readiness confirmation; not a launch speed benchmark. Seed-module classification is inferred from exact served module hashes, unchanged two-module worker pipeline and function-index range; no Debugger bytecode identity. Export indices are parsed from exact served binaries. Internal core functions remain unnamed. Whole-region loop share is only an upper bound, not loop execution or eliminable overhead.','stage':stage,'profiles':outputs}
(base/'analysis.json').write_text(json.dumps(report,indent=2)+'\n')
for r in outputs:print(r['role'],r['categories'],'eligible upper bound',r['eligibleWholeRegionPercent'])
