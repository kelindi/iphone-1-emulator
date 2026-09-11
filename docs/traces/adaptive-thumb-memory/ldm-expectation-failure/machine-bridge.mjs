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
let guards=0;
function drain(){const n=m._test_drain(drainBuffer,2048),ids=Array.from(new Uint32Array(m.jitMemory.buffer,drainBuffer,n));for(const id of ids){assert(owned.delete(id),'Retired handle must have exactly one owner');m.removeFunction(id);}return ids;}
function setup(ops,mmu=false,timer=false){new Uint32Array(m.jitMemory.buffer,m._test_program(),512).fill(0);const words=new Uint32Array(m.jitMemory.buffer,m._test_program(),512);for(const [pc,raw]of ops)words[pc/4]=raw;assert.equal(m._test_begin(512),1);drain();if(mmu)m._test_mmu(mmu==='alias'?1:0);if(timer)m._test_timer();assert.equal(m._test_warm(0,0),1);assert.equal(m._test_warm(1024,1),1);guards=0;}
function prepare(spec){
 setup(spec.ops,spec.mmu,spec.timer);
 const view=new DataView(m.jitMemory.buffer);
 for(const [reg,value]of spec.registers||[])view.setUint32(m._test_cpu()+m._test_offset(0)+reg*4,value,true);
 for(const [address,value]of spec.data||[])view.setUint32(m._test_ram()+address,value,true);
 if(spec.alignment){m._test_alignment();assert.equal(m._test_warm(0,0),1);assert.equal(m._test_warm(1024,1),1);}
}
async function install(ops,physicalOffset=0){const regions=[0,1024].map(base=>{const bytes=new Uint8Array(1024),v=new DataView(bytes.buffer);for(const [pc,raw]of ops)if((pc&~1023)===base)v.setUint32(pc-base,raw,true);return {virtualBase:base,physicalBase:base+physicalOffset,version:1,bytes};});
 const unit=buildUnit({entryPc:0,regions}),parsed=wabt.parseWat('machine-unit.wat',emitUnit(unit,{registers:m._test_offset(0),cpsr:m._test_offset(1)}));let binary;try{parsed.validate();binary=parsed.toBinary({}).buffer;}finally{parsed.destroy();}
 const {instance}=await WebAssembly.instantiate(binary,{env:{memory:m.jitMemory,guard(pc,raw,width){guards++;return m._test_guard(pc,raw,width);}}});
 const id=m.addFunction(instance.exports.run,'viii');assert.equal(m._test_install(id,0,2,m._test_epoch(),binary.length),1);owned.add(id);
 assert.equal(m._test_install(id,0,2,m._test_epoch(),binary.length),0,'Duplicate ownership rejected');return id;
}
function snapshot(){const p=m._test_snapshot(),n=m._test_snapshot_size();assert(p&&n);return Buffer.from(new Uint8Array(m.jitMemory.buffer,p,n));}
try{
 for(const spec of [
  {name:'connected loop',ops:[[0,0xe2800001],[4,0xeafffffd]]},
  {name:'cross-window loop',ops:[[0,0xea0000fe],[1024,0xe2800001],[1028,0xeafffefd]]},
  {name:'MMU register-shift ALU loop',mmu:true,ops:[[0,0xe3a00001],[4,0xe3a01021],[8,0xe1b00170],[12,0xe08220a0],[16,0xeafffffa]]},
  {name:'MMU-on connected loop',mmu:true,ops:[[0,0xe2800001],[4,0xeafffffd]]},
  {name:'MMU-on cross-window loop',mmu:true,ops:[[0,0xea0000fe],[1024,0xe2800001],[1028,0xeafffefd]]},
  {name:'MMU guarded slow instruction',mmu:true,ops:[[0,0xea0000fe],[1024,0xe5910000],[1028,0xeafffefd]]},
  {name:'MMU nonidentity small-page mapping',mmu:'alias',ops:[[0,0xea0000fe],[1024,0xe2800001],[1028,0xeafffefd]]},
  {name:'timer IRQ after compiled prefix',timer:true,ops:[[0,0xe2800001],[4,0xeafffffd],[0x18,0xe3a0205a],[0x1c,0xeafffffe]]},
  {name:'MMU load/store writeback handoff',mmu:true,registers:[[1,0x9000]],ops:[[0,0xe2800001],[4,0xe5a10004],[8,0xe4912004],[12,0xeafffffb]]},
  {name:'MMU false-condition load has no fault',mmu:true,registers:[[1,0x200000]],ops:[[0,0xe2800001],[4,0x05912000],[8,0xeafffffc]],expectFault:0},
  {name:'MMU load translation abort after prefix',mmu:true,registers:[[1,0x200000],[2,0x12345678]],ops:[[0,0xe2800001],[4,0xe3500008],[8,0x1afffffc],[12,0xea00000b],[16,0xeafffffe],[64,0xe5b12004],[68,0xeafffffe]],expectFault:0x200004,expectRegisters:[[0,8],[1,0x200000],[2,0x12345678]]},
  {name:'MMU alignment abort after prefix',mmu:true,alignment:true,registers:[[1,0x9001],[2,0x12345678]],ops:[[0,0xe2800001],[4,0xe3500008],[8,0x1afffffc],[12,0xea00000b],[16,0xeafffffe],[64,0xe5912000],[68,0xeafffffe]],expectFault:0x9001,expectRegisters:[[0,8],[1,0x9001],[2,0x12345678]]},
  {name:'MMU partial LDM abort preserves first load',mmu:true,registers:[[1,0xffffc],[2,0x22222222],[3,0x33333333]],data:[[0xffffc,0x12345678]],ops:[[0,0xe2800001],[4,0xe3500008],[8,0x1afffffc],[12,0xea00000b],[16,0xeafffffe],[64,0xe8b1000c],[68,0xeafffffe]],expectFault:0x100000,expectRegisters:[[0,8],[1,0xffffc],[2,0x12345678],[3,0x33333333]]},
  {name:'MMU partial STM abort preserves first store',mmu:true,registers:[[1,0xffffc],[2,0x12345678],[3,0x33333333]],ops:[[0,0xe2800001],[4,0xe3500008],[8,0x1afffffc],[12,0xea00000b],[16,0xeafffffe],[64,0xe8a1000c],[68,0xeafffffe]],expectFault:0x100000,expectRegisters:[[0,8],[1,0xffffc]],expectMemory:[[0xffffc,0x12345678]]},
  {name:'guest store changes compiled instruction',registers:[[1,0],[2,0xe2800007]],ops:[[0,0xe2800001],[4,0xe5812000],[8,0xeafffffc]],expectRetired:1},
  {name:'MMIO timer register store/load handoff',registers:[[1,0x3e2000a8],[2,7]],ops:[[0,0xe2800001],[4,0xe5812000],[8,0xe5913000],[12,0xeafffffb]],expectRegisters:[[3,7]]},
  {name:'stale literal fallback',ops:[[0,0xe2800001],[4,0xeafffffd]],mutation:0xe2800007}
 ]){
  prepare(spec);if(spec.mutation)new DataView(m.jitMemory.buffer).setUint32(m._test_ram(),spec.mutation,true);
  assert.equal(m._test_run(128),128);assert.equal(m._test_status(),0);const expected=snapshot(),expectedTlbHits=m._test_tlb_hits();
  prepare(spec);await install(spec.ops,spec.mmu==='alias'?0x10000:0);if(spec.mutation)new DataView(m.jitMemory.buffer).setUint32(m._test_ram(),spec.mutation,true);m._test_enable(1);
  assert.equal(m._test_run(128),128);assert.equal(m._test_status(),0);assert.equal(m._test_failed(),0);assert(guards>0,'Actual generated guard path must execute');const actual=snapshot();assert.equal(m._test_tlb_hits(),expectedTlbHits,'Reference fetch accounting');assert.deepEqual(actual,expected,'Full machine state '+spec.name);
  if(spec.timer){assert.deepEqual(Array.from({length:6},(_,i)=>m._test_irq(i)),[35,0x5a,0x1d2,0x30000,8,0x1c],'Real timer IRQ and handler effects');assert.equal(guards,69,'Compiled prefix stops at timer deadline');}
  if(spec.expectFault!==undefined)assert.equal(m._test_fault(0),spec.expectFault,'Fault address '+spec.name);
  for(const [reg,value]of spec.expectRegisters||[])assert.equal(m._test_register(reg),value,'Register r'+reg+' '+spec.name);
  for(const [address,value]of spec.expectMemory||[])assert.equal(new DataView(m.jitMemory.buffer).getUint32(m._test_ram()+address,true),value,'Partial store retained');
  const retired=drain();if(spec.mutation||spec.expectRetired)assert.equal(retired.length,1,'Stale unit is retired once');
  report.cases.push({name:spec.name,credits:128,guardCalls:guards,tlbHits:expectedTlbHits,snapshotBytes:actual.length,sha256:sha(actual),retired:retired.length});
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
