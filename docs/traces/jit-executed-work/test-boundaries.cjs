const fs=require('node:fs'),crypto=require('node:crypto'),assert=require('node:assert/strict'),wabtFactory=require('wabt');
const create=require(process.env.ORACLE_MODULE||'./observer-oracle.cjs'),compiler=require(process.env.COMPILER_PATH||'../../tools/jit/compiler.cjs'),root=(process.env.ARTIFACT_DIR||'.context/jit-continuation').replace(/\/?$/,'/');fs.mkdirSync(root,{recursive:true});
(async()=>{
 const m=await create(),wabt=await wabtFactory(),offsets=Array.from({length:16},(_,i)=>m._emu_jit_offset(i));compiler.configure(offsets);if(compiler.configureCensus)compiler.configureCensus({base:m._emu_fc_generated(),enabled:m._emu_fc_enabled_pointer(),overflow:m._emu_fc_generated_overflow()});assert.equal(m._review_begin(0,0,0,0,0),0);
 const originalRuntimeOnly=true; const cases=[];const runtime=e=>originalRuntimeOnly&&e?1:e;const onlyRefill=process.env.ONLY_REFILL==='1';const countsByLabel={};let checks=0,ids=[],built=[],instructions=[],mode=false,examples=[];
 const branch=(pc,target)=>((0xea000000|(((target-pc-8)>>2)&0xffffff))>>>0),tb=(pc,target)=>(0xe000|(((target-pc-4)>>1)&0x7ff));
 async function program(regions){for(const id of ids)m.removeFunction(id);ids=[];built=[];instructions=regions.flatMap(r=>r.ops.map(([pc,w])=>({pc,w,thumb:r.thumb})));mode=regions[0].thumb;
  for(const [index,r]of regions.entries()){const b=compiler.compileRegion({...r,index,ops:new Map(r.ops)});assert.equal(b.count,r.supportedCount??r.ops.length);built.push({...b,...r});let wat=b.wat;if(r.retireAfter){const name=JSON.stringify('b'+index);wat=wat.replace('(func(export '+name+')','(func $wrapped'+index)+`(func(export ${name})(param $c i32)(param $budget i32)(result i32)(local $n i32)(local.set $n(call $wrapped${index}(local.get $c)(local.get $budget)))(if(local.get $n)(then(call $stale(i32.const ${r.pc})(i32.const ${+r.thumb}))))(local.get $n))`;}const p=wabt.parseWat('continuation.wat','(module(import "env" "memory"(memory 8192))(import "env" "stale"(func $stale(param i32 i32)))'+wat+')');p.validate();const bytes=p.toBinary({}).buffer;p.destroy();const inst=await WebAssembly.instantiate(bytes,{env:{memory:m.jitMemory,stale:m._emu_jit_stale}});ids.push(m.addFunction(inst.instance.exports['b'+index],'iii'));}
 }
 function state(){const size=m._review_state_size();return Buffer.concat([Buffer.from(new Uint8Array(m.jitMemory.buffer,m._review_state(),size)),crypto.createHash('sha256').update(new Uint8Array(m.jitMemory.buffer,m._review_ram(),1<<20)).digest()]);}
 function compare({count=9,cap=64,mmu=0,warm=true,guard=0,mutate=null,regs=[],label='',tryOnly=false,maskTarget=true,expectedIfar=null,timer=null}={}){
  let reference,referenceWork;const rows=[];
  for(let engine=0;engine<3;engine++){
   assert.equal(m._review_begin(0,+mode,0,0,mmu),0);if(warm){for(const b of built.slice(1))assert.equal(m._review_prime(b.pc,+b.thumb),0);assert.equal(m._review_prime(0,+mode),0);}
   const v=new DataView(m.jitMemory.buffer);for(const o of instructions)v[o.thumb?'setUint16':'setUint32'](m._review_ram()+(o.pc&0xfffff),o.w,true);
   if(mutate)v[mutate.thumb?'setUint16':'setUint32'](m._review_ram()+mutate.pc,mutate.w,true);for(const [r,n]of regs)m._review_reg(r,n);
   if(engine){const mask=m._malloc(64);for(const [i,b]of built.entries()){const masks=[...b.masks];if(!maskTarget&&i>0)masks.fill(0);new Uint32Array(m.jitMemory.buffer,mask,16).set(masks);assert.equal(m._emu_jit_install(b.pc,+b.thumb,ids[i],mask),1);}m._free(mask);}
   if(timer)m._review_timer(...timer);if(guard)m._review_guard(guard);if(guard===13&&warm)assert(m._review_value(2)>0,'fault-cache mutation must affect a real target entry');const before=state();let ran; if(m._emu_fc_start)m._emu_fc_start();
   if(tryOnly){if(!engine){ran=0;}else ran=runtime(engine)===1?m._review_control_try(count):m._review_try(count);if(count===0||guard)assert(before.equals(state()),'rejected direct try must preserve architectural/counter state');}
   else ran=timer?m._review_machine_run(count,runtime(engine)):m._review_run(count,runtime(engine),cap);
   if(expectedIfar!==null)assert.equal(m._review_value(0),expectedIfar,'actual target fetch must fault');let bytes=state();if(timer){const ptr=m._review_snapshot(),size=m._review_snapshot_size();assert(ptr&&size);bytes=Buffer.concat([bytes,crypto.createHash('sha256').update(new Uint8Array(m.jitMemory.buffer,ptr,size)).digest()]);}let observer=null;
   if(m._emu_fc_stop){m._emu_fc_stop();const v=new DataView(m.jitMemory.buffer),p=m._emu_fc_summary();const names=['calls','ok','fetched','nofetch','nonok','overflow','mapOverflow','epoch','installs'];observer=Object.fromEntries(names.map((name,i)=>[name,Number(v.getBigUint64(p+i*8,true))]));let sum=0;const base=m._emu_fc_rows();for(let i=0;i<m._emu_fc_capacity();i++)if(v.getUint32(base+i*40+28,true))sum+=Number(v.getBigUint64(base+i*40+32,true));assert.equal(sum,observer.calls);assert.equal(observer.calls,observer.ok+observer.nonok);assert.equal(observer.calls,observer.fetched+observer.nofetch);assert.equal(observer.overflow,0);assert.equal(observer.mapOverflow,0);assert.equal(observer.installs,0);if(!tryOnly)assert.equal(ran,observer.ok+m._emu_jit_stat(0));
    const work=new Map(),credit=(pc,opcode,flags,status,count)=>{const key=[pc,opcode,flags,status].join(':');work.set(key,(work.get(key)||0n)+count);};
    for(let i=0;i<m._emu_fc_capacity();i++){const ptr=base+i*40;if(v.getUint32(ptr+28,true))credit(v.getUint32(ptr,true),v.getUint32(ptr+4,true),v.getUint32(ptr+8,true),v.getUint32(ptr+24,true),v.getBigUint64(ptr+32,true));}
    let generatedSum=0n;const gen=m._emu_fc_generated(),meta=new Map();for(const [index,b]of built.entries())for(const [pc,opcode]of b.ops){const offset=(pc-b.pc)/(b.thumb?2:4);if((b.masks[offset>>>5]>>>(offset&31))&1)meta.set(index*512+offset,{pc,opcode,flags:1|(b.thumb?256:0)});}
    for(let i=0;i<m._emu_fc_generated_capacity();i++){const count=v.getBigUint64(gen+i*8,true);if(!count)continue;assert(meta.has(i),'Count in unsupported/unmapped generated slot');const x=meta.get(i);generatedSum+=count;credit(x.pc,x.opcode,x.flags,0,count);}
    observer.generatedSum=Number(generatedSum);observer.generatedOverflow=Number(v.getBigUint64(m._emu_fc_generated_overflow(),true));assert.equal(observer.generatedOverflow,0);assert.equal(observer.generatedSum,m._emu_jit_stat(0),'Generated per-PC counts must conserve original retirement');
    const sorted=[...work].sort((a,b)=>a[0].localeCompare(b[0])).map(([key,count])=>({key,count:String(count)}));observer.work=sorted;observer.workSha256=crypto.createHash('sha256').update(JSON.stringify(sorted)).digest('hex');
    if(!engine)referenceWork=sorted;else assert.deepEqual(sorted,referenceWork,'Per-PC/opcode completed-work footprint must match actual interpreter');}
   const row={engine,ran,generated:m._emu_jit_stat(0),calls:m._emu_jit_stat(1),regions:m._emu_jit_stat(2)};rows.push(row);cases.push({label,count,cap,mmu,warm,guard,engine,ran,stateSha256:crypto.createHash('sha256').update(bytes).digest('hex'),...row,observer});
   if(!engine)reference={ran,bytes};else if(ran!==reference.ran||!bytes.equals(reference.bytes)){fs.writeFileSync(root+(process.env.ORACLE_MODULE?.split('/').pop().replace('-oracle.cjs','')||'focused')+'-failure.json',JSON.stringify({label,count,cap,mmu,warm,guard,mutate,regs,tryOnly,maskTarget,rows,expected:[...reference.bytes],actual:[...bytes]},null,2));throw Error('State mismatch '+label);}
  }
  checks++;countsByLabel[label]=(countsByLabel[label]||0)+1;if(examples.length<12||countsByLabel[label]===1)examples.push({label,count,cap,mmu,warm,guard,rows});return rows;
 }
 for(const thumb of (onlyRefill?[]:[false,true])){
  const width=thumb?2:4;await program([{pc:0,thumb,ops:[[0,thumb?0x3001:0xe2800001],[width,thumb?tb(width,0x400):branch(width,0x400)]]},{pc:0x400,thumb,ops:[[0x400,thumb?0x3102:0xe2811002],[0x400+width,thumb?tb(0x400+width,0):branch(0x400+width,0)]]}]);
  for(const count of [0,1,2,3,4,5,7,16,33,65,257])for(const cap of [1,2,3,8,64])for(const mmu of [0,1]){
   const rows=compare({count,cap,mmu,label:'cross-region-exact-budget'});
   if(!originalRuntimeOnly&&count===33&&cap===64)assert(rows[2].generated>rows[1].generated,'candidate must actually continue');
  }
  for(const warm of [false,true])for(const guard of [0,1,2,12,13,14,15])compare({count:9,mmu:1,warm,guard,label:'fetch-miss-stamp-fault-host-privilege'});
  const staleTarget=compare({count:3,mmu:1,mutate:{pc:0x400,thumb,w:thumb?0x3207:0xe2822007},label:'zero-progress-stale-target-after-refill'});if(!originalRuntimeOnly){assert.equal(staleTarget[2].generated,2);assert.equal(staleTarget[2].calls,2);assert.equal(staleTarget[2].regions,1);}
  compare({count:9,mmu:1,maskTarget:false,label:'unadmitted-destination-entry'});
  for(const pc of [0,0x400,0x400+width])compare({count:9,mmu:1,mutate:{pc,thumb,w:thumb?0x3207:0xe2822007},label:'stale-source-target-and-target-tail'});
  for(const guard of [3,4,5,6,7,8,9,10,11].filter(g=>!(thumb&&g===8)))compare({count:1,guard,mmu:1,label:'rejected-start-exception-or-fetch',tryOnly:true});
  compare({count:0,label:'zero-budget-identity',tryOnly:true});
 }


 if(!onlyRefill){
 await program([{pc:0,thumb:false,retireAfter:true,ops:[[0,0xe2800001],[4,branch(4,0x400)]]},{pc:0x400,thumb:false,ops:[[0x400,0xe2811002],[0x404,branch(0x404,0)]]}]);
 const selfRetired=compare({count:3,mmu:1,label:'invalidate-current-function-after-committed-prefix'});if(!originalRuntimeOnly){assert.equal(selfRetired[2].generated,3);assert.equal(selfRetired[2].regions,1);}
 await program([{pc:0,thumb:false,ops:[[0,0xe2800001],[4,branch(4,0x400)]]},{pc:0x400,thumb:false,ops:[[0x400,0xe2811002],[0x404,branch(0x404,0)]]}]);
 for(const phase of [0,1,2,3])for(const ticks of [1,2])for(const unmask of [0,1])for(const count of [1,2,3,4,5,9])compare({count,mmu:1,timer:[phase,ticks,unmask],label:'literal-tick-vs-original-and-continued-device-deadline'});
 }
 // Source/destination bytes initially match; stale destination alone must be
 // read through the refreshed mapping. A skipped-refill negative executes it wrong.
 await program([{pc:0,thumb:false,ops:[[0,0xe2800001],[4,0xe12fff12]]},{pc:0x400,thumb:false,ops:[[0x400,0xe2800001],[0x404,0xe12fff12]]}]);
 compare({count:3,mmu:1,mutate:{pc:0x400,thumb:false,w:0xe2800007},regs:[[2,0x400]],label:'stale-identical-source-destination-mapping'});
 if(onlyRefill){console.log(JSON.stringify({passed:true,checks,countsByLabel}));return;}
 // A distinct one-MiB mapping with a real section XN permission fault.
 await program([{pc:0,thumb:false,ops:[[0,0xe2800001],[4,0xe12fff12]]},{pc:0x100000,thumb:false,ops:[[0x100000,0xe2800001],[0x100004,0xe12fff12]]}]);
 compare({count:3,mmu:1,guard:16,regs:[[2,0x100000]],expectedIfar:0x100000,label:'actual-XN-target-prefetch-abort'});
 // ARM/Thumb interworking crosses a region and returns through register BX.
 await program([{pc:0,thumb:false,ops:[[0,0xe2800001],[4,0xe12fff12]]},{pc:0x400,thumb:true,supportedCount:1,ops:[[0x400,0x3102],[0x402,0x4718]]}]);
 for(const target of [0x400,0x401,0x402,0x403])for(const cap of [1,2,3,7])compare({count:9,cap,mmu:1,regs:[[2,target],[3,0]],label:'BX-target-low-bits'});
 // A store in the continued region aliases that newly fetched physical page.
 await program([{pc:0,thumb:false,ops:[[0,0xe2822001],[4,branch(4,0x400)]]},{pc:0x400,thumb:false,ops:[[0x400,0xe5810000],[0x404,0xe2833001],[0x408,branch(0x408,0)]]}]);
 for(const alias of [0,1])for(const target of [0x400,0x404])for(const cap of [1,3,9])compare({count:9,cap,mmu:alias,regs:[[0,0xe2833007],[1,(alias?0x100000:0)+target]],label:'physical-code-write-after-continuation'});
 const report={passed:true,cases,executedWorkSemantics:'Per-PC/opcode/mode/status/event footprint from actual interpreter and each generated n++ conserves exact retired counts; no physical mapping or side-effect claim.',originalRuntimeOnly,compilerPath:process.env.COMPILER_PATH||'original',checks,countsByLabel,examples,scope:originalRuntimeOnly?'Selected compiler compared with interpreter; rows1/2 both use the original runtime. No continuation candidate is combined. Exact architectural/read/TLB/RAM and timer snapshot gates.':'Actual original compiler functions with interpreter/original-runtime/candidate-runtime: exact architecture, TLB/read/write/status counters and entire1MiB RAM hash. Cross-region budgets, cold/warm fetch, MMU stamp/fault/permission paths, missing host RAM, unsupported admission, stale code, IRQ/FIQ/abort/invalid mode, interworking low bits and physical code-write aliases.'};fs.writeFileSync(root+'focused-report.json',JSON.stringify(report,null,2)+'\n');console.log(JSON.stringify(report));m._emu_jit_reset();for(const id of ids)m.removeFunction(id);
})().catch(e=>{console.error(e);process.exit(1)});
