from pathlib import Path
import json,re,collections,hashlib
root=Path.cwd();p=root/'.context/jit-attribution';out=p/'browser/profiles';stages=json.loads((out/'stages.json').read_text());exports=json.loads((p/'production-export-map.json').read_text());bijection=json.loads((p/'renumber-proof/proof.json').read_text());symbols=bijection['productionSymbols'];assert bijection['passed'];assert hashlib.sha256((root/'web/dist/emu.wasm').read_bytes()).hexdigest()==bijection['productionSha256'];results=[]
for stage in stages['stages']:
 row={'stage':stage['label'],'elapsedMs':stage['endUnixMs']-stage['startUnixMs'],'savedAtBefore':stage['metadata'].get('savedAt'),'savedAtAfter':stage['endMetadata'].get('savedAt'),'captureMsBefore':stage['metadata']['metrics'].get('captureMs'),'captureMsAfter':stage['endMetadata']['metrics'].get('captureMs'),'profiles':[]}
 for prof in stage['profiles']:
  j=json.loads((out/prof['file']).read_text());nodes={n['id']:n for n in j['nodes']};parents={c:n['id'] for n in j['nodes'] for c in n.get('children',[])};samples=collections.Counter(j.get('samples',[]));time=collections.Counter();cats=collections.Counter();counts=collections.Counter();functions={}
  for node,dt in zip(j.get('samples',[]),j.get('timeDeltas',[])):time[node]+=dt
  for node,us in time.items():
   f=nodes[node]['callFrame'];name=f['functionName'];url=f['url'];role=prof['role'];sid=f['scriptId'];ix=re.fullmatch(r'wasm-function\[(\d+)\]',name);index=int(ix[1]) if ix else None;chain=[];ancestor=parents.get(node)
   while ancestor in nodes:chain.append(nodes[ancestor]['callFrame']);ancestor=parents.get(ancestor)
   symbol=symbols.get(str(index)) if sid=='8' and role=='worker' else None
   if name=='(idle)':cat='idle'
   elif name=='(garbage collector)':cat='garbage_collector'
   elif name=='(program)':cat='native_program_unknown'
   elif role=='worker' and sid=='8' and index is not None:
    if symbol=='emu_frame':cat='core_frame'
    elif symbol and (symbol.startswith(('snap_','sn_','snapshot_')) or 'snapshot' in symbol):cat='core_snapshot'
    elif symbol=='emu_run':cat='core_run_dispatch_fused'
    elif symbol=='arm_step':cat='core_arm_step_self'
    elif symbol=='region_find':cat='core_jit_region_lookup'
    elif symbol and symbol.startswith('mbx_'):cat='core_mbx_named'
    elif symbol and symbol.startswith(('vfp_','exec_vfp')):cat='core_vfp'
    elif symbol and (symbol.startswith(('mem_','bus_')) or symbol in ['arm_mmu_translate','ap_permits']):cat='core_guest_memory_mmu_bus'
    elif symbol in ['exec_data_processing','alu_add','alu_sub']:cat='core_alu_helpers'
    elif symbol and symbol.startswith(('s5l_','s5l8900_','browser_','active_host_clock','machine_')):cat='core_devices_clock'
    elif symbol:cat='core_other_named'
    else:cat='core_internal_unknown'
   elif role=='worker' and sid=='7' and index is not None:cat='generated_module_inferred'
   elif name.startswith('js-to-wasm:'):cat='wasm_boundary_wrapper'
   elif url.endswith('/emu-worker.js'):cat='worker_frame_js' if name=='present' else 'worker_other_js'
   elif url.endswith('/seed-jit.js'):cat='seed_controller_js'
   elif url.endswith('/dist/emu.js'):cat='emscripten_js'
   elif role=='main' and (sid=='46' or any(a['scriptId']=='46' for a in chain)):cat='test_pixel_observer'
   elif role=='main' and (url.endswith('/storage.js') or any(a['url'].endswith('/storage.js') for a in chain)):cat='storage_js_or_child'
   elif role=='main' and (name=='putImageData' or (url.endswith('/app.js') and name=='worker.onmessage')):cat='main_frame_or_message_js'
   elif url:cat='other_script'
   elif sid=='0':cat='native_unresolved'
   else:cat='other_eval_or_harness'
   cats[cat]+=us;counts[cat]+=samples[node];key=(sid,url,name,symbol,cat)
   if key not in functions:functions[key]={'scriptId':sid,'url':url,'functionName':name,'mappedCoreSymbol':symbol,'category':cat,'selfUs':0,'selfSamples':0,'exampleAncestorFrames':chain[:8]}
   functions[key]['selfUs']+=us;functions[key]['selfSamples']+=samples[node]
  total=sum(time.values());idle=cats['idle'];workerThreads=[t for t in stage['threadCpu'] if t['comm']=='DedicatedWorker'];workerPids={t['pid'] for t in workerThreads};mainThreads=[t for t in stage['threadCpu'] if t['pid'] in workerPids and t['tid']==t['pid']];cpu=sum(t['cpuMs'] for t in (workerThreads if prof['role']=='worker' else mainThreads))
  row['profiles'].append({'role':prof['role'],'file':prof['file'],'sha256':hashlib.sha256((out/prof['file']).read_bytes()).hexdigest(),'profileSpanMs':(j['endTime']-j['startTime'])/1000,'sampledResidencyMs':total/1000,'sampledNonIdleMs':(total-idle)/1000,'schedulerThreadCpuMs':cpu,'sampledNonIdleToSchedulerCpuRatio':(total-idle)/1000/cpu if cpu else None,'samples':len(j.get('samples',[])),'maximumSampleGapUs':max(j.get('timeDeltas',[0])),'categories':[{'category':c,'selfSampledMs':us/1000,'percentOfAllResidency':100*us/total,'samples':counts[c]} for c,us in cats.most_common()],'functions':sorted(functions.values(),key=lambda f:f['selfUs'],reverse=True)})
 row['threadCpu']=stage['threadCpu'];row['exitedThreads']=stage['exitedThreads'];row['processCpu']=stage['processCpu'];row['rendererThreadPoolCpuMs']=sum(t['cpuMs'] for t in stage['threadCpu'] if t['comm'].startswith('ThreadPool'));results.append(row)
report={'scope':'Exclusive statistical stack residency from exact original production browser run. CPU tick deltas are separate, not allocated into stack rows. No inclusive double counting; unknown native/core frames retained.','identityLimit':'Debugger bytecode retrieval timed out. Core URL and function indices map through independently reviewed full-module renumbering proof bound to both source binaries; second worker WASM script is inferred generated module from the unmodified worker pipeline, expected seed bytes and positive JIT metrics. No full captured-bytecode hash proof. Rejected equal-index symbol mapping remains unused; only the fully proven bijection supplies names. Inlined work remains attributed to its containing function.','provenance':{'coreSha256':hashlib.sha256((root/'web/dist/emu.wasm').read_bytes()).hexdigest(),'seedSha256':hashlib.sha256((root/'web/jit/seed.wasm').read_bytes()).hexdigest(),'exportMapSha256':hashlib.sha256((p/'production-export-map.json').read_bytes()).hexdigest(),'bijectionProofSha256':hashlib.sha256((p/'renumber-proof/proof.json').read_bytes()).hexdigest(),'independentWatBindingSha256':hashlib.sha256((p/'renumber-proof/independent-wat-binding.json').read_bytes()).hexdigest()},'stages':results}
(p/'analysis.json').write_text(json.dumps(report,indent=2)+'\n')
for r in results:
 print(r['stage'],'threadpoolCPU',r['rendererThreadPoolCpuMs'])
 for profile in r['profiles']:print(profile['role'],'span',round(profile['profileSpanMs'],1),'nonidle',round(profile['sampledNonIdleMs'],1),'CPU',profile['schedulerThreadCpuMs'],[(c['category'],round(c['selfSampledMs'],1),round(c['percentOfAllResidency'],2)) for c in profile['categories']])
