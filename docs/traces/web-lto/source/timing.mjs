// Adapted from retained .context/jit-continuation/quiet.mjs; original runtime and seed in both arms.
import {readFile,writeFile} from 'node:fs/promises';
import {resolve} from 'node:path';
import {pathToFileURL,fileURLToPath} from 'node:url';
import {createHash} from 'node:crypto';
import {gzipSync} from 'node:zlib';
import assert from 'node:assert/strict';
const dir=resolve('.context/web-lto'),out=dir+'/timing',sha=b=>createHash('sha256').update(b).digest('hex');
const pins=JSON.parse(await readFile(dir+'/timing-pins.json'));for(const [path,digest]of Object.entries(pins.files))assert.equal(sha(await readFile(path)),digest,path);
const fixture=await readFile('.context/graphics-speed/fixed-dismiss.snap'),kernel=await readFile('web/firmware/kernel_1A543a.macho'),tree=await readFile('web/firmware/devicetree_m68ap.bin');
const seedBytes=await readFile('web/jit/seed.wasm'),seed=await WebAssembly.compile(seedBytes),definitionBytes=await readFile('web/jit/regions.json'),definitions=JSON.parse(definitionBytes),abi=JSON.parse(await readFile('web/jit/abi.json'));
assert.equal(sha(seedBytes),abi.moduleSha256);assert.equal(sha(definitionBytes),abi.regionsSha256);assert.equal(definitions.length,512);
const firstReference=JSON.parse(await readFile(dir+'/retained-control-100m.json')).checkpoints.at(-1);
const report={passed:false,scope:'Fixed-work LTO CPU screening only. Both cores600M prewarmed, original512 seed shared, unchanged runtime policy. Six full raw state and exact JIT/TLB checkpoints per run. No gueststate normalization. emu_run interval/process CPU excludes restore/install/input/capture/hash. VFP conformance fails identically in baseline and candidate; relative gate only. No browser speed claim.',seedSha256:sha(seedBytes),fixtureSha256:sha(fixture),node:process.version,warmup:[],runs:[]};
const save=()=>writeFile(out+'/report.json',JSON.stringify(report,null,2));let expected=null,phase='setup';
async function setup(label){const url=pathToFileURL(`${dir}/${label}/emu.js`),{default:create}=await import(url),m=await create({locateFile:p=>fileURLToPath(new URL(p,url))});const alloc=b=>{const p=m._malloc(b.length);assert(p);m.HEAPU8.set(b,p);return p;};const k=alloc(kernel),d=alloc(tree);assert.equal(m._emu_init(k,kernel.length,d,tree.length,0,0,256),0);m._free(k);m._free(d);assert.equal(m._emu_jit_abi(),abi.revision);assert.deepEqual(Array.from({length:16},(_,i)=>m._emu_jit_offset(i)),abi.offsets);return {label,m,snapshot:alloc(fixture),mask:m._malloc(64),ids:[],coreSha256:sha(await readFile(`${dir}/${label}/emu.wasm`))};}
async function run(e){const{m}=e;m._emu_jit_reset();for(const id of e.ids.splice(0))m.removeFunction(id);assert.equal(m._emu_load_snapshot(e.snapshot,fixture.length),0);const instance=await WebAssembly.instantiate(seed,{env:{memory:m.jitMemory,stale:m._emu_jit_stale,span:m._emu_jit_span}});
 for(const r of definitions){const id=m.addFunction(instance.exports['b'+r.index],'iii');e.ids.push(id);new Uint32Array(m.jitMemory.buffer,e.mask,16).set(r.masks);assert.equal(m._emu_jit_install(r.pc,+r.thumb,id,e.mask),1);}assert.equal(m._emu_jit_stat(2),512);m._emu_jit_enable(1);m._emu_touch(1,275,250);
 const result={label:e.label,coreSha256:e.coreSha256,runMs:0,processCpuMs:0,checkpoints:[]};report.current={phase,...result};await save();const began=performance.now();
 for(let done=0;done<600000000;){const cpu=process.cpuUsage(),start=performance.now(),ran=m._emu_run(1000000);result.runMs+=performance.now()-start;const used=process.cpuUsage(cpu);result.processCpuMs+=(used.user+used.system)/1000;assert.equal(ran,1000000);done+=1000000;
  if(done===5000000)m._emu_touch(0,275,250);if(done===300000000)m._emu_button(1,1);if(done===305000000)m._emu_button(1,0);
  if(done%100000000===0){const ptr=m._emu_save_snapshot(),size=m._emu_snapshot_size();assert(ptr&&size);const view=Buffer.from(m.HEAPU8.buffer,ptr,size);
   assert.equal(view.readUInt32LE(20),40);assert.equal(view.readBigUInt64LE(24),BigInt(size-48));assert.equal(view.toString('ascii',146,150),'CPU ');assert.equal(view.readBigUInt64LE(154),428n);
   const row={done,hash:sha(view),bytes:size,tlb:[566,574,582].map(off=>view.readBigUInt64LE(off).toString()),generated:m._emu_jit_stat(0),calls:m._emu_jit_stat(1),regions:m._emu_jit_stat(2)};result.checkpoints.push(row);report.current={phase,...result};await save();
   try{
    assert(row.generated>0);
    if(done===100000000){assert.equal(row.hash,firstReference.rawSha256);assert.equal(row.bytes,firstReference.bytes);for(const k of ['generated','calls','regions'])assert.equal(row[k],firstReference[k]);}
    if(expected)assert.deepEqual(row,expected[done/100000000-1]);
   }catch(error){await writeFile(`${out}/${phase}-${done}-failure.snap.gz`,gzipSync(view));throw error;}finally{m._emu_free_snapshot();}
  }
 }
 const rgba=m._malloc(320*480*4),dims=m._malloc(8);assert.equal(m._emu_frame(rgba,dims,dims+4),1);const width=m.HEAPU32[dims>>>2],height=m.HEAPU32[(dims+4)>>>2];result.frame={width,height,sha256:sha(m.HEAPU8.subarray(rgba,rgba+width*height*4))};m._free(rgba);m._free(dims);result.elapsedIncludingChecksMs=performance.now()-began;return result;
}
try{
 const engines={A:await setup('control'),B:await setup('candidate')};
 for(const key of ['A','B']){phase='warmup-'+key;const r=await run(engines[key]);report.warmup.push(r);if(!expected)expected=r.checkpoints;else assert.deepEqual(r.frame,report.warmup[0].frame);await save();console.log(JSON.stringify({phase,passed:true,checkpoints:r.checkpoints.length}));}
 report.measuredStartUTC=new Date().toISOString();
 for(const [i,key]of ['A','B','B','A'].entries()){phase=`arm-${i+1}-${key}`;const r=await run(engines[key]);assert.deepEqual(r.frame,report.warmup[0].frame);report.runs.push(r);await save();console.log(JSON.stringify({phase,runMs:r.runMs,processCpuMs:r.processCpuMs,passed:true}));}
 report.measuredEndUTC=new Date().toISOString();report.passed=true;delete report.current;
}catch(error){report.error={phase,message:error.message,stack:error.stack};process.exitCode=1;}finally{await save();console.log(JSON.stringify({end:true,passed:report.passed,error:report.error?.message}));}
