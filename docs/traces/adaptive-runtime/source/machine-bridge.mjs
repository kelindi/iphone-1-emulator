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
const report={passed:false,scope:'Actual emitted WASM, guarded C registry and real machine scheduler; full snapshots on synthetic integer loops. Not real-phone workloads, MMU-on execution or performance acceptance.',cases:[]};
let guards=0;
function drain(){const n=m._test_drain(drainBuffer,2048),ids=Array.from(new Uint32Array(m.jitMemory.buffer,drainBuffer,n));for(const id of ids){assert(owned.delete(id),'Retired handle must have exactly one owner');m.removeFunction(id);}return ids;}
function setup(ops){new Uint32Array(m.jitMemory.buffer,m._test_program(),512).fill(0);const words=new Uint32Array(m.jitMemory.buffer,m._test_program(),512);for(const [pc,raw]of ops)words[pc/4]=raw;assert.equal(m._test_begin(512),1);drain();assert.equal(m._test_warm(0,0),1);assert.equal(m._test_warm(1024,1),1);guards=0;}
async function install(ops){const regions=[0,1024].map(base=>{const bytes=new Uint8Array(1024),v=new DataView(bytes.buffer);for(const [pc,raw]of ops)if((pc&~1023)===base)v.setUint32(pc-base,raw,true);return {virtualBase:base,physicalBase:base,version:1,bytes};});
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
  {name:'stale literal fallback',ops:[[0,0xe2800001],[4,0xeafffffd]],mutation:0xe2800007}
 ]){
  setup(spec.ops);if(spec.mutation)new DataView(m.jitMemory.buffer).setUint32(m._test_ram(),spec.mutation,true);
  assert.equal(m._test_run(128),128);assert.equal(m._test_status(),0);const expected=snapshot();
  setup(spec.ops);await install(spec.ops);if(spec.mutation)new DataView(m.jitMemory.buffer).setUint32(m._test_ram(),spec.mutation,true);m._test_enable(1);
  assert.equal(m._test_run(128),128);assert.equal(m._test_status(),0);assert.equal(m._test_failed(),0);assert(guards>0,'Actual generated guard path must execute');const actual=snapshot();assert.deepEqual(actual,expected,'Full machine state '+spec.name);
  const retired=drain();if(spec.mutation)assert.equal(retired.length,1,'Stale unit is retired once');
  report.cases.push({name:spec.name,credits:128,guardCalls:guards,snapshotBytes:actual.length,sha256:sha(actual),retired:retired.length});
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
