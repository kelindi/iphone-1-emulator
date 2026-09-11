import {createRequire} from 'node:module';
import {resolve} from 'node:path';
import {readFile,writeFile,mkdir} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import assert from 'node:assert/strict';
import wabtFactory from 'wabt';
import {buildUnit} from '../unit.mjs';
import {emitUnit} from '../emit.mjs';
const require=createRequire(import.meta.url),root=resolve(process.env.ARTIFACT_DIR||'.context/adaptive-compiler'),modulePath=resolve(process.env.MACHINE_MODULE||root+'/machine-bridge.cjs'),m=await require(modulePath)(),wabt=await wabtFactory();
assert.equal(m._test_begin(0),1);
const drainBuffer=m._malloc(8192),owned=new Set(),sha=b=>createHash('sha256').update(b).digest('hex');
const report={passed:false,scope:'Actual emitted WASM, guarded C registry and real machine scheduler; full snapshots on synthetic integer loops. Not real-phone workloads or performance acceptance.',cases:[]};
let guards=0,readCalls=0,readHits=0,writeCalls=0,writeHits=0,writeBoundaries=0;
function drain(){const n=m._test_drain(drainBuffer,2048),ids=Array.from(new Uint32Array(m.jitMemory.buffer,drainBuffer,n));for(const id of ids){assert(owned.delete(id),'Retired handle must have exactly one owner');m.removeFunction(id);}return ids;}
function setup(ops,mmu=false,timer=false,thumb=false){new Uint32Array(m.jitMemory.buffer,m._test_program(),512).fill(0);const words=new Uint32Array(m.jitMemory.buffer,m._test_program(),512);for(const [pc,raw]of ops)if(thumb)new DataView(words.buffer).setUint16(words.byteOffset+pc,raw,true);else words[pc/4]=raw;assert.equal(m._test_begin(512),1);drain();if(thumb)m._test_thumb();if(mmu)m._test_mmu(mmu==='alias'?1:0);if(timer)m._test_timer();assert.equal(m._test_warm(0,0),1);assert.equal(m._test_warm(1024,1),1);guards=0;readCalls=0;readHits=0;writeCalls=0;writeHits=0;writeBoundaries=0;}
function prepare(spec){
 setup(spec.ops,spec.mmu,spec.timer,spec.thumb);
 if(spec.aliasWrite)m._test_alias_write();
 if(spec.track){assert.equal(m._test_track(),1);assert.equal(m._test_warm(0,0),1);assert.equal(m._test_warm(1024,1),1);}
 const view=new DataView(m.jitMemory.buffer);
 for(const [reg,value]of spec.registers||[])view.setUint32(m._test_cpu()+m._test_offset(0)+reg*4,value,true);
 for(const [address,value]of spec.data||[])view.setUint32(m._test_ram()+address,value,true);
 if(spec.unaligned!==undefined){m._test_unaligned(spec.unaligned?1:0);assert.equal(m._test_warm(0,0),1);assert.equal(m._test_warm(1024,1),1);}
 for(const [va,value]of spec.warmWrites||[])assert.equal(m._test_warm_write(va,value),1);
 if(spec.warmWrites){assert.equal(m._test_warm(0,0),1);assert.equal(m._test_warm(1024,1),1);}
 if(spec.directWrites!==undefined)m._test_direct_writes(spec.directWrites?1:0);
 if(spec.alignment){m._test_alignment();assert.equal(m._test_warm(0,0),1);assert.equal(m._test_warm(1024,1),1);}
}
async function install(ops,physicalOffset=0,thumb=false){const regions=[0,1024].map(base=>{const bytes=new Uint8Array(1024),v=new DataView(bytes.buffer);for(const [pc,raw]of ops)if((pc&~1023)===base)v[thumb?'setUint16':'setUint32'](pc-base,raw,true);return {virtualBase:base,physicalBase:base+physicalOffset,version:1,bytes};});
 const unit=buildUnit({entryPc:0,regions,thumb}),parsed=wabt.parseWat('machine-unit.wat',emitUnit(unit,{registers:m._test_offset(0),cpsr:m._test_offset(1)}));let binary;try{parsed.validate();binary=parsed.toBinary({}).buffer;}finally{parsed.destroy();}
 const {instance}=await WebAssembly.instantiate(binary,{env:{memory:m.jitMemory,guard(pc,raw,width){guards++;return m._test_guard(pc,raw,width);},read(va,width,privileged){readCalls++;const pointer=m._test_read(va,width,privileged);if(pointer)readHits++;return pointer;},write(va,width,privileged,value){writeCalls++;const status=m._test_write(va,width,privileged,value);if(status)writeHits++;if(status===2)writeBoundaries++;return status;}}});
 const id=m.addFunction(instance.exports.run,'viii');assert.equal(m._test_install(id,0,2,m._test_epoch(),binary.length),1);owned.add(id);
 assert.equal(m._test_install(id,0,2,m._test_epoch(),binary.length),0,'Duplicate ownership rejected');return id;
}
function snapshot(){const p=m._test_snapshot(),n=m._test_snapshot_size();assert(p&&n);return Buffer.from(new Uint8Array(m.jitMemory.buffer,p,n));}
try{
 for(const spec of [
  {name:'connected loop',ops:[[0,0xe2800001],[4,0xeafffffd]]},
  {name:'cross-window loop',ops:[[0,0xea0000fe],[1024,0xe2800001],[1028,0xeafffefd]]},
  {name:'MMU register-shift ALU loop',mmu:true,ops:[[0,0xe3a00001],[4,0xe3a01021],[8,0xe1b00170],[12,0xe08220a0],[16,0xeafffffa]]},
  {name:'MMU Thumb arithmetic loop',mmu:true,thumb:true,ops:[[0,0x3001],[2,0x0041],[4,0x4051],[6,0x4351],[8,0xe7fa]],registers:[[2,3]]},
  {name:'MMU-on connected loop',mmu:true,ops:[[0,0xe2800001],[4,0xeafffffd]]},
  {name:'MMU-on cross-window loop',mmu:true,ops:[[0,0xea0000fe],[1024,0xe2800001],[1028,0xeafffefd]]},
  {name:'MMU guarded slow instruction',mmu:true,ops:[[0,0xea0000fe],[1024,0xe5910000],[1028,0xeafffefd]]},
  {name:'MMU nonidentity small-page mapping',mmu:'alias',ops:[[0,0xea0000fe],[1024,0xe2800001],[1028,0xeafffefd]]},
  {name:'timer IRQ after compiled prefix',timer:true,ops:[[0,0xe2800001],[4,0xeafffffd],[0x18,0xe3a0205a],[0x1c,0xeafffffe]]},
  {name:'MMU load/store writeback handoff',mmu:true,registers:[[1,0x9000]],ops:[[0,0xe2800001],[4,0xe5a10004],[8,0xe4912004],[12,0xeafffffb]]},
  {name:'MMU false-condition load has no fault',mmu:true,registers:[[1,0x200000]],ops:[[0,0xe2800001],[4,0x05912000],[8,0xeafffffc]],expectFault:0},
  {name:'MMU load translation abort after prefix',mmu:true,registers:[[1,0x200000],[2,0x12345678]],ops:[[0,0xe2800001],[4,0xe3500008],[8,0x1afffffc],[12,0xea00000b],[16,0xeafffffe],[64,0xe5b12004],[68,0xeafffffe]],expectFault:0x200004,expectRegisters:[[0,8],[1,0x200000],[2,0x12345678]]},
  {name:'MMU alignment abort after prefix',mmu:true,alignment:true,registers:[[1,0x9001],[2,0x12345678]],ops:[[0,0xe2800001],[4,0xe3500008],[8,0x1afffffc],[12,0xea00000b],[16,0xeafffffe],[64,0xe5912000],[68,0xeafffffe]],expectFault:0x9001,expectRegisters:[[0,8],[1,0x9001],[2,0x12345678]]},
  {name:'MMU partial LDM abort preserves destination registers',mmu:true,registers:[[1,0xffffc],[2,0x22222222],[3,0x33333333]],data:[[0xffffc,0x12345678]],ops:[[0,0xe2800001],[4,0xe3500008],[8,0x1afffffc],[12,0xea00000b],[16,0xeafffffe],[64,0xe8b1000c],[68,0xeafffffe]],expectFault:0x100000,expectRegisters:[[0,8],[1,0xffffc],[2,0x22222222],[3,0x33333333]]},
  {name:'MMU partial STM abort preserves first store',mmu:true,registers:[[1,0xffffc],[2,0x12345678],[3,0x33333333]],ops:[[0,0xe2800001],[4,0xe3500008],[8,0x1afffffc],[12,0xea00000b],[16,0xeafffffe],[64,0xe8a1000c],[68,0xeafffffe]],expectFault:0x100000,expectRegisters:[[0,8],[1,0xffffc]],expectMemory:[[0xffffc,0x12345678]]},
  {name:'guest store changes compiled instruction',registers:[[1,0],[2,0xe2800007]],ops:[[0,0xe2800001],[4,0xe3500008],[8,0x1afffffc],[12,0xe5812000],[16,0xeafffffa]],expectRetired:1},
  {name:'MMIO timer register store/load handoff',registers:[[1,0x3e2000a8],[2,7]],ops:[[0,0xe2800001],[4,0xe5812000],[8,0xe5913000],[12,0xeafffffb]],expectRegisters:[[3,7]]},
  {name:'compiled cold RAM load refuses without double miss',mmu:true,registers:[[1,0x9000]],data:[[0x9000,0x12345678]],ops:[[0,0xe2833001],[4,0xe3530008],[8,0x1afffffc],[12,0xe5910000],[16,0xeafffffe]],expectReadHits:false,expectRegisters:[[0,0x12345678],[3,8]]},
  {name:'compiled device load remains reference',registers:[[1,0x3e2000a8]],ops:[[0,0xe5910000],[4,0xe2833001],[8,0xeafffffc]],expectReadHits:false,expectRegisters:[[0,0]]},
  {name:'MMU compiled warm word loads',mmu:true,registers:[[1,0x9000]],data:[[0x9000,0x12345678]],ops:[[0,0xe5910000],[4,0xe2833001],[8,0xeafffffc]],expectReadHits:true,expectRegisters:[[0,0x12345678]]},
  {name:'MMU compiled post-index loads',mmu:true,registers:[[1,0x9000]],data:[[0x9000,0x12345678],[0x9004,0xabcdef01]],ops:[[0,0xe4910004],[4,0xe2833001],[8,0xeafffffc]],expectReadHits:true},
  {name:'MMU compiled unprivileged translated loads',mmu:true,registers:[[1,0x9000]],data:[[0x9000,0xfedcba98]],ops:[[0,0xe4b10000],[4,0xe2833001],[8,0xeafffffc]],expectReadHits:true,expectRegisters:[[0,0xfedcba98]]},
  {name:'MMU compiled byte loads zero extend',mmu:true,registers:[[1,0x9000]],data:[[0x9000,0x000000ff]],ops:[[0,0xe5d10000],[4,0xe2833001],[8,0xeafffffc]],expectReadHits:true,expectRegisters:[[0,255]]},
  {name:'MMU compiled unaligned U1 RAM load',mmu:true,unaligned:true,registers:[[1,0x9001]],data:[[0x9000,0x12345678],[0x9004,0x000000ab]],ops:[[0,0xe5910000],[4,0xe2833001],[8,0xeafffffc]],expectReadHits:true,expectRegisters:[[0,0xab123456]]},
  {name:'MMU legacy unaligned load stays reference',mmu:true,unaligned:false,registers:[[1,0x9001]],data:[[0x9000,0x12345678]],ops:[[0,0xe5910000],[4,0xe2833001],[8,0xeafffffc]],expectReadHits:false,expectRegisters:[[0,0x78123456]]},
  {name:'MMU cross-granule load stays reference',mmu:true,unaligned:true,registers:[[1,0x93fe]],data:[[0x93fc,0x12345678],[0x9400,0xabcdef01]],ops:[[0,0xe5910000],[4,0xe2833001],[8,0xeafffffc]],expectReadHits:false,expectRegisters:[[0,0xef011234]]},
  {name:'MMU nonidentity compiled RAM load',mmu:'alias',registers:[[1,0x800]],data:[[0x10800,0x76543210]],ops:[[0,0xe5910000],[4,0xe2833001],[8,0xeafffffc]],expectReadHits:true,expectRegisters:[[0,0x76543210]]},
  {name:'MMU Thumb compiled word and byte loads',mmu:true,thumb:true,registers:[[1,0x9000]],data:[[0x9000,0x1234fe78]],ops:[[0,0x6808],[2,0x784a],[4,0x3301],[6,0xe7fb]],expectReadHits:true,expectRegisters:[[0,0x1234fe78],[2,254]]},
  {name:'MMU Thumb compiled literal load',mmu:true,thumb:true,data:[[0x24,0x12345678]],ops:[[0,0x4808],[2,0x3101],[4,0xe7fc]],expectReadHits:true,expectRegisters:[[0,0x12345678]]},
  {name:'MMU compiled warm word stores',mmu:true,registers:[[1,0x9000]],ops:[[0,0xe2800001],[4,0xe5810000],[8,0xeafffffc]],expectWriteHits:true,expectMemory:[[0x9000,43]]},
  {name:'MMU compiled warm byte stores',mmu:true,registers:[[1,0x9000],[0,0x100]],ops:[[0,0xe2800001],[4,0xe5c10000],[8,0xeafffffc]],expectWriteHits:true,expectMemory:[[0x9000,43]]},
  {name:'MMU compiled post-index stores',mmu:true,registers:[[1,0x9000]],ops:[[0,0xe2800001],[4,0xe4810004],[8,0xeafffffc]],expectWriteHits:true,expectRegisters:[[1,0x90ac]],expectMemory:[[0x90a8,43]]},
  {name:'MMU compiled translated stores',mmu:true,registers:[[1,0x9000]],ops:[[0,0xe2800001],[4,0xe4a10000],[8,0xeafffffc]],expectWriteHits:true,expectMemory:[[0x9000,43]]},
  {name:'compiled cold RAM store refuses once',mmu:true,registers:[[1,0x9000],[2,0x12345678]],ops:[[0,0xe2800001],[4,0xe3500008],[8,0x1afffffc],[12,0xe5812000],[16,0xeafffffe]],expectWriteHits:false,expectMemory:[[0x9000,0x12345678]]},
  {name:'compiled device store remains reference',registers:[[1,0x3e2000a8]],ops:[[0,0xe2800001],[4,0xe5810000],[8,0xeafffffc]],expectWriteHits:false},
  {name:'compiled store respects revoked direct consent',mmu:true,directWrites:false,warmWrites:[[0x9000,0]],registers:[[1,0x9000]],ops:[[0,0xe2800001],[4,0xe5810000],[8,0xeafffffc]],expectWriteHits:false,expectMemory:[[0x9000,43]]},
  {name:'MMU U1 compiled unaligned store',mmu:true,unaligned:true,registers:[[1,0x9001]],ops:[[0,0xe2800001],[4,0xe5810000],[8,0xeafffffc]],expectWriteHits:true,expectMemory:[[0x9001,43]]},
  {name:'MMU legacy unaligned store stays reference',mmu:true,unaligned:false,registers:[[1,0x9001]],ops:[[0,0xe2800001],[4,0xe5810000],[8,0xeafffffc]],expectWriteHits:false,expectMemory:[[0x9000,43]]},
  {name:'MMU cross-granule store stays reference',mmu:true,unaligned:true,registers:[[1,0x93fe]],ops:[[0,0xe2800001],[4,0xe5810000],[8,0xeafffffc]],expectWriteHits:false,expectMemory:[[0x93fe,43]]},
  {name:'MMU Thumb compiled word and byte stores',mmu:true,thumb:true,registers:[[1,0x9000]],ops:[[0,0x3001],[2,0x6008],[4,0x7048],[6,0xe7fb]],expectWriteHits:true,expectMemory:[[0x9000,0x2020]]},
  {name:'compiled store forces exit before changed successor',mmu:true,track:true,warmWrites:[[16,0xe2833001]],registers:[[1,16],[2,0xe2833007]],ops:[[0,0xe2800001],[4,0xe3500008],[8,0x1afffffc],[12,0xe5812000],[16,0xe2833001],[20,0xeafffffe]],expectWriteHits:true,expectWriteBoundary:true,expectRetired:1,expectVersionOffset:0,expectExactVersion:3,expectRegisters:[[0,8],[3,7]]},
  {name:'compiled store boundary through virtual code alias',mmu:'alias',aliasWrite:true,track:true,warmWrites:[[0x100010,0xe2833001]],registers:[[1,0x100010],[2,0xe2833007]],ops:[[0,0xe2800001],[4,0xe3500008],[8,0x1afffffc],[12,0xe5812000],[16,0xe2833001],[20,0xeafffffe]],expectWriteHits:true,expectWriteBoundary:true,expectRetired:1,expectVersionOffset:0x10000,expectExactVersion:3,expectRegisters:[[0,8],[3,7]]},
  {name:'untracked compiled code store still forces exit',warmWrites:[[16,0xe2833001]],registers:[[1,16],[2,0xe2833007]],ops:[[0,0xe2800001],[4,0xe3500008],[8,0x1afffffc],[12,0xe5812000],[16,0xe2833001],[20,0xeafffffe]],expectWriteHits:true,expectWriteBoundary:true,expectRetired:1,expectRegisters:[[0,8],[3,7]]},
  {name:'tracked cached RAM stores',mmu:true,track:true,registers:[[1,0x9000]],ops:[[0,0xe2800001],[4,0xe5810000],[8,0xeafffffc]],expectVersionOffset:0x9000,expectExactVersion:44},
  {name:'tracked code store through second virtual alias',mmu:'alias',aliasWrite:true,track:true,registers:[[1,0x100000],[2,0xe2800007]],ops:[[0,0xe2800001],[4,0xe3500008],[8,0x1afffffc],[12,0xe5812000],[16,0xeafffffa]],expectRetired:1,expectVersionOffset:0x10000,expectExactVersion:2},
  {name:'tracked guest store changes compiled code',track:true,registers:[[1,0],[2,0xe2800007]],ops:[[0,0xe2800001],[4,0xe3500008],[8,0x1afffffc],[12,0xe5812000],[16,0xeafffffa]],expectRetired:1,expectVersionOffset:0,expectExactVersion:2},
  {name:'stale literal fallback',ops:[[0,0xe2800001],[4,0xeafffffd]],mutation:0xe2800007}
 ]){
  prepare(spec);if(spec.mutation)new DataView(m.jitMemory.buffer).setUint32(m._test_ram(),spec.mutation,true);
  assert.equal(m._test_run(128),128);assert.equal(m._test_status(),0);const expected=snapshot(),expectedTlbHits=m._test_tlb_hits(),expectedDread=[m._test_dread(0),m._test_dread(1)],expectedDwrite=[m._test_dwrite(0),m._test_dwrite(1)],expectedVersion=spec.track?m._test_version(spec.expectVersionOffset):0;
  prepare(spec);await install(spec.ops,spec.mmu==='alias'?0x10000:0,spec.thumb);if(spec.mutation)new DataView(m.jitMemory.buffer).setUint32(m._test_ram(),spec.mutation,true);m._test_enable(1);
  assert.equal(m._test_run(128),128);assert.equal(m._test_status(),0);assert.equal(m._test_failed(),0);assert(guards>0,'Actual generated guard path must execute');const actual=snapshot();assert.equal(m._test_tlb_hits(),expectedTlbHits,'Reference fetch accounting');assert.deepEqual(actual,expected,'Full machine state '+spec.name);assert.deepEqual([m._test_dread(0),m._test_dread(1)],expectedDread,'Read-cache hits/misses counted once');assert.deepEqual([m._test_dwrite(0),m._test_dwrite(1)],expectedDwrite,'Write-cache hits/misses counted once');
  if(spec.expectWriteHits!==undefined){assert(writeCalls>0,'Compiled store helper executes');assert.equal(writeHits>0,spec.expectWriteHits,'Expected store success/refusal');}
  if(spec.expectWriteBoundary){assert.equal(writeBoundaries,1,'Code-writing store requests one immediate boundary');assert.equal(writeHits,1,'No repeated store after code boundary');}
  if(spec.expectReadHits!==undefined){assert(readCalls>0,'Generated memory helper must be called');assert.equal(readHits>0,spec.expectReadHits,'Expected actual warm RAM path or precise refusal');}
  if(spec.name==='MMU false-condition load has no fault')assert.equal(readCalls,0,'False condition never calls read helper');
  if(spec.track){assert.equal(expectedVersion,spec.expectExactVersion,'Every committed CPU store advances its physical version');assert.equal(m._test_version(spec.expectVersionOffset),expectedVersion,'Reference and generated execution notify same writes');}
  if(spec.timer){assert.deepEqual(Array.from({length:6},(_,i)=>m._test_irq(i)),[35,0x5a,0x1d2,0x30000,8,0x1c],'Real timer IRQ and handler effects');assert.equal(guards,69,'Compiled prefix stops at timer deadline');}
  if(spec.expectFault!==undefined){assert.equal(m._test_fault(0),spec.expectFault,'Fault address '+spec.name);if(spec.expectFault){assert.equal(m._test_register(14),72,'Abort LR identifies faulting instruction');assert.equal(m._test_register(15),16,'Data-abort vector');}}
  for(const [reg,value]of spec.expectRegisters||[])assert.equal(m._test_register(reg)>>>0,value,'Register r'+reg+' '+spec.name);
  for(const [address,value]of spec.expectMemory||[])assert.equal(new DataView(m.jitMemory.buffer).getUint32(m._test_ram()+address,true),value,'Partial store retained');
  if(spec.expectRetired)assert(guards>1,'Generated prefix executes before guest code store');
  const retired=drain();if(spec.mutation||spec.expectRetired)assert.equal(retired.length,1,'Stale unit is retired once');
  report.cases.push({name:spec.name,credits:128,guardCalls:guards,readCalls,readHits,writeCalls,writeHits,writeBoundaries,dread:expectedDread,dwrite:expectedDwrite,tlbHits:expectedTlbHits,snapshotBytes:actual.length,sha256:sha(actual),retired:retired.length});
 }
 {
  const spec={ops:[[0,0xe2800001],[4,0xeafffffd]],track:true};
  prepare(spec);const savedState=snapshot();
  const source=m._malloc(savedState.length);new Uint8Array(m.jitMemory.buffer,source,savedState.length).set(savedState);
  assert.equal(m._test_run(128),128);assert.equal(m._test_restore(source,savedState.length),0);assert.equal(m._test_run(128),128);const expected=snapshot();
  prepare(spec);await install(spec.ops);m._test_enable(1);assert.equal(m._test_run(128),128);assert(guards>0);
  const oldEpoch=m._test_epoch();assert.equal(m._test_restore(source,savedState.length),0);assert.notEqual(m._test_epoch(),oldEpoch);assert.equal(m._test_version(0),0,'Restore drops derived versions');
  assert.equal(drain().length,1,'Restore retires actual WASM handle once');const previousGuards=guards;
  assert.equal(m._test_run(128),128);assert.equal(guards,previousGuards,'Restored phone cannot execute old module');assert.deepEqual(snapshot(),expected,'Full machine after restore and continued reference execution');
  const late=m.addFunction(()=>{},'viii');assert.equal(m._test_install(late,0,2,oldEpoch,64),0,'Pre-restore queued result rejected');m.removeFunction(late);m._free(source);
  report.cases.push({name:'actual WASM restore invalidates old module and pending capture',credits:256,guardCalls:guards,sha256:sha(expected)});
 }
 for(const writer of ['host','bus']){
  const spec={ops:[[0,0xe2800001],[4,0xeafffffd]],track:true};
  const change=()=>{const write=writer==='host'?m._test_host_write:m._test_bus_write;write(0,0xe2800007);write(0,0xe2800001);assert.equal(m._test_version(0),3,'ABA writes advance physical version twice');};
  prepare(spec);change();assert.equal(m._test_run(128),128);const expected=snapshot();
  prepare(spec);await install(spec.ops);change();
  const late=m.addFunction(()=>{},'viii');
  assert.equal(m._test_install(late,0,2,m._test_epoch(),64),0,'Stale captured work rejected even after original bytes return');m.removeFunction(late);
  m._test_enable(1);assert.equal(m._test_run(128),128);assert.equal(m._test_status(),0);assert.equal(m._test_failed(),0);
  assert.equal(guards,0,'Changed physical version rejected before generated entry');assert.equal(drain().length,1,'Stale installed handle retired once');
  assert.deepEqual(snapshot(),expected,'Version-only ABA refusal preserves full machine state');
  report.cases.push({name:writer+' writer ABA capture/install/entry rejection',credits:128,guardCalls:guards,version:m._test_version(0),sha256:sha(expected)});
 }
 setup([[0,0xe2800001],[4,0xeafffffd]]);
 const malformed=m.addFunction((cpu,budget,result)=>{new DataView(m.jitMemory.buffer).setUint32(result,budget+1,true);},'viii');
 assert.equal(m._test_install(malformed,0,2,m._test_epoch(),64),1);owned.add(malformed);m._test_enable(1);
 assert(m._test_run(128)<128);assert.equal(m._test_status(),2);assert.equal(m._test_failed(),1);
 const failedState=snapshot();m._test_enable(0);
 assert.equal(m._test_run(1),0,'Fatal latch blocks the single-step bypass');assert.equal(m._test_status(),2);assert.deepEqual(snapshot(),failedState);
 report.cases.push({name:'fatal latch blocks repeated single-step',passed:true});
 m._test_unbind();drain();assert.equal(owned.size,0);report.passed=true;
}catch(e){report.error=e.stack;process.exitCode=1;}
finally{m._free(drainBuffer);report.runtimeSha256=sha(await readFile(modulePath.replace(/\.cjs$/,'.wasm')));await mkdir(root+'/runtime',{recursive:true});await writeFile(root+'/runtime/report.json',JSON.stringify(report,null,2));console.log(JSON.stringify(report));}
