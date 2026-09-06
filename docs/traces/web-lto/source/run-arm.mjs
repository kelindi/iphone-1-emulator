import fs from 'node:fs/promises';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {gunzipSync,gzipSync} from 'node:zlib';
import {resolve} from 'node:path';
import {pathToFileURL} from 'node:url';
const base=resolve('.context/web-lto'),kind=process.argv[2];assert(['interpreter','seeded'].includes(kind));
const dir=`${base}/replay/${kind}`;await fs.mkdir(dir,{recursive:false});
const sha=b=>createHash('sha256').update(b).digest('hex');
const pins=JSON.parse(await fs.readFile(`${base}/replay-pins.json`,'utf8')),expected=JSON.parse(await fs.readFile(`${base}/retained-control-100m.json`,'utf8'));
const report={kind,passed:false,vfpConformancePassed:false,scope:'Relative LTO correctness only. Baseline VFP literal595pass/12fail retained. No timing claim or normalization.',checkpoints:[],started:new Date().toISOString()};
const save=()=>fs.writeFile(`${dir}/report.json`,JSON.stringify(report,null,2));
try{
 for(const [file,h]of Object.entries(pins.files))assert.equal(sha(await fs.readFile(file)),h,file);
 const {default:create}=await import(pathToFileURL(`${base}/candidate/emu.js`));const m=await create({locateFile:f=>`${base}/candidate/${f}`});
 const alloc=b=>{const p=m._malloc(b.length);assert(p);m.HEAPU8.set(b,p);return p;};
 const kernel=await fs.readFile('web/firmware/kernel_1A543a.macho'),tree=await fs.readFile('web/firmware/devicetree_m68ap.bin');let p=alloc(kernel),q=alloc(tree);assert.equal(m._emu_init(p,kernel.length,q,tree.length,0,0,256),0);m._free(p);m._free(q);
 let fixture=await fs.readFile(expected.fixturePath);assert.equal(sha(fixture),expected.fixtureSha256);p=alloc(fixture);assert.equal(m._emu_load_snapshot(p,fixture.length),0);m._free(p);fixture=null;
 const abi=JSON.parse(await fs.readFile('web/jit/abi.json'));assert.equal(m._emu_jit_abi(),abi.revision);assert.deepEqual(Array.from({length:16},(_,i)=>m._emu_jit_offset(i)),abi.offsets);
 if(kind==='seeded'){
  const seed=await fs.readFile('web/jit/seed.wasm');assert.equal(sha(seed),expected.seedSha256);const defs=JSON.parse(await fs.readFile('web/jit/regions.json'));assert.equal(defs.length,512);
  const {instance}=await WebAssembly.instantiate(seed,{env:{memory:m.jitMemory,stale:m._emu_jit_stale,span:m._emu_jit_span}});p=m._malloc(64);
  for(const r of defs){const id=m.addFunction(instance.exports['b'+r.index],'iii');m.HEAPU32.set(r.masks,p>>>2);assert.equal(m._emu_jit_install(r.pc,+r.thumb,id,p),1);}m._free(p);assert.equal(m._emu_jit_stat(2),512);
 }
 m._emu_jit_enable(kind==='seeded'?1:0);report.initialRegions=m._emu_jit_stat(2);report.moduleSha256=pins.files[`${base}/candidate/emu.wasm`];report.fixtureSha256=expected.fixtureSha256;report.seedSha256=expected.seedSha256;await save();
 m._emu_touch(1,275,250);
 for(let done=0;done<100000000;){assert.equal(m._emu_run(1000000),1000000);done+=1000000;if(done===5000000)m._emu_touch(0,275,250);
  if(done%25000000===0){
   const stat=await fs.statfs(base);assert(Number(stat.bavail)*Number(stat.bsize)>4.2*2**30,'disk floor before snapshot');
   const ptr=m._emu_save_snapshot(),n=m._emu_snapshot_size();assert(ptr&&n);const raw=m.HEAPU8.subarray(ptr,ptr+n),hash=sha(raw),compressed=gzipSync(raw);
   const file=`raw-${done}.snap.gz`;await fs.writeFile(`${dir}/${file}`,compressed);
   // Persist complete lossless raw state BEFORE any equivalence assertion.
   const row={done,rawSha256:hash,bytes:n,gzipSha256:sha(compressed),gzipBytes:compressed.length,file,generated:m._emu_jit_stat(0),calls:m._emu_jit_stat(1),regions:m._emu_jit_stat(2)};report.checkpoints.push(row);await save();
   assert.equal(sha(gunzipSync(await fs.readFile(`${dir}/${file}`))),hash,'persisted lossless state differs');m._emu_free_snapshot();
   const ref=expected.checkpoints.find(x=>x.done===done);assert.equal(n,ref.bytes,'snapshot size');assert.equal(hash,ref.rawSha256,`full raw state at${done}`);
   if(kind==='seeded'){for(const name of ['generated','calls','regions'])assert.equal(row[name],ref[name],name);assert(row.generated>0);}else{assert.equal(row.generated,0);assert.equal(row.calls,0);}
   console.log(JSON.stringify({kind,done,rawSha256:hash,generated:row.generated,passed:true}));
  }
 }
 p=m._malloc(320*480*4);q=m._malloc(8);assert.equal(m._emu_frame(p,q,q+4),1);const width=m.HEAPU32[q>>>2],height=m.HEAPU32[(q+4)>>>2],raw=m.HEAPU8.subarray(p,p+width*height*4);report.frame={width,height,sha256:sha(raw)};await fs.writeFile(`${dir}/frame.rgba.gz`,gzipSync(raw));await save();assert.deepEqual(report.frame,expected.frame);m._free(p);m._free(q);
 report.passed=true;report.ended=new Date().toISOString();await save();console.log(JSON.stringify({kind,passed:true,checkpoints:4}));
}catch(e){report.error={message:e.message,stack:e.stack};await save();throw e;}
