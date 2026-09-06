import {readFile,writeFile} from 'node:fs/promises';
import {gunzipSync} from 'node:zlib';
import {createHash} from 'node:crypto';
import {resolve} from 'node:path';
import vm from 'node:vm';
import assert from 'node:assert/strict';
import wabtFactory from 'wabt';
const root=resolve('.context/jit-register-layout'),hash=b=>createHash('sha256').update(b).digest('hex');
const {default:createEmu}=await import(resolve('web/dist/emu.js'));
const m=await createEmu({locateFile:p=>resolve('web/dist/'+p)});
const alloc=b=>{const p=m._malloc(b.length);assert(p);m.HEAPU8.set(b,p);return p;};
const k=await readFile('web/firmware/kernel_1A543a.macho'),d=await readFile('web/firmware/devicetree_m68ap.bin'),kp=alloc(k),dp=alloc(d);
assert.equal(m._emu_init(kp,k.length,dp,d.length,0,0,256),0);m._free(kp);m._free(dp);
const fixture=await readFile('.context/graphics-speed/fixed-dismiss.snap'),sp=alloc(fixture),slots=512,diag=m._malloc(slots*8),mask=m._malloc(64);assert(diag&&mask);
const abi=JSON.parse(await readFile('web/jit/abi.json')),defs=JSON.parse(await readFile('web/jit/regions.json')),seed=await readFile('web/jit/seed.wasm');
const offsets=Array.from({length:16},(_,i)=>m._emu_jit_offset(i));assert.deepEqual(offsets,abi.offsets);assert.equal(m._emu_jit_abi(),abi.revision);assert.equal(hash(seed),abi.moduleSha256);
const input=JSON.parse(gunzipSync(await readFile('tools/jit/seed-input.json.gz'))),original=await readFile('tools/jit/compiler.cjs','utf8');
let source=original;
const helper=`
function censusCount(slot) { return '(;CENSUS_BEGIN;)(i64.store '+C(${diag}+slot*8)+'(i64.add(i64.load '+C(${diag}+slot*8)+')(i64.const 1)))(;CENSUS_END;)'; }
`;
source=source.replace('function compileRegion(e){',helper+'\nfunction compileRegion(e){');
assert.equal(source.split("let body='';").length,2);
source=source.replace("let body='';","let body=censusCount(e.index);");
await writeFile(root+'/compiler-census.cjs',source);
const baselineBox={module:{exports:{}}},censusBox={module:{exports:{}}};vm.runInNewContext(original,baselineBox);vm.runInNewContext(source,censusBox);
const baseline=baselineBox.module.exports,census=censusBox.module.exports;baseline.configure(offsets);census.configure(offsets);
const funcs=[],bodyProof=[];const strip=s=>s.replaceAll(/\(;CENSUS_BEGIN;\)[\s\S]*?\(;CENSUS_END;\)/g,'');
for(let i=0;i<input.regions.length;i++){
 const r=input.regions[i],e={...r,ops:new Map(r.ops)},a=baseline.compileRegion(e),b=census.compileRegion(e);assert(a&&b);assert.equal(strip(b.wat),a.wat);assert.deepEqual(Array.from(b.masks,x=>x>>>0),defs[i].masks);assert.equal(r.pc,defs[i].pc);assert.equal(r.index,defs[i].index);assert.equal(r.thumb,defs[i].thumb);
 funcs.push(b.wat);bodyProof.push({index:r.index,originalWatSha256:hash(a.wat),instrumentedWatSha256:hash(b.wat),strippedWatIdentical:true});
}
const wabt=await wabtFactory(),parsed=wabt.parseWat('census.wat',`(module(import "env" "memory"(memory 8192))(import "env" "stale"(func $stale(param i32 i32)))(import "env" "span"(func $span(param i32 i32 i32 i32 i32)(result i32)))${funcs.join('')})`);parsed.validate();const binary=Buffer.from(parsed.toBinary({}).buffer);parsed.destroy();await writeFile(root+'/census-seed.wasm',binary);
const modules=[null,await WebAssembly.compile(seed),await WebAssembly.compile(binary)],ids=[];
const report={scope:'Actual original-seed region-call counts in fixed100M MBX Settings workload. Every register load/spill, guard, instruction and policy preserved. Static register references weighted by actual calls, not a compiler optimization or speed result.',runtimeSha256:hash(await readFile('web/dist/emu.wasm')),seedSha256:hash(seed),inputSha256:hash(await readFile('tools/jit/seed-input.json.gz')),compilerSha256:hash(original),instrumentedCompilerSha256:hash(source),diagnosticSeedSha256:hash(binary),fixtureSha256:hash(fixture),counterSlots:slots,counterBytes:slots*8,abiRevision:abi.revision,bodyProof,runs:[]};
for(let mode=0;mode<3;mode++){
 m._emu_jit_reset();for(const id of ids.splice(0))m.removeFunction(id);assert.equal(m._emu_load_snapshot(sp,fixture.length),0);m.HEAPU8.fill(0,diag,diag+slots*8);
 if(mode){const instance=await WebAssembly.instantiate(modules[mode],{env:{memory:m.jitMemory,stale:m._emu_jit_stale,span:m._emu_jit_span}});for(const r of defs){const id=m.addFunction(instance.exports['b'+r.index],'iii');ids.push(id);new Uint32Array(m.jitMemory.buffer,mask,16).set(r.masks);assert.equal(m._emu_jit_install(r.pc,+r.thumb,id,mask),1);}}
 m._emu_jit_enable(mode?1:0);m._emu_touch(1,275,250);for(let done=0;done<100e6;done+=1e6){assert.equal(m._emu_run(1e6),1e6);if(done+1e6===5e6)m._emu_touch(0,275,250);}
 const p=m._emu_save_snapshot(),size=m._emu_snapshot_size();assert(p&&size);const snapshotHash=hash(m.HEAPU8.subarray(p,p+size));m._emu_free_snapshot();
 const row={mode:['interpreter','productionSeed','diagnosticSeed'][mode],snapshotHash,generated:m._emu_jit_stat(0),calls:m._emu_jit_stat(1),regions:m._emu_jit_stat(2)};
 if(mode){assert.equal(snapshotHash,report.runs[0].snapshotHash);assert(row.generated>0&&row.calls>0);}if(mode===2){for(const key of ['generated','calls','regions'])assert.equal(row[key],report.runs[1][key]);report.counters=Array.from(new BigUint64Array(m.jitMemory.buffer,diag,slots),Number);}
 report.runs.push(row);console.log(JSON.stringify(row));
}
const layout=JSON.parse(await readFile(root+'/inventory.json','utf8'));
assert.equal(layout.compilerSha256,report.compilerSha256);
report.weighted=layout.details.map(r=>({...r,calls:report.counters[r.index]}));
const sum=fn=>report.weighted.reduce((n,r)=>n+fn(r),0);
assert.equal(sum(r=>r.calls),report.runs[2].calls);
report.derived={calls:sum(r=>r.calls),nominalRegisterLoads:sum(r=>r.calls*16),nominalRegisterSpills:sum(r=>r.calls*16),
 spillsToRegionNeverWrittenRegisters:sum(r=>r.calls*r.neverWritten.length),
 loadsOfRegionNeverReferencedRegisters:sum(r=>r.calls*r.neverReferenced.length)};
report.limit='These are nominal emitter operations. Host compiler may optimize some; no CPU-time attribution or guaranteed savings. Any candidate must preserve imported callbacks, all registers, zero/stale exits, and physical aliases.';
report.passed=true;
await writeFile(root+'/census.json',JSON.stringify(report,null,2)+'\n');
console.log(JSON.stringify({derived:report.derived,passed:true}));
m._emu_jit_reset();for(const id of ids)m.removeFunction(id);m._free(mask);m._free(diag);m._free(sp);
