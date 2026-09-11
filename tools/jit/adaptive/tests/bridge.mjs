import {createRequire} from 'node:module';
import {resolve} from 'node:path';
import assert from 'node:assert/strict';
import wabtFactory from 'wabt';
import {buildUnit} from '../unit.mjs';
import {emitUnit} from '../emit.mjs';
const require=createRequire(import.meta.url),m=await require(resolve(process.env.BRIDGE_MODULE||'.context/adaptive-compiler/bridge.cjs'))(),wabt=await wabtFactory();
m._bridge_reset();
const bytes=new Uint8Array(1024),view=new DataView(bytes.buffer);
[0xe3a00001,0xe2800001,0xeafffffc].forEach((raw,i)=>view.setUint32(i*4,raw,true));
const unit=buildUnit({entryPc:0,regions:[{virtualBase:0,physicalBase:0,version:1,bytes}]}),wat=emitUnit(unit,{registers:m._bridge_offset(0),cpsr:m._bridge_offset(1)});
const parsed=wabt.parseWat('bridge-test.wat',wat);let binary;try{parsed.validate();binary=parsed.toBinary({}).buffer;}finally{parsed.destroy();}
let guards=0,refuse=-1;
const {instance}=await WebAssembly.instantiate(binary,{env:{memory:m.jitMemory,guard(pc){guards++;return +(pc!==refuse);}}});
const id=m.addFunction(instance.exports.run,'viii'),memory=new DataView(m.jitMemory.buffer);
const cycles=()=>memory.getBigUint64(m._bridge_cpu()+m._bridge_offset(2),true);
const result=()=>Array.from(new Uint32Array(m.jitMemory.buffer,m._bridge_result(),8));
try{
 assert.equal(m._bridge_invoke(id,0,1),0);assert.equal(guards,0);assert.equal(cycles(),0n);
 assert.equal(m._bridge_invoke(id,3,1),1);assert.equal(cycles(),3n);assert.equal(memory.getUint32(m._bridge_cpu(),true),2);assert.equal(result()[0],3);assert.equal(result()[1],1);
 m._bridge_reset();refuse=4;
 assert.equal(m._bridge_invoke(id,3,1),1);assert.equal(cycles(),1n);assert.equal(memory.getUint32(m._bridge_cpu(),true),1);assert.equal(result()[0],1);assert.equal(result()[1],4);assert.equal(result()[3],4);
 const malformed=m.addFunction((cpu,budget,out)=>{memory.setUint32(out,budget+1,true);memory.setUint32(out+4,1,true);},'viii');
 try{m._bridge_reset();assert.equal(m._bridge_invoke(malformed,3,1),-1);assert.equal(cycles(),0n);}finally{m.removeFunction(malformed);}
 console.log('PASS: actual WASM through C ABI2 bridge: zero budget, exact cycle credit, stale prefix, malformed result rejection');
}finally{m.removeFunction(id);}
