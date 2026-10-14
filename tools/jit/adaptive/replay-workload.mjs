// Correctness pilot only. Timings are diagnostic, not the declared ABBA gate.
import {readFile,writeFile,mkdir} from 'node:fs/promises';
import {resolve} from 'node:path';
import {pathToFileURL} from 'node:url';
import {createHash} from 'node:crypto';
import assert from 'node:assert/strict';
import wabtFactory from 'wabt';
import {createWorkloadCompiler} from './workload-compiler.mjs';
import {workloads,validateWorkload,nextBoundary} from './workloads.mjs';
const root=resolve(process.env.RUNTIME_ROOT||'.context/adaptive-compiler/workload-observer-runtime');
const out=resolve(process.env.ARTIFACT_DIR||'.context/adaptive-compiler/workload-observer-replay');await mkdir(out);
const sha=b=>createHash('sha256').update(b).digest('hex');
const reference=JSON.parse(await readFile('tools/jit/adaptive/fixtures/adaptive-workloads-report.json'));
const build=JSON.parse(await readFile(root+'/build-report.json'));assert(build.passed);
assert.equal(sha(await readFile(root+'/workload.wasm')),build.wasmSha256);
assert.equal(sha(await readFile(root+'/workload.cjs')),build.wrapperSha256,'Pinned runtime wrapper');
const w=workloads.find(w=>w.name===(process.env.WORKLOAD||'settings'));assert(w);validateWorkload(w);
const stop=Number(process.env.STOP_CREDITS||w.credits);assert(Number.isSafeInteger(stop)&&stop>0&&stop<=w.credits);
const report={passed:false,scope:'Adaptive real-app correctness pilot against frozen complete checkpoints. Diagnostic timings only; not ABBA throughput acceptance.',workload:w,stop,wasmSha256:build.wasmSha256,credits:0,cpuMs:0,wallMs:0,checkpoints:[],validatedCheckpoints:0,fullWorkload:false};
report.sources={};
const sourceFiles=['tools/jit/adaptive/replay-workload.mjs','tools/jit/adaptive/workload-compiler.mjs','tools/jit/adaptive/module-cache.mjs','tools/jit/adaptive/workloads.mjs','tools/jit/adaptive/unit.mjs','tools/jit/adaptive/decode.mjs','tools/jit/adaptive/emit.mjs'];
for(const file of sourceFiles)report.sources[file]=sha(await readFile(file));
const save=()=>writeFile(out+'/report.json',JSON.stringify(report,null,2)+'\n');
try{
 const fixture=await readFile('.context/adaptive-compiler/workload-home.snap');assert.equal(sha(fixture),reference.fixture.sha256);
 const baseline=JSON.parse(await readFile('.context/adaptive-compiler/baseline-final/freeze-manifest.json'));
 const kernel=await readFile('web/firmware/kernel_1A543a.macho'),tree=await readFile('web/firmware/devicetree_m68ap.bin');
 for(const [name,b]of [['kernel',kernel],['devicetree',tree]])assert.equal(sha(b),baseline.firmware.assetSha256[baseline.firmware[name]]);
 const {default:create}=await import(pathToFileURL(root+'/workload.cjs')),m=await create(),wabt=await wabtFactory();
 const alloc=b=>{const p=m._malloc(b.length);assert(p);m.HEAPU8.set(b,p);return p;};
 const k=alloc(kernel),d=alloc(tree),s=alloc(fixture);assert.equal(m._emu_init(k,kernel.length,d,tree.length,0,0,256),0);assert.equal(m._emu_load_snapshot(s,fixture.length),0);assert.equal(m._workload_prepare(),1);
 const compiler=createWorkloadCompiler(m,wabt);m.workloadCompile=compiler.compile;m._workload_observe();m._adaptive_enable(1);
 report.compiler=compiler.stats;
 for(let done=0;done<stop;){
  for(const e of w.events.filter(e=>e.at===done)){if(e.type==='touch')m._emu_touch(e.down,e.x,e.y);else m._emu_button(e.id,e.down);}
  const next=Math.min(stop,nextBoundary(w,done)),cpu=process.cpuUsage(),start=performance.now();
  assert.equal(m._emu_run(next-done),next-done);assert.equal(m._adaptive_failed(),0);assert.equal(m._emu_status(),0);
  compiler.drain();
  report.wallMs+=performance.now()-start;const delta=process.cpuUsage(cpu);report.cpuMs+=(delta.user+delta.system)/1000;
  done=next;report.credits=done;
  report.runtime=Array.from({length:13},(_,i)=>m._adaptive_stat(i));report.attempts=m._workload_attempts();
  if(w.checkpoints.includes(done)){
   const p=m._emu_save_snapshot(),bytes=m._emu_snapshot_size();assert(p&&bytes);
   const checkpoint={credits:done,bytes,sha256:sha(m.HEAPU8.subarray(p,p+bytes))};m._emu_free_snapshot();report.checkpoints.push(checkpoint);
   const expected=reference.runs.find(r=>r.name===w.name&&r.repeat===0).checkpoints.find(c=>c.credits===done);
   assert.deepEqual(checkpoint,expected,'Complete frozen checkpoint '+w.name+' '+done);report.validatedCheckpoints++;
  }
  await save();console.log(JSON.stringify({credits:done,generated:report.runtime[0],installed:compiler.stats.installed,cpuMs:report.cpuMs}));
 }
 assert(report.runtime[0]>0,'Actual generated retirement is required');
 report.compiler=structuredClone(compiler.stats);compiler.close();for(const file of sourceFiles)assert.equal(sha(await readFile(file)),report.sources[file],'Source changed during replay: '+file);report.passed=true;report.fullWorkload=stop===w.credits;
}catch(e){report.error=e.stack;process.exitCode=1;}
await save();console.log(JSON.stringify({passed:report.passed,credits:report.credits,error:report.error}));
