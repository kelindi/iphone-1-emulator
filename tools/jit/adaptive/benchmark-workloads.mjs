// Predeclared Stage 2 gate. No shortened workloads or adjustable acceptance gate.
import {readFile,writeFile,mkdir} from 'node:fs/promises';
import {resolve} from 'node:path';
import {pathToFileURL} from 'node:url';
import {createHash} from 'node:crypto';
import {cpus,release} from 'node:os';
import assert from 'node:assert/strict';
import wabtFactory from 'wabt';
import {createWorkloadCompiler} from './workload-compiler.mjs';
import {workloads,validateWorkload,nextBoundary} from './workloads.mjs';
const root=resolve(process.env.RUNTIME_ROOT||'.context/adaptive-compiler/workload-refresh-runtime');
const baselineRoot=resolve(process.env.BASELINE_ROOT||'.context/adaptive-compiler/baseline-final');
const out=resolve(process.env.ARTIFACT_DIR||'.context/adaptive-compiler/workload-abba');
await mkdir(out); // Never replace evidence from a prior gate.
const sha=b=>createHash('sha256').update(b).digest('hex');
assert.equal(typeof process.threadCpuUsage,'function','Node with main-thread CPU accounting is required');
const report={passed:false,correctnessPassed:false,performancePassed:false,scope:'Stage 2 fixed CPU throughput gate; not browser or Mac latency acceptance.',host:{node:process.version,platform:process.platform,release:release(),cpu:cpus()[0]?.model},timingBoundary:'CPU spans engine setup through teardown after identical emu_init+snapshot restore, subtracting only measured main-thread CPU for checkpoint serialization/hash/comparison, report I/O and statistics sampling; background compilation CPU during those exclusions remains counted. Wall time accumulates setup/install, original inputs/chunks including drains, and teardown. Common initialization/restore and one-time module/WABT loading are outside both measurements. Runtime instances remain alive across warmup and timed replays; installed generated units are reset every replay.',policy:{warmupsPerVersionPerWorkload:1,timedOrder:['A','B','B','A'],A:'independently frozen seeded engine',B:'adaptive candidate',aggregation:'Mean CPU milliseconds per workload/version; geometric mean of A/B ratios >= 2, each ratio >= 1.',minimumGeometricMean:2,minimumIndividual:1},identities:{},runs:[]};
const save=()=>writeFile(out+'/report.json',JSON.stringify(report,null,2)+'\n');
const pinned=new Map();
async function pin(path,expected){path=resolve(path);const bytes=await readFile(path),hash=sha(bytes);if(expected)assert.equal(hash,expected,'Identity '+path);pinned.set(path,hash);report.identities[path]=hash;return bytes;}
async function verify(){for(const [path,hash] of pinned)assert.equal(sha(await readFile(path)),hash,'Input changed during gate: '+path);}
function measure(row,fn){const start=performance.now();try{return fn();}finally{row.wallMs+=performance.now()-start;}}
function excludeMain(row,fn){const start=process.threadCpuUsage();try{return fn();}finally{const d=process.threadCpuUsage(start);row.excludedMainCpuMs+=(d.user+d.system)/1000;}}
async function excludeMainAsync(row,fn){const start=process.threadCpuUsage();try{return await fn();}finally{const d=process.threadCpuUsage(start);row.excludedMainCpuMs+=(d.user+d.system)/1000;}}
function updateCpu(row,start){const d=process.cpuUsage(start);row.totalProcessCpuMs=(d.user+d.system)/1000;row.cpuMs=row.totalProcessCpuMs-row.excludedMainCpuMs;}
try{
 const reference=JSON.parse(await pin('tools/jit/adaptive/fixtures/adaptive-workloads-report.json'));
 const baselineBytes=await pin(baselineRoot+'/freeze-manifest.json',reference.freezeManifestSha256),baseline=JSON.parse(baselineBytes);
 const frozen=relative=>pin(baselineRoot+'/'+relative,baseline.entries.find(e=>e.relative===relative)?.sha256||assert.fail('Missing frozen identity '+relative));
 await frozen('site/dist/emu.js');await frozen('site/dist/emu.wasm');
 const seedBytes=await frozen('site/jit/seed.wasm'),regions=JSON.parse(await frozen('site/jit/regions.json'));
 const build=JSON.parse(await pin(root+'/build-report.json'));assert.equal(build.passed,true);
 await pin(root+'/workload.cjs',build.wrapperSha256||assert.fail('Missing candidate wrapper identity'));
 await pin(root+'/workload.wasm',build.wasmSha256||assert.fail('Missing candidate WASM identity'));
 for(const file of ['tools/jit/adaptive/benchmark-workloads.mjs','tools/jit/adaptive/workload-compiler.mjs','tools/jit/adaptive/module-cache.mjs','tools/jit/adaptive/workloads.mjs','tools/jit/adaptive/unit.mjs','tools/jit/adaptive/decode.mjs','tools/jit/adaptive/emit.mjs','package-lock.json'])await pin(file);
 const fixture=await pin('.context/adaptive-compiler/workload-home.snap',reference.fixture.sha256);
 const kernel=await pin('web/firmware/kernel_1A543a.macho',baseline.firmware.assetSha256[baseline.firmware.kernel]);
 const tree=await pin('web/firmware/devicetree_m68ap.bin',baseline.firmware.assetSha256[baseline.firmware.devicetree]);
 const {default:createA}=await import(pathToFileURL(baselineRoot+'/site/dist/emu.js'));
 const {default:createB}=await import(pathToFileURL(root+'/workload.cjs'));
 const engines={A:await createA({locateFile:p=>baselineRoot+'/site/dist/'+p}),B:await createB()};
 const seed=new WebAssembly.Module(seedBytes),wabt=await wabtFactory();
 // Modules and staging allocations are retained, while machine initialization
 // below resets nonserialized MMU stamps as well as all serialized guest state.
 const staging={};
 for(const version of ['A','B']){const m=engines[version],pointers=[];const alloc=b=>{const p=m._malloc(b.length);assert(p);m.HEAPU8.set(b,p);pointers.push(p);return p;};staging[version]={k:alloc(kernel),d:alloc(tree),s:alloc(fixture),mask:alloc(Buffer.alloc(64)),pointers};}
 for(const w of workloads){
  validateWorkload(w);const expected=reference.runs.find(r=>r.name===w.name&&r.repeat===0);assert(expected);
  assert.deepEqual(w,reference.workloads.find(r=>r.name===w.name),'Frozen workload identity');
  for(const [index,version] of ['A','B','A','B','B','A'].entries()){
   await verify();
   const m=engines[version],p=staging[version],row={workload:w.name,version,phase:index<2?'warmup':'timed',ordinal:index<2?0:index-1,cpuMs:0,wallMs:0,totalProcessCpuMs:0,excludedMainCpuMs:0,rowCpuAccounting:'Whole-process setup-through-teardown CPU minus main-thread-only excluded work; retains background CPU during exclusions.',credits:0,checkpoints:[],passed:false};report.runs.push(row);
   assert.equal(m._emu_init(p.k,kernel.length,p.d,tree.length,0,0,256),0);
   assert.equal(m._emu_load_snapshot(p.s,fixture.length),0);
   let compiler,ids=[];
   const rowCpuStart=process.cpuUsage();
   measure(row,()=>{
    if(version==='B'){assert.equal(m._workload_prepare(),1);compiler=createWorkloadCompiler(m,wabt);m.workloadCompile=compiler.compile;m._workload_observe();m._adaptive_enable(1);}
    else{m._emu_jit_reset();const instance=new WebAssembly.Instance(seed,{env:{memory:m.jitMemory,stale:m._emu_jit_stale,span:m._emu_jit_span}});for(const r of regions){const id=m.addFunction(instance.exports['b'+r.index],'iii');ids.push(id);m.HEAPU32.set(r.masks,p.mask>>>2);assert.equal(m._emu_jit_install(r.pc,+r.thumb,id,p.mask),1);}m._emu_jit_enable(1);}
   });
   for(let done=0;done<w.credits;){
    const next=nextBoundary(w,done);
    measure(row,()=>{
     for(const e of w.events.filter(e=>e.at===done)){if(e.type==='touch')m._emu_touch(e.down,e.x,e.y);else m._emu_button(e.id,e.down);}
     assert.equal(m._emu_run(next-done),next-done);assert.equal(m._emu_status(),0);
     if(compiler){assert.equal(m._adaptive_failed(),0);compiler.drain();}
    });
    done=next;row.credits=done;
    if(w.checkpoints.includes(done))excludeMain(row,()=>{
     const pointer=m._emu_save_snapshot(),bytes=m._emu_snapshot_size();assert(pointer&&bytes);
     const checkpoint={credits:done,bytes,sha256:sha(m.HEAPU8.subarray(pointer,pointer+bytes))};m._emu_free_snapshot();row.checkpoints.push(checkpoint);
     assert.deepEqual(checkpoint,expected.checkpoints.find(c=>c.credits===done),'Complete frozen checkpoint '+w.name+' '+version+' '+done);
    });
    if(done%10000000===0)await excludeMainAsync(row,async()=>{updateCpu(row,rowCpuStart);await save();console.log(JSON.stringify({workload:w.name,version,phase:row.phase,ordinal:row.ordinal,credits:done,cpuMs:row.cpuMs}));});
   }
   excludeMain(row,()=>{
   if(compiler){row.runtime=Array.from({length:13},(_,i)=>m._adaptive_stat(i));row.generated=row.runtime[0];row.compiler=structuredClone(compiler.stats);row.attempts=m._workload_attempts();}
   else row.generated=m._emu_jit_stat(0);
   assert(row.generated>0,'Actual generated retirement is required in each run');assert.equal(row.checkpoints.length,3);
   });
   measure(row,()=>{if(compiler){compiler.close();m.workloadCompile=undefined;}else{m._emu_jit_reset();for(const id of ids)m.removeFunction(id);ids=[];}});
   updateCpu(row,rowCpuStart);assert(row.cpuMs>0,'Positive accounted CPU time');
   row.passed=true;await verify();await save();
  }
 }
 report.results=workloads.map(w=>{const mean=version=>{const runs=report.runs.filter(r=>r.workload===w.name&&r.version===version&&r.phase==='timed');assert.equal(runs.length,2);return runs.reduce((n,r)=>n+r.cpuMs,0)/runs.length;};const controlCpuMs=mean('A'),candidateCpuMs=mean('B');assert(controlCpuMs>0&&candidateCpuMs>0);return {workload:w.name,controlCpuMs,candidateCpuMs,speedup:controlCpuMs/candidateCpuMs};});
 report.geometricMean=Math.exp(report.results.reduce((sum,r)=>sum+Math.log(r.speedup),0)/report.results.length);
 report.correctnessPassed=report.runs.length===18&&report.runs.every(r=>r.passed&&r.checkpoints.length===3);
 report.performancePassed=report.geometricMean>=2&&report.results.every(r=>r.speedup>=1);
 await verify();report.passed=report.correctnessPassed&&report.performancePassed;
 for(const version of ['A','B'])for(const p of staging[version].pointers)engines[version]._free(p);
 if(!report.passed)process.exitCode=1;
}catch(e){report.error=e.stack;process.exitCode=1;}
await save();console.log(JSON.stringify({passed:report.passed,correctnessPassed:report.correctnessPassed,performancePassed:report.performancePassed,geometricMean:report.geometricMean,error:report.error,report:out+'/report.json'}));
