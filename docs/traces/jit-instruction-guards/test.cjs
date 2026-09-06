const fs=require('node:fs'),crypto=require('node:crypto'),assert=require('node:assert/strict'),wabtFactory=require('wabt');
const create=require('./oracle.cjs'),compilers=[require('../../tools/jit/compiler.cjs'),require(process.env.TEST_COMPILER||'./compiler.cjs')],root='.context/jit-instruction-guards/';
(async()=>{
 const m=await create(),wabt=await wabtFactory(),offsets=Array.from({length:16},(_,i)=>m._emu_jit_offset(i));compilers.forEach(c=>c.configure(offsets));
 new Uint32Array(m.HEAPU32.buffer,m._review_program(),1).set([0x46c0]);assert.equal(m._review_begin(1,1,0,0,0),0);
 let checks=0,policyDifferences=0,zeroChecks=0,aliasChecks=0,ids=[],built=[],raw,thumb;
 const examples=[];
 async function program(words,t){raw=words;thumb=t;for(const id of ids)m.removeFunction(id);ids=[];built=[];
  for(const compiler of compilers){const b=compiler.compileRegion({pc:0,thumb:t,index:0,ops:new Map(words.map((w,i)=>[i*(t?2:4),w]))});assert.equal(b.count,words.length);built.push(b);
   const p=wabt.parseWat('guards.wat','(module(import "env" "memory"(memory 8192))(import "env" "stale"(func $stale(param i32 i32)))'+b.wat+')');p.validate();const bytes=p.toBinary({}).buffer;p.destroy();const inst=await WebAssembly.instantiate(bytes,{env:{memory:m.jitMemory,stale:m._emu_jit_stale}});ids.push(m.addFunction(inst.instance.exports.b0,'iii'));
  }
 }
 function state(){const size=m._review_state_size(),a=Buffer.from(new Uint8Array(m.jitMemory.buffer,m._review_state(),size));return Buffer.concat([a,crypto.createHash('sha256').update(new Uint8Array(m.jitMemory.buffer,m._review_ram(),1<<20)).digest()]);}
 function compare({entry=0,mutate=-1,replacement=null,count=1,cap=16,flags=0,regs=[],alias=0,mid=null,label=''}){
  const rows=[];let reference;
  for(let engine=0;engine<3;engine++){
   new Uint32Array(m.jitMemory.buffer,m._review_program(),raw.length).set(raw);assert.equal(m._review_begin(raw.length,+thumb,flags,entry*(thumb?2:4),alias),0);
   for(const [r,v] of regs)m._review_reg(r,v);
   function change(index,value){const pointer=m._review_ram()+index*(thumb?2:4);if(thumb)new DataView(m.jitMemory.buffer).setUint16(pointer,value,true);else new DataView(m.jitMemory.buffer).setUint32(pointer,value,true);}
   if(mutate>=0)change(mutate,replacement===null?(thumb?0x3107:0xe2811007):replacement);
   if(engine){const mask=m._malloc(64);new Uint32Array(m.jitMemory.buffer,mask,16).set(built[engine-1].masks);assert.equal(m._emu_jit_install(0,+thumb,ids[engine-1],mask),1);m._free(mask);}
   const before=count===0?state():null;let ran;
   if(count===0){ran=engine?m._review_try(0):m._review_run(0,0,1);assert(before.equals(state()));assert.equal(m._emu_jit_stat(0),0);assert.equal(m._emu_jit_stat(1),0);zeroChecks++;}
   else if(mid){ran=m._review_run(mid.after,engine?1:0,cap);change(mid.index,mid.raw);ran+=m._review_run(count-mid.after,engine?1:0,cap);}
   else ran=m._review_run(count,engine?1:0,cap);
   const bytes=state(),row={engine,ran,generated:m._emu_jit_stat(0),calls:m._emu_jit_stat(1),regions:m._emu_jit_stat(2)};rows.push(row);
   if(!engine)reference={ran,bytes};else if(ran!==reference.ran||!bytes.equals(reference.bytes)){fs.writeFileSync(root+'failure.json',JSON.stringify({label,raw,thumb,entry,mutate,replacement,count,cap,flags,regs,alias,mid,rows,expected:[...reference.bytes],actual:[...bytes]},null,2));throw Error('State mismatch '+label);}
  }
  checks++;if(alias)aliasChecks++;if(rows[1].generated!==rows[2].generated||rows[1].regions!==rows[2].regions||rows[1].calls!==rows[2].calls){policyDifferences++;if(examples.length<20)examples.push({label,thumb,entry,mutate,count,cap,rows});}
  return rows;
 }
 for(const t of [false,true]){
  await program(Array.from({length:8},(_,i)=>t?0x3001+(i%4)*0x100:0xe2800001+(i%4)*0x11000),t);
  for(const entry of [0,1,3,7])for(const mutate of [-1,0,1,3,7])for(const max of [0,1,2,4,8])for(const cap of [1,8])for(const flags of [0,0xf0000000]){const count=Math.min(max,8-entry);compare({entry,mutate,count,cap,flags,label:'straight-tail-prefix-suffix-budget'});}
  for(const entry of [0,3])for(const mutate of [entry,7])for(const replacement of [t?0xde00:0xe7f000f0])compare({entry,mutate,replacement,count:8-entry,label:'changed-current-or-later-undefined'});
  for(const after of [1,3])for(const index of [0,after,7])compare({count:8,mid:{after,index,raw:t?0x3107:0xe2811007},label:'mutation-between-calls-after-prior-work'});
  const prefix=compare({entry:3,mutate:0,count:2,label:'assert-prefix-policy'});assert.equal(prefix[1].generated,0);assert.equal(prefix[2].generated,2);assert.equal(prefix[1].regions,0);assert.equal(prefix[2].regions,1);
  const suffix=compare({entry:0,mutate:7,count:2,label:'assert-unexecuted-suffix-policy'});assert.equal(suffix[1].generated,0);assert.equal(suffix[2].generated,2);assert.equal(suffix[2].regions,1);
  const current=compare({mutate:3,count:5,label:'assert-prior-state-writeback'});assert.equal(current[2].generated,3);assert.equal(current[2].regions,0);
  await program(t?[0x3001,0x3a01,0xd1fc,0x46c0]:[0xe2800001,0xe2522001,0x1afffffc,0xe1a00000],t);
  for(const count of [1,2,3,4,6,9,10])for(const cap of [1,3,10])for(const mutate of [-1,0,2])compare({count,cap,mutate,regs:[[2,3]],label:'conditional-branch-loop-budget'});
  // Store through an identity or a second virtual mapping of the fetched physical page.
  await program(t?[0x3201,0x6008,0x3301,0x3401]:[0xe2822001,0xe5810000,0xe2833001,0xe2844001],t);
  for(const alias of [0,1])for(const target of [0,2,3])for(const cap of [1,4]){
   const word=t?(target===0?0x3207:target===2?0x3307:0x3407):(target===0?0xe2822007:target===2?0xe2833007:0xe2844007);
   // Thumb STR writes two opcodes; preserve the adjacent instruction explicitly.
   const replacement=t?((word&65535)|((raw[target+1]||0x46c0)<<16)):word;
   const rows=compare({alias,count:4,cap,regs:[[0,replacement>>>0],[1,(alias?0x100000:0)+target*(t?2:4)]],label:'physical-code-store-alias'});
   assert(rows[2].generated<4,'code store must fall back through alias guard');
  }
 }
 const report={passed:true,checks,zeroChecks,aliasChecks,policyDifferences,examples,scope:'Original interpreter versus original/candidate compilers: exact packed architectural bytes, host TLB/read/write/status counters and full1MiBRAM hash. ARM+Thumb stale prefix/suffix/current instruction, tail entry, prior work, undefined fallback, split mutation, conditional branches, budgets and physical code-write aliases. Host JIT retirement/calls/regions explicitly compared and allowed to differ by policy.'};fs.writeFileSync(root+'focused-report.json',JSON.stringify(report,null,2)+'\n');console.log(report);m._emu_jit_reset();for(const id of ids)m.removeFunction(id);
})().catch(e=>{console.error(e);process.exit(1)});
