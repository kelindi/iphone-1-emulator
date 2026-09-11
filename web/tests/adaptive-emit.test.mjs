import {test} from 'node:test';
import assert from 'node:assert/strict';
import wabtFactory from 'wabt';
import {buildUnit} from '../../tools/jit/adaptive/unit.mjs';
import {emitUnit} from '../../tools/jit/adaptive/emit.mjs';
const wabt=await wabtFactory();
async function fixture(ops,{thumb=false,guard=()=>1,read=()=>0,write=()=>0}={}){
 const bytes=new Uint8Array(1024).fill(255),source=new DataView(bytes.buffer);
 ops.forEach((raw,i)=>thumb?source.setUint16(i*2,raw,true):source.setUint32(i*4,raw,true));
 const unit=buildUnit({entryPc:0x1000,thumb,regions:[{virtualBase:0x1000,physicalBase:0x2000,version:1,bytes}]});
 const wat=emitUnit(unit,{registers:32,cpsr:96}),parsed=wabt.parseWat('adaptive-test.wat',wat);
 parsed.validate();const binary=parsed.toBinary({}).buffer;parsed.destroy();
 const memory=new WebAssembly.Memory({initial:1}),view=new DataView(memory.buffer),calls=[];
 const cpu=256,result=2048,r=new Uint32Array(memory.buffer,cpu+32,16);
 r.forEach((_,i)=>r[i]=0x12340000+i);r[15]=0x1000;
 view.setUint32(cpu+96,thumb?0x30:0x10,true);
 const {instance}=await WebAssembly.instantiate(binary,{env:{memory,read,write,guard:(pc,raw,width)=>{calls.push([pc>>>0,raw>>>0,width]);return guard(pc>>>0,raw>>>0,width);}}});
 return {unit,wat,r,view,calls,cpu,flags:()=>view.getUint32(cpu+96,true),run(budget){new Uint8Array(memory.buffer,result,32).fill(0xaa);instance.exports.run(cpu,budget,result);return Array.from(new Uint32Array(memory.buffer,result,8));}};
}
test('zero budget neither guards nor changes CPU and overwrites all result words',async()=>{
 const f=await fixture([0xe3a00007]);const before=Array.from(new Uint8Array(f.view.buffer,f.cpu,128));
 assert.deepEqual(f.run(0),[0,1,0,0x1000,0x1000,0xffffffff,0,0]);assert.equal(f.calls.length,0);
 assert.deepEqual(Array.from(new Uint8Array(f.view.buffer,f.cpu,128)),before);
});
test('straight-line tail entry, pure guards, fallback and stale prefix are distinct',async()=>{
 const f=await fixture([0xe3a00007,0xe2801002,0xee000000]);
 let out=f.run(8);assert.deepEqual(out,[2,3,0,0x1008,0x1008,0,0,0]);assert.equal(f.r[0],7);assert.equal(f.r[1],9);
 assert.deepEqual(f.calls.map(x=>x[0]),[0x1000,0x1004,0x1008]);
 const tail=await fixture([0xe3a00007,0xe2801002]);tail.r[15]=0x1004;tail.r[0]=20;
 assert.equal(tail.run(1)[0],1);assert.equal(tail.r[1],22);assert.equal(tail.calls.length,1);
 const stale=await fixture([0xe3a00007,0xe2801002],{guard:pc=>pc===0x1004?0:1});
 assert.deepEqual(stale.run(8),[1,4,0,0x1004,0x1004,0,0,0]);assert.equal(stale.r[0],7);assert.equal(stale.r[1],0x12340001);
});
test('guards run before failed conditions, budget stops before next guard',async()=>{
 const f=await fixture([0x03a00007,0xe3a01009]);assert.equal(f.run(1)[1],1);
 assert.equal(f.r[0],0x12340000);assert.equal(f.r[15],0x1004);assert.equal(f.calls.length,1);
 const stale=await fixture([0x03a00007],{guard:()=>2});assert.equal(stale.run(1)[1],4);assert.equal(stale.r[15],0x1000);
});
test('direct branch/link preserves conditional path and external edge',async()=>{
 const f=await fixture([0xeb0003fe]);assert.deepEqual(f.run(10),[1,2,0,0x2000,0x2000,0xffffffff,0,0]);assert.equal(f.r[14],0x1004);
 const no=await fixture([0x0b0003fe,0xe3a00007]);assert.equal(no.run(2)[0],2);assert.equal(no.r[0],7);assert.equal(no.r[14],0x1234000e);
});
test('BX invalid alignment refuses before effects; interworking commits exactly once',async()=>{
 const bad=await fixture([0xe12fff10]);bad.r[0]=0x2002;const before=Array.from(bad.r);
 assert.deepEqual(bad.run(10),[0,3,0,0x1000,0x1000,0,0,0]);assert.deepEqual(Array.from(bad.r),before);assert.equal(bad.flags(),16);
 const exchange=await fixture([0xe12fff10]);exchange.r[0]=0x2003;
 assert.deepEqual(exchange.run(1),[1,6,0,0x2002,0x1000,0,0,0]);assert.equal(exchange.flags(),0x30);
 const no=await fixture([0x012fff10,0xe3a01009]);no.r[0]=0x2002;assert.equal(no.run(2)[0],2);assert.equal(no.r[1],9);
});
test('literal arithmetic flags and Thumb immediates execute in actual WASM',async()=>{
 const f=await fixture([0xe2901001]);f.r[0]=0x7fffffff;
 assert.equal(f.run(1)[0],1);assert.equal(f.r[1],0x80000000);assert.equal(f.flags(),0x90000010);
 const adc=await fixture([0xe2b01000]);adc.r[0]=0xffffffff;adc.view.setUint32(adc.cpu+96,0x20000010,true);
 adc.run(1);assert.equal(adc.r[1],0);assert.equal(adc.flags(),0x60000010);
 const thumb=await fixture([0x2007,0x3002,0x2809],{thumb:true});thumb.run(3);assert.equal(thumb.r[0],9);assert.equal(thumb.flags(),0x60000030);
});
test('empty captured unit compiles and refuses unknown entry without a guard',async()=>{
 const unit=buildUnit({entryPc:0x1000,regions:[]});
 const parsed=wabt.parseWat('empty.wat',emitUnit(unit,{registers:0,cpsr:64}));parsed.validate();
 const binary=parsed.toBinary({}).buffer;parsed.destroy();
 const memory=new WebAssembly.Memory({initial:1}),words=new Uint32Array(memory.buffer);words[15]=0x1000;words[16]=0x10;
 const before=Array.from(words.slice(0,17));let guards=0;
 const {instance}=await WebAssembly.instantiate(binary,{env:{memory,guard:()=>{guards++;return 1;}}});
 instance.exports.run(0,8,128);
 assert.deepEqual(Array.from(words.slice(32,40)),[0,5,0,0x1000,0x1000,0xffffffff,0,0]);
 assert.deepEqual(Array.from(words.slice(0,17)),before);assert.equal(guards,0);
});
test('large instruction dispatch stays within validator nesting limits',async()=>{
 const f=await fixture(Array.from({length:256},()=>0xe2800001));
 const initial=f.r[0];const result=f.run(256);
 assert.equal(result[0],256);assert.equal(result[1],1);assert.equal(f.r[0],(initial+256)>>>0);
 assert.equal(f.calls.length,256);
});
test('512-instruction units execute correctly across dispatcher groups and tail entries',async()=>{
 const regions=[0x1000,0x1400].map(base=>{const bytes=new Uint8Array(1024),v=new DataView(bytes.buffer);for(let i=0;i<256;i++)v.setUint32(i*4,0xe0800001,true);return {virtualBase:base,physicalBase:base,version:1,bytes};});
 const unit=buildUnit({entryPc:0x1000,regions});assert.equal(unit.instructionCount,512);
 const parsed=wabt.parseWat('full-unit.wat',emitUnit(unit,{registers:0,cpsr:68}));let bytes;try{parsed.validate();bytes=parsed.toBinary({}).buffer;}finally{parsed.destroy();}
 const memory=new WebAssembly.Memory({initial:1}),v=new DataView(memory.buffer);let guards=0;
 const {instance}=await WebAssembly.instantiate(bytes,{env:{memory,guard:()=>{guards++;return 1;}}});
 for(const start of [0,63,64,127,128,511]){
  const regs=new Uint32Array(memory.buffer,128,16);regs.fill(0);regs[1]=1;regs[15]=0x1000+4*start;v.setUint32(128+68,0x1f,true);guards=0;
  instance.exports.run(128,512-start,4096);
  assert.equal(regs[0],512-start);assert.equal(regs[15],0x1800);assert.equal(guards,512-start);
  assert.equal(v.getUint32(4096,true),512-start);assert.equal(v.getUint32(4100,true),1);
 }
});
test('Thumb MUL changes N/Z while preserving C/V and unrelated flags',async()=>{
 const f=await fixture([0x4348],{thumb:true}); // MUL r0,r1
 f.r[0]=0x80000000;f.r[1]=2;f.view.setUint32(f.cpu+96,0xb80f0030,true);
 assert.equal(f.run(1)[0],1);assert.equal(f.r[0],0);assert.equal(f.flags(),0x780f0030);
});
test('Thumb register shift32 is distinct from zero and masks only the low byte',async()=>{
 const f=await fixture([0x4088],{thumb:true}); // LSL r0,r1
 f.r[0]=0x80000001;f.r[1]=32;
 assert.equal(f.run(1)[0],1);assert.equal(f.r[0],0);assert.equal(f.flags(),0x60000030);
 f.r[0]=0x80000001;f.r[1]=256;f.r[15]=0x1000;f.view.setUint32(f.cpu+96,0x10000030,true);
 f.run(1);assert.equal(f.r[0],0x80000001);assert.equal(f.flags(),0x90000030);
});
test('Thumb high-register PC reads use pc+4 and PC writes still refuse',async()=>{
 const f=await fixture([0x4678],{thumb:true}); // MOV r0,pc
 f.run(1);assert.equal(f.r[0],0x1004);assert.equal(f.r[15],0x1002);
 const cmp=await fixture([0x45ff],{thumb:true}); // CMP pc,pc
 cmp.run(1);assert.equal(cmp.flags(),0x60000030);assert.equal(cmp.r[15],0x1002);
 const no=await fixture([0x4687],{thumb:true}); // MOV pc,r0
 assert.equal(no.run(1)[1],3);assert.equal(no.r[15],0x1000);
});
test('RAM loads use returned host pointer and commit post-index writeback only on success',async()=>{
 const calls=[];const f=await fixture([0xe4b10004,0xe2802001],{read:(va,width,priv)=>{calls.push([va>>>0,width,priv]);return 0x9000;}});
 f.r[1]=0x4000;f.view.setUint32(0x9000,0xfedcba98,true);f.view.setUint32(f.cpu+96,0xf000001f,true);
 assert.equal(f.unit.blocks.length,1);assert.equal(f.run(2)[0],2);
 assert.deepEqual(calls,[[0x4000,4,0]]);assert.equal(f.r[0],0xfedcba98);assert.equal(f.r[1],0x4004);assert.equal(f.r[2],0xfedcba99);assert.equal(f.flags(),0xf000001f);
 const no=await fixture([0xe5b10004]);const before=Array.from(no.r);assert.equal(no.run(2)[1],3);assert.deepEqual(Array.from(no.r),before);
});
test('load helper skips false conditions, budget exhaustion and failed guards',async()=>{
 for(const spec of [{raw:0x05910000,budget:1,guard:()=>1},{raw:0xe5910000,budget:0,guard:()=>1},{raw:0xe5910000,budget:1,guard:()=>0}]){
  let reads=0;const f=await fixture([spec.raw],{guard:spec.guard,read:()=>{reads++;return 0x9000;}});f.run(spec.budget);assert.equal(reads,0);
 }
});
test('ordinary load privilege follows mode and address arithmetic wraps at32bits',async()=>{
 for(const mode of [0x10,0x1f]){const calls=[];const f=await fixture([0xe5910004],{read:(...args)=>{calls.push(args.map(x=>x>>>0));return 0x9000;}});
 f.r[1]=0xfffffffc;f.view.setUint32(f.cpu+96,mode,true);f.view.setUint32(0x9000,7,true);f.run(1);assert.deepEqual(calls,[[0,4,mode===0x10?0:1]]);assert.equal(f.r[0],7);}
});
test('Thumb byte loads zero-extend and literal address aligns instructionPCplus4',async()=>{
 const calls=[];const f=await fixture([0x2000,0x4801,0x780a],{thumb:true,read:(va,width,priv)=>{calls.push([va>>>0,width,priv]);return 0x9000;}});
 f.r[1]=0x8123;f.view.setUint32(0x9000,0xffeecc80,true);assert.equal(f.run(3)[0],3);
 assert.deepEqual(calls,[[0x1008,4,0],[0x8123,1,0]]);assert.equal(f.r[0],0xffeecc80);assert.equal(f.r[2],0x80);
});
test('read import is absent from arithmetic units and present in load units',async()=>{
 const a=await fixture([0xe2800001]),b=await fixture([0xe5900000]);
 assert(!a.wat.includes('(import "env" "read"'));assert(b.wat.includes('(import "env" "read"'));
});
test('committed watched store exits after retirement/writeback before any following guard',async()=>{
 const calls=[];let f;f=await fixture([0xe2800001,0xe4a10004,0xe2822001],{write:(va,width,priv,value)=>{calls.push([va>>>0,width,priv,value>>>0]);f.view.setUint32(0x9000,value,true);return 2;}});
 f.r[1]=0x8000;const beforeR2=f.r[2],flags=f.flags();const out=f.run(3);
 assert.deepEqual(out,[2,6,0,0x1008,0x1004,0,0,0]);assert.deepEqual(calls,[[0x8000,4,0,0x12340001]]);
 assert.equal(f.r[1],0x8004);assert.equal(f.r[2],beforeR2);assert.equal(f.flags(),flags);assert.equal(f.view.getUint32(0x9000,true),0x12340001);
 assert.deepEqual(f.calls.map(c=>c[0]),[0x1000,0x1004]);
});
test('store refusal changes neither registers/writeback nor RAM',async()=>{
 const f=await fixture([0xe5a10004],{write:()=>0});const before=Array.from(f.r);f.view.setUint32(0x9000,0xfeedabcd,true);
 assert.deepEqual(f.run(3),[0,3,0,0x1000,0x1000,0,0,0]);assert.deepEqual(Array.from(f.r),before);assert.equal(f.view.getUint32(0x9000,true),0xfeedabcd);
});
test('unknown store result is INVALID even if the helper committed memory',async()=>{
 for(const status of [3,-1]){let f;f=await fixture([0xe4a10004,0xe2822001],{write:()=>{f.view.setUint32(0x9000,0x12345678,true);return status;}});
 const before=Array.from(f.r);const out=f.run(3);assert.equal(out[1],0);assert.equal(out[0],0);assert.deepEqual(Array.from(f.r),before);assert.equal(f.calls.length,1);assert.equal(f.view.getUint32(0x9000,true),0x12345678);}
});
test('store helper skips false conditions, budget exhaustion and failed guards',async()=>{
 for(const spec of [{raw:0x05810000,budget:1,guard:()=>1},{raw:0xe5810000,budget:0,guard:()=>1},{raw:0xe5810000,budget:1,guard:()=>0}]){
 let writes=0;const f=await fixture([spec.raw],{guard:spec.guard,write:()=>{writes++;return 2;}});f.run(spec.budget);assert.equal(writes,0);}
});
test('ordinary store followed by false store does not inherit helper status',async()=>{
 let writes=0;const f=await fixture([0xe5810000,0x05810004,0xe2822001],{write:()=>{writes++;return 1;}});
 const r2=f.r[2];assert.equal(f.run(3)[1],1);assert.equal(writes,1);assert.equal(f.r[2],r2+1);
});
test('word STR pc uses pc+12 and optional write import is isolated',async()=>{
 const calls=[];const f=await fixture([0xe58ff000],{write:(...args)=>{calls.push(args.map(n=>n>>>0));return 1;}});
 f.run(1);assert.deepEqual(calls,[[0x1008,4,0,0x100c]]);
 const arithmetic=await fixture([0xe2800001]);assert(!arithmetic.wat.includes('(import "env" "write"'));assert(f.wat.includes('(import "env" "write"'));
});
