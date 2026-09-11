// Actual WASM versus retained C interpreter oracle. CPU-only emitter boundary;
// machine scheduler, MMU guards and integration are qualified separately.
import {createRequire} from 'node:module';
import {readFile,writeFile,mkdir} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import {resolve} from 'node:path';
import assert from 'node:assert/strict';
import wabtFactory from 'wabt';
import {buildUnit} from './unit.mjs';
import {emitUnit} from './emit.mjs';
const require=createRequire(import.meta.url);
const oraclePath=resolve('docs/traces/connected-compiler-experiment/oracle.cjs');
const m=await require(oraclePath)(),wabt=await wabtFactory();
const layout={registers:m._emu_jit_offset(0),cpsr:m._emu_jit_offset(1)};
const busOffset=m._emu_jit_offset(9),cyclesOffset=busOffset-8;
assert.equal(busOffset,256,'Pinned oracle header layout changed');
const out=resolve(process.env.ARTIFACT_DIR||'.context/adaptive-compiler/emitter');await mkdir(out,{recursive:true});
const hash=b=>createHash('sha256').update(b).digest('hex');
const report={passed:false,scope:'Actual generated WASM versus existing C arm_step: complete pre-cycle CPU header (registers/CPSR/CP15/banked registers), and separately cycle ownership. Guard is a pure identity-RAM test stub, not production MMU qualification. No scheduler or whole-phone performance claim.',oracleSha256:hash(await readFile(oraclePath.replace('.cjs','.wasm'))),checks:0,groups:{}};
assert.equal(m._review_begin(0,0,0,0,0),0);
const result=m._malloc(32);let active,guardCalls=0,refuseAt=null,throwGuard=false;
function state(){const c=m._review_cpu();return Buffer.from(m.HEAPU8.subarray(c,c+cyclesOffset));}
function cycles(){return new DataView(m.jitMemory.buffer).getBigUint64(m._review_cpu()+cyclesOffset,true);}
function reset({flags=0,regs=[],entry=0}={}){
 assert.equal(m._review_begin(0,+active.thumb,flags,entry,0),0);
 const v=new DataView(m.jitMemory.buffer),ram=m._review_ram();
 for(const [pc,raw]of active.ops)v[active.thumb?'setUint16':'setUint32'](ram+pc,raw,true);
 for(const [r,value]of regs)m._review_reg(r,value);
 guardCalls=0;m.HEAPU8.fill(0xa5,result,result+32);
}
async function program(ops,thumb=false){
 const map=new Map();for(const [pc,raw]of ops){const base=(pc&~1023)>>>0;if(!map.has(base))map.set(base,{virtualBase:base,physicalBase:base,version:1,bytes:new Uint8Array(1024)});const r=map.get(base);new DataView(r.bytes.buffer)[thumb?'setUint16':'setUint32'](pc&1023,raw,true);}
 const unit=buildUnit({entryPc:0,thumb,regions:[...map.values()]}),wat=emitUnit(unit,layout),parsed=wabt.parseWat('adaptive-emitter.wat',wat);
 let bytes;try{parsed.validate();bytes=parsed.toBinary({}).buffer;}finally{parsed.destroy();}
 const {instance}=await WebAssembly.instantiate(bytes,{env:{memory:m.jitMemory,guard(pc,raw,width){guardCalls++;if(throwGuard)throw Error('synthetic guard trap');if(pc===refuseAt)return 0;const v=new DataView(m.jitMemory.buffer),addr=m._review_ram()+(pc>>>0);return +(v[width===2?'getUint16':'getUint32'](addr,true)===(raw>>>0));}}});
 active={ops,thumb,unit,run:instance.exports.run};
}
function compare(label,{count=1,retired=count,reason=1,flags=0,regs=[],entry=0,refuse=null}={}){
 reset({flags,regs,entry});assert.equal(m._review_run(retired,0,Math.max(retired,1)),retired);const expected=state(),referenceCycles=cycles();
 reset({flags,regs,entry});refuseAt=refuse;active.run(m._review_cpu(),count,result);refuseAt=null;
 const fields=Array.from(m.HEAPU32.subarray(result>>>2,(result>>>2)+8));
 assert.equal(fields[0],retired,label+' retired');assert.equal(fields[1],reason,label+' exit');assert.equal(fields[2],0,label+' status');assert.equal(fields[3],m.HEAPU32[(m._review_cpu()+60)>>>2],label+' resume PC');assert.equal(fields[7],0,label+' reserved');
 assert.deepEqual(state(),expected,label+' complete CPU header');assert.equal(cycles(),0n,label+' generated must not credit cycles');assert.equal(referenceCycles,BigInt(retired));
 report.checks++;report.groups[label]=(report.groups[label]||0)+1;
}
try{
 // Arithmetic extrema and all NZCV combinations exercise carry, borrow,
 // signed overflow and preservation of unrelated flags for every ALU opcode.
 for(let opcode=0;opcode<16;opcode++)for(const setFlags of [false,true]){
  if(opcode>=8&&opcode<=11&&!setFlags)continue;
  for(const immediate of [0xff,0x102]){
   const raw=(0xe2000000|(opcode<<21)|(+setFlags<<20)|(1<<16)|immediate)>>>0;await program([[0,raw]]);
   for(let nzcv=0;nzcv<16;nzcv++)for(const value of [0,0x7fffffff,0x80000000,0xffffffff])compare('ARM immediate ALU',{flags:((nzcv<<28)|0x080f0000)>>>0,regs:[[1,value]]});
  }
 }
 for(let cond=0;cond<15;cond++){
  await program([[0,((cond<<28)|0x03b00007)>>>0]]);
  for(let flags=0;flags<16;flags++)compare('ARM conditions',{flags:(flags<<28)>>>0});
 }
 for(const raw of [0x20ff,0x28ff,0x30ff,0x38ff]){
  await program([[0,raw]],true);for(let flags=0;flags<16;flags++)for(const value of [0,0x7fffffff,0x80000000,0xffffffff])compare('Thumb immediate ALU',{flags:(flags<<28)>>>0,regs:[[0,value]]});
 }
 await program([[0,0xe28f0001]]);compare('architectural PC operand');
 await program([[0,0xe2800001],[4,0xeafffffd]]);for(const count of [0,1,2,3,31,256]){compare('backward loop budget',{count});if(!count)assert.equal(guardCalls,0);}
 compare('mid-block entry',{entry:4,count:3});
 await program([[0,0xea0000fe],[1024,0xe2800001],[1028,0xeafffefd]]);compare('cross-window loop',{count:17});
 await program([[0,0xeb0000fe]]);compare('external linked edge',{count:2,retired:1,reason:2});
 await program([[0,0xe2800001],[4,0xe5901000]]);compare('unsupported prefix exit',{count:3,retired:1,reason:3});compare('stale first instruction',{count:3,retired:0,reason:4,refuse:0});compare('stale after prefix',{count:3,retired:1,reason:4,refuse:4});
 await program([[0,0xe12fff12]]);compare('BX Thumb boundary',{count:4,retired:1,reason:6,regs:[[2,0x401]]});compare('BX invalid alignment',{count:4,retired:0,reason:3,regs:[[2,0x402]]});
 await program([[0,0x112fff12],[4,0xe3a00003]]);compare('conditional BX false',{count:2,flags:0x40000000,regs:[[2,0x402]]});compare('conditional BX true',{count:4,retired:1,reason:6,regs:[[2,0x401]]});
 reset();throwGuard=true;assert.throws(()=>active.run(m._review_cpu(),1,result),/synthetic guard trap/);throwGuard=false;report.groups['unexpected trap propagates']=1;report.checks++;
 report.passed=true;
}catch(e){report.error=e.stack;report.lastProgram=active?.ops;process.exitCode=1;}
finally{m._free(result);report.sources={};for(const p of ['decode.mjs','unit.mjs','emit.mjs','test-emitter.mjs'])report.sources[p]=hash(await readFile(new URL(p,import.meta.url)));await writeFile(out+'/report.json',JSON.stringify(report,null,2));console.log(JSON.stringify(report));}
