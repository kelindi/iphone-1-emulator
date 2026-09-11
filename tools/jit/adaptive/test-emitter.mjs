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
const report={passed:false,scope:'Actual generated WASM versus existing C arm_step: complete pre-cycle CPU header (registers/CPSR/CP15/banked registers), and separately cycle ownership. Guard/read/write use identity-RAM test stubs, with full RAM equality for store-enabled cases; not production MMU/cache qualification. No scheduler or whole-phone performance claim.',oracleSha256:hash(await readFile(oraclePath.replace('.cjs','.wasm'))),checks:0,groups:{}};
assert.equal(m._review_begin(0,0,0,0,0),0);
const result=m._malloc(32);let active,guardCalls=0,refuseAt=null,throwGuard=false,readEnabled=false,readRefuse=null,readHits=0,readCalls=[],writeEnabled=false,writeOutcome=1,writeRefuse=null,writeHits=0,writeCalls=[];
function state(){const c=m._review_cpu();return Buffer.from(m.HEAPU8.subarray(c,c+cyclesOffset));}
function cycles(){return new DataView(m.jitMemory.buffer).getBigUint64(m._review_cpu()+cyclesOffset,true);}
function reset({flags=0,regs=[],entry=0,memory=[]}={}){
 assert.equal(m._review_begin(0,+active.thumb,flags,entry,0),0);
 const v=new DataView(m.jitMemory.buffer),ram=m._review_ram();
 for(const region of active.regions)m.HEAPU8.set(region.bytes,ram+region.virtualBase);
 for(const [r,value]of regs)m._review_reg(r,value);
 for(const [address,bytes]of memory)m.HEAPU8.set(bytes,ram+address);
 guardCalls=0;readHits=0;readCalls=[];writeHits=0;writeCalls=[];m.HEAPU8.fill(0xa5,result,result+32);
}
async function program(ops,thumb=false){
 const map=new Map();for(const [pc,raw]of ops){const base=(pc&~1023)>>>0;if(!map.has(base))map.set(base,{virtualBase:base,physicalBase:base,version:1,bytes:new Uint8Array(1024).fill(255)});const r=map.get(base);new DataView(r.bytes.buffer)[thumb?'setUint16':'setUint32'](pc&1023,raw,true);}
 const unit=buildUnit({entryPc:0,thumb,regions:[...map.values()]}),wat=emitUnit(unit,layout),parsed=wabt.parseWat('adaptive-emitter.wat',wat);
 let bytes;try{parsed.validate();bytes=parsed.toBinary({}).buffer;}finally{parsed.destroy();}
 const {instance}=await WebAssembly.instantiate(bytes,{env:{memory:m.jitMemory,write(va,width,priv,value){va>>>=0;writeCalls.push([va,width,priv,value>>>0]);if(!writeEnabled||va===writeRefuse||![1,4].includes(width)||va%width||va+width>(1<<20))return 0;const view=new DataView(m.jitMemory.buffer);view[width===1?'setUint8':'setUint32'](m._review_ram()+va,value,true);writeHits++;return writeOutcome;},read(va,width,priv){va>>>=0;readCalls.push([va,width,priv]);if(!readEnabled||va===readRefuse||![1,4].includes(width)||va%width||va+width>(1<<20))return 0;readHits++;return m._review_ram()+va;},guard(pc,raw,width){guardCalls++;if(throwGuard)throw Error('synthetic guard trap');if(pc===refuseAt)return 0;const v=new DataView(m.jitMemory.buffer),addr=m._review_ram()+(pc>>>0);return +(v[width===2?'getUint16':'getUint32'](addr,true)===(raw>>>0));}}});
 active={ops,thumb,unit,regions:[...map.values()],run:instance.exports.run};
}
function compare(label,{count=1,retired=count,reason=1,flags=0,regs=[],entry=0,refuse=null,memory=[],reads=false,refuseRead=null,expectedHits,expectedCalls,writes=false,writeResult=1,refuseWrite=null,expectedWrites,expectedWriteCalls}={}){
 reset({flags,regs,entry,memory});assert.equal(m._review_run(retired,0,Math.max(retired,1)),retired);const expected=state(),referenceCycles=cycles(),referenceRam=writes?Buffer.from(m.HEAPU8.subarray(m._review_ram(),m._review_ram()+(1<<20))):null;
 reset({flags,regs,entry,memory});refuseAt=refuse;readEnabled=reads;readRefuse=refuseRead;writeEnabled=writes;writeOutcome=writeResult;writeRefuse=refuseWrite;active.run(m._review_cpu(),count,result);refuseAt=null;
 const fields=Array.from(m.HEAPU32.subarray(result>>>2,(result>>>2)+8));
 assert.equal(fields[0],retired,label+' retired');assert.equal(fields[1],reason,label+' exit');assert.equal(fields[2],0,label+' status');assert.equal(fields[3],m.HEAPU32[(m._review_cpu()+60)>>>2],label+' resume PC');assert.equal(fields[7],0,label+' reserved');
 assert.deepEqual(state(),expected,label+' complete CPU header');assert.equal(cycles(),0n,label+' generated must not credit cycles');assert.equal(referenceCycles,BigInt(retired));
 if(expectedHits!==undefined)assert.equal(readHits,expectedHits,label+' successful helper hits');
 if(expectedCalls!==undefined)assert.equal(readCalls.length,expectedCalls,label+' helper calls');
 if(expectedWrites!==undefined)assert.equal(writeHits,expectedWrites,label+' committed writes');
 if(expectedWriteCalls!==undefined)assert.equal(writeCalls.length,expectedWriteCalls,label+' write helper calls');
 if(referenceRam)assert.deepEqual(Buffer.from(m.HEAPU8.subarray(m._review_ram(),m._review_ram()+(1<<20))),referenceRam,label+' complete RAM');
 else for(const [address,bytes]of memory)assert.deepEqual(Buffer.from(m.HEAPU8.subarray(m._review_ram()+address,m._review_ram()+address+bytes.length)),Buffer.from(bytes),label+' load preserved RAM');
 report.checks++;report.groups[label]=(report.groups[label]||0)+1;
}
try{
 // Store helper stub commits real identity RAM. Every store-enabled case
 // compares the full1MiB RAM image against actual interpreter execution.
 const storeBytes=Uint8Array.from([1,2,3,4,5,6,7,8]);
 for(const byte of [false,true])for(const pre of [false,true])for(const up of [false,true])for(const wb of [false,true])for(const offset of [0,4,4095]){
  const raw=(0xe4000000|(+pre<<24)|(+up<<23)|(+byte<<22)|(+wb<<21)|(1<<16)|offset)>>>0;
  await program([[0,raw]]);const address=0x80000,base=pre?(address+(up?-offset:offset))>>>0:address;
  for(const value of [0,0xfedcba98])compare('ARM immediate RAM store addressing',{regs:[[0,value],[1,base]],memory:[[address,storeBytes]],writes:true,expectedWrites:1,expectedWriteCalls:1});
  compare('ARM refused store preserves CPU and RAM',{retired:0,reason:3,regs:[[0,0xffffffff],[1,base]],memory:[[address,storeBytes]],writes:true,refuseWrite:address,expectedWrites:0,expectedWriteCalls:1});
 }
 for(let cond=0;cond<15;cond++){
  await program([[0,((cond<<28)|0x05810000)>>>0]]);
  for(let nzcv=0;nzcv<16;nzcv++)compare('ARM store conditions',{flags:(nzcv<<28)>>>0,regs:[[0,0xfedcba98],[1,0x80000]],memory:[[0x80000,storeBytes]],writes:true});
 }
 for(const raw of [0xe581f000,0xe58f0020,0xe5800000]){
  await program([[0,raw]]);compare('ARM store PCsource/base and no-writeback alias',{regs:[[0,0x80000],[1,0x80000]],writes:true,expectedWrites:1});
 }
 for(const raw of [0xe5c1f000,0xe5a00000,0xe4800000,0xe48f0000,0xe7810002,0xe8810001]){
  await program([[0,raw]]);compare('ARM excluded store forms',{retired:0,reason:3,writes:true,expectedWrites:0,expectedWriteCalls:0});
 }
 for(const byte of [false,true])for(const offset of [0,1,31])for(const alias of [false,true]){
  const rd=alias?1:0;await program([[0,(byte?0x7000:0x6000)|(offset<<6)|(1<<3)|rd]],true);
  compare('Thumb immediate RAM store',{regs:[[0,0xfedcba98],[1,0x80000-offset*(byte?1:4)]],memory:[[0x80000,storeBytes]],writes:true,expectedWrites:1});
 }
 await program([[0,0xe2800001],[4,0xe4a10004],[8,0xe2822001]]);
 compare('watched store boundary retires prefix and writeback',{count:3,retired:2,reason:6,regs:[[1,0x80000]],writes:true,writeResult:2,expectedWrites:1});
 compare('watched store boundary wins exact budget',{count:2,retired:2,reason:6,regs:[[1,0x80000]],writes:true,writeResult:2,expectedWrites:1});
 compare('ordinary store continues sequential arithmetic',{count:3,regs:[[1,0x80000]],writes:true,expectedWrites:1});
 compare('store budget suppresses helper',{count:1,writes:true,expectedWrites:0,expectedWriteCalls:0});
 compare('store stale guard suppresses helper',{count:3,retired:1,reason:4,refuse:4,writes:true,expectedWrites:0,expectedWriteCalls:0});
 compare('store refusal after prefix',{count:3,retired:1,reason:3,regs:[[1,0x80000]],writes:true,refuseWrite:0x80000,expectedWrites:0,expectedWriteCalls:1});
 await program([[0,0xe5810000],[4,0xe5912000],[8,0xe2822001]]);
 compare('store then load sees committed RAM',{count:3,regs:[[0,0x12345678],[1,0x80000]],writes:true,reads:true,expectedWrites:1,expectedHits:1});
 // Successful loads use a pure identity-RAM pointer stub and an explicit
 // per-success hit counter; production MMU/cache semantics are tested separately.
 const payload=Uint8Array.from([0x80,0xff,0x34,0x92,0x01,0x23,0x45,0x67]);
 for(const byte of [false,true])for(const pre of [false,true])for(const up of [false,true])for(const wb of [false,true])for(const offset of [0,4,4095]){
  const raw=(0xe4100000|(+pre<<24)|(+up<<23)|(+byte<<22)|(+wb<<21)|(1<<16)|offset)>>>0;
  await program([[0,raw]]);
  const address=0x80000,base=pre?(address+(up?-offset:offset))>>>0:address;
  for(const flags of [0,0xf0000000])compare('ARM immediate RAM load addressing',{flags,regs:[[1,base]],memory:[[address,payload]],reads:true,expectedHits:1,expectedCalls:1});
  compare('ARM load pointer refusal preserves base/destination',{retired:0,reason:3,regs:[[1,base]],memory:[[address,payload]],expectedHits:0,expectedCalls:1});
 }
 for(const raw of [0xe4900000,0xe4d00000,0xe5900000]){
  await program([[0,raw]]);compare('ARM load destination/base legal aliases',{regs:[[0,0x80000]],memory:[[0x80000,payload]],reads:true,expectedHits:1});
 }
 for(const raw of [0xe5b00000,0xe4900004,0xe4b00000,0xe59ff000,0xe49f0000,0xe7910002,0xe5810000]){
  await program([[0,raw]]);compare('ARM excluded memory forms',{retired:0,reason:3,reads:true,expectedHits:0,expectedCalls:0});
 }
 await program([[0,0xe59f0020]]);compare('ARM literal PC plus8',{memory:[[40,payload]],reads:true,expectedHits:1});
 for(let cond=0;cond<15;cond++){
  await program([[0,((cond<<28)|0x05910000)>>>0]]);
  for(let nzcv=0;nzcv<16;nzcv++)compare('ARM load conditions',{flags:(nzcv<<28)>>>0,regs:[[1,0x80000]],memory:[[0x80000,payload]],reads:true});
 }
 for(const byte of [false,true])for(const offset of [0,1,31])for(const alias of [false,true]){
  const rd=alias?1:0;await program([[0,(byte?0x7800:0x6800)|(offset<<6)|(1<<3)|rd]],true);
  compare('Thumb immediate RAM load',{regs:[[1,0x80000-offset*(byte?1:4)]],memory:[[0x80000,payload]],reads:true,expectedHits:1});
 }
 await program([[0,0x2000],[2,0x4801]],true);
 compare('Thumb aligned literal PC plus4',{count:2,memory:[[8,payload]],reads:true,expectedHits:1});
 await program([[0,0xe2800001],[4,0xe5912000],[8,0xe2823001]]);
 compare('RAM load mixed straight-line execution',{count:3,regs:[[1,0x80000]],memory:[[0x80000,payload]],reads:true,expectedHits:1});
 compare('RAM load budget excludes helper',{count:1,reads:true,expectedHits:0,expectedCalls:0});
 compare('RAM load stale excludes helper',{count:3,retired:1,reason:4,refuse:4,reads:true,expectedHits:0,expectedCalls:0});
 compare('RAM load refused after prefix',{count:3,retired:1,reason:3,regs:[[1,0x80000]],memory:[[0x80000,payload]],reads:true,refuseRead:0x80000,expectedHits:0,expectedCalls:1});
 // Arithmetic extrema and all NZCV combinations exercise carry, borrow,
 // signed overflow and preservation of unrelated flags for every ALU opcode.
 for(let opcode=0;opcode<16;opcode++)for(const setFlags of [false,true]){
  if(opcode>=8&&opcode<=11&&!setFlags)continue;
  for(const immediate of [0xff,0x102]){
   const raw=(0xe2000000|(opcode<<21)|(+setFlags<<20)|(1<<16)|immediate)>>>0;await program([[0,raw]]);
   for(let nzcv=0;nzcv<16;nzcv++)for(const value of [0,0x7fffffff,0x80000000,0xffffffff])compare('ARM immediate ALU',{flags:((nzcv<<28)|0x080f0000)>>>0,regs:[[1,value]]});
  }
 }
 // Shift edge cases use actual C arm_step as the comparator, including
 // register amount masking to eight bits and all carry-out boundaries.
 for(let type=0;type<4;type++)for(let amount=0;amount<32;amount++){
  await program([[0,(0xe1b00002|(type<<5)|(amount<<7))>>>0]]);
  for(const value of [0,1,0x7fffffff,0x80000000,0x80000001,0xffffffff])for(const flags of [0,0x20000000,0x90000000,0xf0000000])
   compare('ARM immediate shifts',{flags,regs:[[2,value]]});
 }
 for(let type=0;type<4;type++){
  await program([[0,(0xe1b00312|(type<<5))>>>0]]);
  for(const amount of [0,1,31,32,33,63,64,255,256,257,0x10000020])for(const value of [0,1,0x7fffffff,0x80000000,0x80000001,0xffffffff])for(const flags of [0,0x20000000,0x90000000,0xf0000000])
   compare('ARM register shift boundaries',{flags,regs:[[2,value],[3,amount]]});
 }
 for(let opcode=0;opcode<16;opcode++)for(const setFlags of [false,true]){
  if(opcode>=8&&opcode<=11&&!setFlags)continue;
  const rn=opcode===13||opcode===15?0:1;
  for(let type=0;type<4;type++){
   const raw=(0xe0000312|(opcode<<21)|(+setFlags<<20)|(rn<<16)|(type<<5))>>>0;
   await program([[0,raw]]);
   for(const amount of [0,1,32,33,255])for(const value of [0,0x7fffffff,0x80000000,0xffffffff])for(const flags of [0,0x20000000])
    compare('ARM shifted ALU integration',{flags,regs:[[1,0x7fffffff],[2,value],[3,amount]]});
  }
 }
 for(const raw of [0xe1b0008f,0xe08f0082]){await program([[0,raw]]);compare('ARM immediate-shift PC operand',{regs:[[2,0x12345678]]});}
 for(const raw of [0xe1b00f12,0xe1b0031f,0xe08f0312]){await program([[0,raw]]);compare('ARM register-shift PC rejection',{retired:0,reason:3});}
 for(const raw of [0xe1b00012,0xe1b00310,0xe0911312,0xe0910112]){
  await program([[0,raw]]);for(const amount of [0,1,32,33,255])compare('ARM shift register aliases',{regs:[[0,amount],[1,amount],[2,0x80000001],[3,amount]]});
 }
 for(let cond=0;cond<15;cond++){
  await program([[0,((cond<<28)|0x03b00007)>>>0]]);
  for(let flags=0;flags<16;flags++)compare('ARM conditions',{flags:(flags<<28)>>>0});
 }
 for(const raw of [0x20ff,0x28ff,0x30ff,0x38ff]){
  await program([[0,raw]],true);for(let flags=0;flags<16;flags++)for(const value of [0,0x7fffffff,0x80000000,0xffffffff])compare('Thumb immediate ALU',{flags:(flags<<28)>>>0,regs:[[0,value]]});
 }
 for(let type=0;type<3;type++)for(let amount=0;amount<32;amount++){
  await program([[0,(type<<11)|(amount<<6)|(1<<3)]],true);
  for(const value of [0,1,0x7fffffff,0x80000000,0x80000001,0xffffffff])for(const flags of [0,0x20000000,0x90000000,0xf0000000])
   compare('Thumb immediate shifts',{flags,regs:[[1,value]]});
 }
 for(const immediate of [false,true])for(const sub of [false,true])for(const rd of [0,1,2,7]){
  const rs=rd===7?7:1,source=rd===7?7:2;
  await program([[0,0x1800|(+immediate<<10)|(+sub<<9)|(source<<6)|(rs<<3)|rd]],true);
  for(const a of [0,1,0x7fffffff,0x80000000,0xffffffff])for(const b of [0,1,0x7fffffff,0x80000000,0xffffffff])for(const flags of [0,0x20000000,0x90000000,0xf0000000])
   compare('Thumb add/sub register and imm3 aliases',{flags,regs:[[rs,a],[source,b]]});
 }
 for(const sub of [false,true])for(let imm=0;imm<8;imm++){
  await program([[0,0x1c00|(+sub<<9)|(imm<<6)|(1<<3)]],true);
  compare('Thumb imm3 literal range',{regs:[[1,0xffffffff]]});
 }
 for(let code=0;code<16;code++)for(const alias of [false,true]){
  const rs=alias?0:1;await program([[0,0x4000|(code<<6)|(rs<<3)]],true);
  for(let nzcv=0;nzcv<16;nzcv++)for(const a of [0,1,0x7fffffff,0x80000000,0x80000001,0xffffffff])for(const b of alias?[a]:[0,1,0x7fffffff,0x80000000,0x80000001,0xffffffff])
   compare('Thumb register ALU all flags and aliases',{flags:((nzcv<<28)|0x080f0000)>>>0,regs:[[0,a],[rs,b]]});
 }
 for(const code of [2,3,4,7]){
  await program([[0,0x4000|(code<<6)|(1<<3)]],true);
  for(const amount of [0,1,31,32,33,63,64,255,256,257,0x10000020])for(const value of [0,1,0x7fffffff,0x80000000,0x80000001,0xffffffff])for(const flags of [0,0x20000000,0x90000000,0xf0000000])
   compare('Thumb register shift boundaries',{flags,regs:[[0,value],[1,amount]]});
 }
 for(const code of [0,1,2])for(const rd of [0,7,8,14,15])for(const rs of [0,7,8,14,15]){
  if(rd===15&&code!==1)continue;
  const raw=0x4400|(code<<8)|((rd&8)<<4)|(rs<<3)|(rd&7);
  await program([[0,raw]],true);
  for(let nzcv=0;nzcv<16;nzcv++)for(const value of [0,0x7fffffff,0x80000000,0xffffffff])
   compare('Thumb high registers and PC reads',{flags:((nzcv<<28)|0x080f0000)>>>0,regs:[[rd,0x80000001],[rs,value]].filter(([r])=>r!==15)});
 }
 for(const raw of [0x4487,0x4687,0x4700,0x4780,0x6802]){
  await program([[0,raw]],true);compare('Thumb PC write/exchange or refused load',{retired:0,reason:3});
 }
 await program([[0,0x1840],[2,0x2807],[4,0xd1fc],[6,0x6802]],true);
 for(const count of [0,1,2,3,20,21])compare('Thumb connected register loop budget',{count,regs:[[0,0],[1,1]]});
 compare('Thumb register loop slow boundary',{count:22,retired:21,reason:3,regs:[[0,0],[1,1]]});
 await program([[0,0xe28f0001]]);compare('architectural PC operand');
 await program([[0,0xe2800001],[4,0xeafffffd]]);for(const count of [0,1,2,3,31,256]){compare('backward loop budget',{count});if(!count)assert.equal(guardCalls,0);}
 compare('mid-block entry',{entry:4,count:3});
 await program([[0,0xea0000fe],[1024,0xe2800001],[1028,0xeafffefd]]);compare('cross-window loop',{count:17});
 await program([[0,0xeb0000fe]]);compare('external linked edge',{count:2,retired:1,reason:2});
 await program([[0,0xe2800001],[4,0xe5901000]]);compare('refused load after prefix',{count:3,retired:1,reason:3});compare('stale first instruction',{count:3,retired:0,reason:4,refuse:0});compare('stale after prefix',{count:3,retired:1,reason:4,refuse:4});
 await program([[0,0xe12fff12]]);compare('BX Thumb boundary',{count:4,retired:1,reason:6,regs:[[2,0x401]]});compare('BX invalid alignment',{count:4,retired:0,reason:3,regs:[[2,0x402]]});
 await program([[0,0x112fff12],[4,0xe3a00003]]);compare('conditional BX false',{count:2,flags:0x40000000,regs:[[2,0x402]]});compare('conditional BX true',{count:4,retired:1,reason:6,regs:[[2,0x401]]});
 reset();throwGuard=true;assert.throws(()=>active.run(m._review_cpu(),1,result),/synthetic guard trap/);throwGuard=false;report.groups['unexpected trap propagates']=1;report.checks++;
 report.passed=true;
}catch(e){report.error=e.stack;report.lastProgram=active?.ops;process.exitCode=1;}
finally{m._free(result);report.sources={};for(const p of ['decode.mjs','unit.mjs','emit.mjs','test-emitter.mjs'])report.sources[p]=hash(await readFile(new URL(p,import.meta.url)));await writeFile(out+'/report.json',JSON.stringify(report,null,2));console.log(JSON.stringify(report));}
