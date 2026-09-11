// Qualification capture only. No candidate engine or adaptive speed claim.
import {readFile,writeFile,mkdir} from 'node:fs/promises';
import {resolve} from 'node:path';
import {pathToFileURL} from 'node:url';
import {createHash} from 'node:crypto';
import {gzipSync} from 'node:zlib';
import assert from 'node:assert/strict';
import {workloads,validateWorkload,nextBoundary} from './workloads.mjs';
import {classifyPerformanceScreen} from '../../../web/tests/helpers/performance-home.mjs';
const root=resolve(process.env.BASELINE_ROOT||'.context/adaptive-compiler/baseline-final');
const out=resolve(process.env.ARTIFACT_DIR||'.context/adaptive-compiler/workloads');
const fixturePath=process.env.FIXTURE_SNAPSHOT;
if(!fixturePath)throw Error('FIXTURE_SNAPSHOT must name a raw verified Home snapshot');
await mkdir(out); // Never replace a previous capture.
const sha=b=>createHash('sha256').update(b).digest('hex');
const manifest=JSON.parse(await readFile(root+'/freeze-manifest.json'));
const pin=async(relative)=>{const bytes=await readFile(root+'/'+relative);assert.equal(sha(bytes),manifest.entries.find(e=>e.relative===relative)?.sha256,'Frozen identity '+relative);return bytes;};
const fixture=await readFile(fixturePath),kernel=await readFile('web/firmware/kernel_1A543a.macho'),tree=await readFile('web/firmware/devicetree_m68ap.bin');
for(const [name,bytes] of [['kernel',kernel],['devicetree',tree]])assert.equal(sha(bytes),manifest.firmware.assetSha256[manifest.firmware[name]],'Firmware identity '+name);
await pin('site/dist/emu.js');await pin('site/dist/emu.wasm');
const seed=await WebAssembly.compile(await pin('site/jit/seed.wasm')),regions=JSON.parse(await pin('site/jit/regions.json'));
const report={passed:false,scope:'Deterministic seeded-engine workload qualification; scheduler credits, not fetched instructions. No candidate or browser-speed claim.',fixture:{path:resolve(fixturePath),sha256:sha(fixture),bytes:fixture.length},freezeManifestSha256:sha(await readFile(root+'/freeze-manifest.json')),workloads,runs:[]};
const save=()=>writeFile(out+'/report.json',JSON.stringify(report,null,2));
let m,ids=[],ptrs=[];
try{
 const {default:create}=await import(pathToFileURL(root+'/site/dist/emu.js'));
 m=await create({locateFile:p=>root+'/site/dist/'+p});
 const alloc=b=>{const p=m._malloc(b.length);assert(p);ptrs.push(p);m.HEAPU8.set(b,p);return p;};
 const k=alloc(kernel),d=alloc(tree);assert.equal(m._emu_init(k,kernel.length,d,tree.length,0,0,256),0);
 const snap=alloc(fixture),mask=alloc(Buffer.alloc(64)),frame=alloc(Buffer.alloc(320*480*4)),dimensions=alloc(Buffer.alloc(8));
 function pixels(){assert.equal(m._emu_frame(frame,dimensions,dimensions+4),1);assert.deepEqual(Array.from(m.HEAPU32.subarray(dimensions>>>2,(dimensions>>>2)+2)),[320,480]);return Buffer.from(m.HEAPU8.subarray(frame,frame+320*480*4));}
 for(let repeat=0;repeat<2;repeat++)for(const w of workloads){
  validateWorkload(w);m._emu_jit_reset();for(const id of ids.splice(0))m.removeFunction(id);
  assert.equal(m._emu_load_snapshot(snap,fixture.length),0);assert.equal(classifyPerformanceScreen(pixels()).kind,'home','Fixture must start Home');
  const instance=await WebAssembly.instantiate(seed,{env:{memory:m.jitMemory,stale:m._emu_jit_stale,span:m._emu_jit_span}});
  for(const r of regions){const id=m.addFunction(instance.exports['b'+r.index],'iii');ids.push(id);m.HEAPU32.set(r.masks,mask>>>2);assert.equal(m._emu_jit_install(r.pc,+r.thumb,id,mask),1);}m._emu_jit_enable(1);
  const row={name:w.name,repeat,cpuMs:0,wallMs:0,checkpoints:[]};report.runs.push(row);
  for(let done=0;done<w.credits;){
   for(const e of w.events.filter(e=>e.at===done)){if(e.type==='touch')m._emu_touch(e.down,e.x,e.y);else m._emu_button(e.id,e.down);}
   const next=nextBoundary(w,done),cpu=process.cpuUsage(),start=performance.now();assert.equal(m._emu_run(next-done),next-done);row.wallMs+=performance.now()-start;const delta=process.cpuUsage(cpu);row.cpuMs+=(delta.user+delta.system)/1000;done=next;
   if(w.checkpoints.includes(done)){
    const p=m._emu_save_snapshot(),bytes=m._emu_snapshot_size();assert(p&&bytes);row.checkpoints.push({credits:done,bytes,sha256:sha(m.HEAPU8.subarray(p,p+bytes))});m._emu_free_snapshot();
    await writeFile(`${out}/${repeat}-${w.name}-${done}.rgba.gz`,gzipSync(pixels()));await save();
   }
  }
  row.generated=m._emu_jit_stat(0);assert(row.generated>0,'Generated path must execute');
  if(repeat)assert.deepEqual(row.checkpoints,report.runs.find(r=>r.name===w.name&&r.repeat===0).checkpoints,'Full-state determinism '+w.name);
  await save();console.log(JSON.stringify(row));
 }
 report.passed=true;
}catch(e){report.error=e.stack;process.exitCode=1;}
finally{if(m){m._emu_jit_reset();for(const id of ids)m.removeFunction(id);for(const p of ptrs)m._free(p);}await save();}
