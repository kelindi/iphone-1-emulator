from pathlib import Path
import json,hashlib,collections,re
ROOT=Path.cwd();OUT=ROOT/'docs/traces/warm-settings-reassessment';sha=lambda b:hashlib.sha256(b).hexdigest()
A=ROOT/'.context/jit-attribution/no-map/emu.wasm';B=ROOT/'docs/traces/jit-stream-cache/sw-cache-offline/frozen-site/dist/emu.wasm'
def leb(b,p):
 v=s=0
 while True:
  x=b[p];p+=1;v|=(x&127)<<s
  if x<128:return v,p
  s+=7
def parts(path):
 b=path.read_bytes();p=8;sections={}
 while p<len(b):
  key=b[p];n,p=leb(b,p+1);sections[key]=b[p:p+n];p+=n
 c=sections[10];n,p=leb(c,0);bodies=[]
 for _ in range(n):
  k,p=leb(c,p);bodies.append(c[p:p+k]);p+=k
 assert p==len(c);return b,sections,bodies
ab,asec,ac=parts(A);bb,bsec,bc=parts(B);proof=json.loads((ROOT/'docs/traces/browser-cpu-attribution/renumber-proof/proof.json').read_text());assert proof['passed'] and sha(ab)==proof['productionSha256'];assert sha(bb)=='b3237ef3843412aea164d3db111299650ea8433bf16311417116799959ab6606';assert set(asec)==set(bsec);assert all(asec[k]==bsec[k] for k in asec if k!=10);assert len(ac)==len(bc)==223
changed=[i+8 for i,(x,y) in enumerate(zip(ac,bc)) if x!=y];assert changed==[154]
symbols=proof['productionSymbols'];binding={'passed':True,'originalSha256':sha(ab),'profileRuntimeSha256':sha(bb),'allNonCodeSectionsByteIdentical':True,'definedFunctions':223,'imports':8,'changedFunctionBodies':changed,'changedFunctionName':symbols['154'],'rule':'Every other complete function body is byte-identical at the same index to the previously proven symbol map. Changed154(snap_ram) is left changed/unattributed here. No relink or guest run.'};(OUT/'body-identity.json').write_text(json.dumps(binding,indent=2)+'\n')
def category(f):
 name=f['functionName'];url=f['url'];m=re.fullmatch(r'wasm-function\[(\d+)\]',name);symbol=None
 if '/dist/emu.wasm' in url and m:
  i=int(m[1]);symbol=symbols.get(str(i)) if i!=154 else 'changed_snapshot_body_unattributed'
  if symbol=='arm_step':cat='interpreter_step_containing_inlined_work'
  elif symbol=='emu_run':cat='run_loop_containing_inlined_work'
  elif symbol=='region_find':cat='region_lookup'
  elif symbol=='emu_frame':cat='framebuffer_export'
  elif symbol and symbol.startswith('mbx_'):cat='distinct_mbx_functions'
  elif symbol and (symbol.startswith(('mem_','bus_')) or symbol in ['arm_mmu_translate','ap_permits']):cat='memory_mmu_bus_helpers'
  elif symbol and symbol.startswith(('vfp_','exec_vfp')):cat='vfp_helpers'
  elif symbol in ['exec_data_processing','alu_add','alu_sub']:cat='alu_helpers'
  elif symbol and symbol.startswith(('s5l_','s5l8900_','browser_','active_host_clock','machine_')):cat='distinct_device_clock'
  else:cat='other_core'
 elif m and url.startswith('wasm://'):cat='generated_seed_inferred'
 elif name=='(idle)':cat='idle'
 elif name=='(garbage collector)':cat='gc'
 else:cat='javascript_native_wrappers_other'
 return cat,symbol
rows=[]
for arm in ['1-original','4-original']:
 base=ROOT/'docs/traces/jit-lazy-groups/quiet-first-use'/arm
 analysis=json.loads((base/'analysis.json').read_text());report=json.loads((base/'report.json').read_text());stages=json.loads((base/'profiles/stages.json').read_text());a=next(x for x in report['actions'] if x['label']=='H1' and x['kind']=='settings');st=next(x for x in analysis['stages'] if x['label']=='H1-settings');j=json.loads((base/'profiles/H1-settings-worker.cpuprofile').read_text());nodes={x['id']:x for x in j['nodes']};parents={c:n['id'] for n in j['nodes'] for c in n.get('children',[])}
 # Timing.begin lies after start marker; final observer lies before end marker.
 # Its unobserved offset is therefore [0,span-observedAt], without claiming an exact bridge.
 marker=analysis['markers']['H1-settings-start'];slack=st['traceSpanMs']-a['observedAtMs'];assert 0<=slack<100
 release=[marker+a['releaseOffsetMs']*1000,marker+(a['releaseOffsetMs']+slack)*1000];ready=[marker+a['readyMs']*1000,marker+(a['readyMs']+slack)*1000]
 windows={'whole_profile':[j['startTime'],j['endTime']],'common_release_to_ready':[release[1],ready[0]],'union_release_to_ready':[release[0],ready[1]]};res={}
 for label,(lo,hi) in windows.items():
  cats=collections.Counter();fn=collections.Counter();memparents=collections.Counter();t=j['startTime'];maxgap=0
  for id,dt in zip(j['samples'],j['timeDeltas']):
   end=t+dt;us=max(0,min(end,hi)-max(t,lo));t=end
   if not us:continue
   maxgap=max(maxgap,dt);f=nodes[id]['callFrame'];cat,symbol=category(f);cats[cat]+=us;fn[symbol or f['functionName']]+=us
   if cat=='memory_mmu_bus_helpers':
    anc=parents.get(id);chain=[];generated_ancestor=False
    while anc in nodes:
     af=nodes[anc]['callFrame'];acat,asym=category(af)
     if asym:chain.append(asym)
     if acat=='generated_seed_inferred':generated_ancestor=True
     anc=parents.get(anc)
    owner='arm_step' if 'arm_step' in chain else 'generated' if generated_ancestor else 'other'
    memparents[owner]+=us
  total=sum(cats.values());res[label]={'intervalMs':(hi-lo)/1000,'sampledMs':total/1000,'nonIdleSampledMs':(total-cats['idle'])/1000,'maxSampleGapUs':maxgap,'categories':[{'category':k,'ms':v/1000,'percent':100*v/total} for k,v in cats.most_common()],'topFunctions':[{'name':k,'ms':v/1000} for k,v in fn.most_common(16)],'memoryHelpersAncestorMs':{k:v/1000 for k,v in memparents.items()}}
 cpu=sum(t['cpuMs'] for t in st['schedulerThreads'] if t['comm']=='DedicatedWorker');main=sum(t['cpuMs'] for t in st['schedulerThreads'] if t['comm']=='chrome');gpu=sum(t['cpuMs'] for t in st['schedulerProcesses'] if t['type']=='GPU')
 rows.append({'arm':arm,'sourceProfile':str((base/'profiles/H1-settings-worker.cpuprofile').relative_to(ROOT)),'sourceProfileSha256':sha((base/'profiles/H1-settings-worker.cpuprofile').read_bytes()),'readyMs':a['releaseToReadyMs'],'stageMs':st['traceSpanMs'],'timingBridgeUncertaintyMs':slack,'stageRetired':st['retired'],'stageGenerated':st['generated'],'stageGeneratedCalls':st['calls'],'stageInterpreterRetired':st['retired']-st['generated'],'stageWorkerSchedulerCpuMs':cpu,'stageMainSchedulerCpuMs':main,'stageGpuProcessCpuMs':gpu,'stageCompilationCpuLowerMs':st['compileThreadCpuLowerMs'],'stageCompilationCpuUpperMs':st['compileThreadCpuUpperMs'],'windows':res})
result={'scope':'Source/data-only reanalysis of retained original-seed b3237 warmSettings profiles from lazy32 comparison; no new guest/browser/build. Current compiled-cache loader and RTC changes are not sampled by this historical run.','binding':binding,'limitations':['WASM function-start columns cannot separate inlined arm_step/emu_run suboperations.','Generated-module identity follows prior accepted pipeline/actual seed binding inference; core names use exact unchanged function bytes.','Whole-stage counters/scheduler CPU include500ms observer confirmation; readiness-window samples are conservatively clipped with observed marker uncertainty.','Sample residency is not hardware CPU attribution; GPU-process CPU is host rendering/readback/recording, not named guest MBX time.'],'rows':rows};(OUT/'analysis.json').write_text(json.dumps(result,indent=2)+'\n')
for r in rows:
 print(r['arm'],'ready',r['readyMs'],'slack',r['timingBridgeUncertaintyMs'],'CPU',r['stageWorkerSchedulerCpuMs'],'stage',r['stageMs'])
 for n in ['whole_profile','common_release_to_ready']:
  w=r['windows'][n];print(n,w['sampledMs'],[(x['category'],round(x['ms'],1),round(x['percent'],2)) for x in w['categories']])
