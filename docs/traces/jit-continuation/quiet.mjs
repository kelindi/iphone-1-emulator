import {readFile,writeFile,rm} from 'node:fs/promises';
import {existsSync} from 'node:fs';
import {resolve} from 'node:path';
import {pathToFileURL,fileURLToPath} from 'node:url';
import {createHash} from 'node:crypto';
const dir=resolve('.context/jit-continuation'),flag=dir+'/quiet-start';await rm(flag,{force:true});
const sha=b=>createHash('sha256').update(b).digest('hex');
const fixture=await readFile('.context/graphics-speed/fixed-dismiss.snap'),kernel=await readFile('web/firmware/kernel_1A543a.macho'),tree=await readFile('web/firmware/devicetree_m68ap.bin');
const seedBytes=await readFile('web/jit/seed.wasm'),seed=await WebAssembly.compile(seedBytes),definitionBytes=await readFile('web/jit/regions.json'),definitions=JSON.parse(definitionBytes),abi=JSON.parse(await readFile('web/jit/abi.json'));
if(sha(seedBytes)!==abi.moduleSha256||sha(definitionBytes)!==abi.regionsSha256)throw Error('Seed hash mismatch');
const correctness=JSON.parse(await readFile(dir+'/correctness-600m.json'));
const expected=correctness.fixtures.find(f=>f.fixture==='mbx').runs.filter(r=>r.enabled===1);
if(!correctness.passed || expected.length!==6)throw Error('Incomplete frozen references');
const report={scope:'Two full600M-prewarmed cores; original seed shared, runtime continuation is the only core change. Each600M MBX replay restores identical bytes. Six full raw SHA/TLB checkpoints plus engine-specific frozen JIT policy counters. ABBA synchronous emu_run intervals and process CPU exclude restore, seed install, input, capture and hashing. No browser claim.',seedSha256:sha(seedBytes),fixtureSha256:sha(fixture),node:process.version,warmup:[],runs:[]};
const engines={};
async function setup(label,path){const url=pathToFileURL(resolve(path));const {default:createEmu}=await import(url);const m=await createEmu({locateFile:p=>fileURLToPath(new URL(p,url))});const alloc=b=>{const p=m._malloc(b.length);if(!p)throw Error('allocation');m.HEAPU8.set(b,p);return p;};const k=alloc(kernel),d=alloc(tree);if(m._emu_init(k,kernel.length,d,tree.length,0,0,256))throw Error('init');m._free(k);m._free(d);if(m._emu_jit_abi()!==abi.revision||JSON.stringify(Array.from({length:16},(_,i)=>m._emu_jit_offset(i)))!==JSON.stringify(abi.offsets))throw Error('ABI mismatch');return {label,m,snapshot:alloc(fixture),mask:m._malloc(64),ids:[],coreSha256:sha(await readFile(fileURLToPath(new URL('emu.wasm',url))))};}
async function run(e){
 const {m}=e;m._emu_jit_reset();for(const id of e.ids.splice(0))m.removeFunction(id);
 if(m._emu_load_snapshot(e.snapshot,fixture.length))throw Error('restore');
 const instance=await WebAssembly.instantiate(seed,{env:{memory:m.jitMemory,stale:m._emu_jit_stale,span:m._emu_jit_span}});
 for(const r of definitions){const id=m.addFunction(instance.exports['b'+r.index],'iii');e.ids.push(id);new Uint32Array(m.jitMemory.buffer,e.mask,16).set(r.masks);if(!m._emu_jit_install(r.pc,r.thumb?1:0,id,e.mask))throw Error('install');}
 m._emu_jit_enable(1);m._emu_touch(1,275,250);let runMs=0,processCpuMs=0;const began=performance.now(),checkpoints=[];
 for(let done=0;done<600e6;){
  const cpu=process.cpuUsage(),start=performance.now(),ran=m._emu_run(1e6);runMs+=performance.now()-start;const used=process.cpuUsage(cpu);processCpuMs+=(used.user+used.system)/1000;
  if(ran!==1e6)throw Error('retirement');done+=1e6;
  if(done===5e6)m._emu_touch(0,275,250);if(done===300e6)m._emu_button(1,1);if(done===305e6)m._emu_button(1,0);
  if(done%100e6===0){
   const p=m._emu_save_snapshot(),size=m._emu_snapshot_size();if(!p||!size)throw Error('snapshot');
   const view=Buffer.from(m.HEAPU8.buffer,p,size);
   if(view.readUInt32LE(20)!==40 || view.readBigUInt64LE(24)!==BigInt(size-48) || view.toString('ascii',146,150)!=='CPU ' || view.readBigUInt64LE(154)!==428n)throw Error('Snapshot schema');
   const row={done,hash:sha(view),tlb:[566,574,582].map(off=>view.readBigUInt64LE(off).toString()),generated:m._emu_jit_stat(0),calls:m._emu_jit_stat(1),regions:m._emu_jit_stat(2)};
   m._emu_free_snapshot();const ref=expected[done/100e6-1];
   if(row.hash!==ref.hash || JSON.stringify(row.tlb)!==JSON.stringify(ref.tlb))throw Error('Raw/TLB mismatch '+JSON.stringify(row));
   for(const key of ['generated','calls','regions']){const name=e.label==='baseline'?'original'+key[0].toUpperCase()+key.slice(1):key;if(row[key]!==ref[name])throw Error('Policy counter mismatch '+key);}
   checkpoints.push(row);
  }
 }
 return {label:e.label,coreSha256:e.coreSha256,runMs,processCpuMs,elapsedIncludingChecksMs:performance.now()-began,checkpoints};
}
try{engines.A=await setup('baseline',dir+'/baseline-dist/emu.js');engines.B=await setup('continuation',dir+'/dist/emu.js');if(engines.A.coreSha256!=='b3237ef3843412aea164d3db111299650ea8433bf16311417116799959ab6606'||engines.B.coreSha256!=='9220380b7a2a45b33ed965796d252af33b65ec6cae5129be34111c5d18ebcce3'||report.seedSha256!=='d9c661556ab17bb8433ed9b0c3ecfd52e2fcafe58221b4cb84a920e4976b3881')throw Error('Frozen artifact mismatch');for(const key of ['A','B']){const row=await run(engines[key]);report.warmup.push(row);console.log('WARMUP',JSON.stringify(row));}await writeFile(dir+'/quiet-ready.json',JSON.stringify(report,null,2)+'\n');console.log('READY for quiet-start');while(!existsSync(flag))await new Promise(r=>setTimeout(r,250));report.startUnixMs=Date.now();for(const key of ['A','B','B','A']){const row=await run(engines[key]);report.runs.push(row);console.log('RUN',JSON.stringify(row));await writeFile(dir+'/quiet-report.json',JSON.stringify(report,null,2)+'\n');}report.endUnixMs=Date.now();report.passed=true;}catch(error){report.passed=false;report.error=error.stack;process.exitCode=1;}finally{await writeFile(dir+'/quiet-report.json',JSON.stringify(report,null,2)+'\n');console.log('END',report.passed);}
