const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),assert=require('node:assert/strict');
const wabtFactory=require('../../node_modules/wabt'),create=require('./oracle.cjs');
const compilers=[require('../../tools/jit/compiler.cjs'),require(process.env.GROUP_COMPILER||'./compiler.cjs')];
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
const here=__dirname;
(async()=>{
 const m=await create(),wabt=await wabtFactory(),offsets=Array.from({length:16},(_,i)=>m._emu_jit_offset(i));
 compilers.forEach(c=>c.configure(offsets));
 const results=[];let checks=0;
 const branch=(pc,target)=>((0xea000000|(((target-pc-8)>>2)&0xffffff))>>>0);
 const tb=(pc,target)=>(0xe000|(((target-pc-4)>>1)&0x7ff));
 function state(){const n=m._review_state_size();return Buffer.concat([Buffer.from(new Uint8Array(m.jitMemory.buffer,m._review_state(),n)),crypto.createHash('sha256').update(new Uint8Array(m.jitMemory.buffer,m._review_ram(),1<<20)).digest()]);}
 async function prepare(thumb,kind='loop'){
  const width=thumb?2:4,ops=[];
  for(let i=0;i<80;i++){
   const pc=i*2*width,next=((i+1)%80)*2*width;
   ops.push([pc,thumb?0x3001:0xe2800001],[pc+width,thumb?tb(pc+width,next):branch(pc+width,next)]);
  }
  if(kind==='alias')ops[62][1]=0xe5810000;
  if(kind==='unsupported')ops[64][1]=thumb?0x4718:0xef000000;
  if(kind==='leave-page')ops[63][1]=thumb?tb(ops[63][0],0x400):branch(ops[63][0],0x400);
  if(kind==='mode-change')ops[63][1]=0xe12fff12;
  const modules=[];
  for(const compiler of compilers){
   const b=compiler.compileRegion({pc:0,thumb,index:0,ops:new Map(ops)});
   const parsed=wabt.parseWat('groups.wat','(module(import "env" "memory"(memory 8192))(import "env" "stale"(func $stale(param i32 i32)))'+b.wat+')');parsed.validate();
   const bytes=parsed.toBinary({}).buffer;parsed.destroy();
   modules.push({built:b,module:await WebAssembly.compile(bytes)});
  }
  assert.deepEqual(modules[0].built.masks,modules[1].built.masks);
  assert(modules[1].built.groups>=3,'must exercise real internal groups');
  return {thumb,width,ops,modules,kind};
 }
 async function check(p,{count=161,cap=512,mmu=0,entry=0,mutate=null,regs=[],timer=null,direct=false,label='loop'}={}){
  let expected;const rows=[];
  for(let engine=0;engine<3;engine++){
   assert.equal(m._review_begin(0,+p.thumb,0,0,mmu),0);
   const view=new DataView(m.jitMemory.buffer),base=m._review_ram();
   for(const [pc,w]of p.ops)view[p.thumb?'setUint16':'setUint32'](base+pc,w,true);
   // A real instruction outside the region is always interpreted.
   view.setUint32(base+0x400,0xeafffffe,true);
   if(mutate)view[p.thumb?'setUint16':'setUint32'](base+mutate.pc,mutate.w,true);
   for(const [r,v]of regs)m._review_reg(r,v);m._review_reg(15,entry);
   let id=null,staleCalls=0;
   if(engine){
    const {built,module}=p.modules[engine-1],instance=await WebAssembly.instantiate(module,{env:{memory:m.jitMemory,stale:(a,b)=>{staleCalls++;m._emu_jit_stale(a,b);}}});
    id=m.addFunction(instance.exports.b0,'iii');const mask=m._malloc(64);new Uint32Array(m.jitMemory.buffer,mask,16).set(built.masks);assert.equal(m._emu_jit_install(0,+p.thumb,id,mask),1);m._free(mask);
   }
   if(timer)m._review_timer(...timer);
   // Both seed variants deliberately use only original_try. The oracle also
   // contains a continuation candidate; none of its entry points is called.
   const ran=direct?(engine?m._review_control_try(count):0):timer?m._review_machine_run(count,engine?1:0):m._review_run(count,engine?1:0,cap);
   let bytes=state();if(timer){const ptr=m._review_snapshot(),size=m._review_snapshot_size();assert(ptr&&size);bytes=Buffer.concat([bytes,crypto.createHash('sha256').update(new Uint8Array(m.jitMemory.buffer,ptr,size)).digest()]);}
   const row={engine,ran,staleCalls,generated:m._emu_jit_stat(0),calls:m._emu_jit_stat(1),stateSha256:sha(bytes)};rows.push(row);
   if(!engine)expected={ran,bytes};else {assert.equal(ran,expected.ran,label+' retirement');assert(bytes.equals(expected.bytes),label+' full state');}
   m._emu_jit_reset();if(id!==null)m.removeFunction(id);
  }
  assert.equal(rows[1].generated,rows[2].generated,label+' generated classification');assert.equal(rows[1].calls,rows[2].calls,label+' public calls');assert.equal(rows[1].staleCalls,rows[2].staleCalls,label+' stale callback');
  if(mutate)assert(rows[1].staleCalls>0,'must reach actual stale guard');
  checks++;results.push({label,thumb:p.thumb,kind:p.kind,count,cap,mmu,entry,mutate,regs,timer,direct,rows});
 }
 for(const thumb of [false,true]){
  const p=await prepare(thumb);
  for(const count of [0,1,2,63,64,65,127,128,129,160,161,321])for(const cap of [1,2,31,64,512])for(const mmu of [0,1])await check(p,{count,cap,mmu,label:'three-groups-budget-loop'});
  for(const block of [0,31,32,63,64,79])for(const tail of [0,1])await check(p,{count:163,entry:(block*2+tail)*p.width,label:'exact-original-tail-entry'});
  for(const block of [0,31,32,63,64,79])await check(p,{count:163,entry:(block*2+1)*p.width,mutate:{pc:block*2*p.width,w:thumb?0x3107:0xe2811007},label:'stale-earlier-op-on-tail-entry'});
  for(const block of [32,64])await check(p,{count:163,mutate:{pc:block*2*p.width,w:thumb?0x3107:0xe2811007},label:'stale-new-group'});
  await check(p,{count:0,direct:true,label:'zero-budget-no-callback'});
  const unsupported=await prepare(thumb,'unsupported');await check(unsupported,{count:67,label:'true-unsupported-not-group-transfer'});
  const leave=await prepare(thumb,'leave-page');await check(leave,{count:65,label:'region-exit-not-group-transfer'});
 }
 const arm=await prepare(false);
 for(const phase of [0,1,2,3])for(const ticks of [1,2])for(const unmask of [0,1])for(const count of [63,64,65,129])await check(arm,{count,timer:[phase,ticks,unmask],entry:31*8,label:'literal-tick-and-device-deadline'});
 const alias=await prepare(false,'alias');for(const mmu of [0,1])for(const target of [32*8,64*8])await check(alias,{count:163,mmu,regs:[[0,0xe2811007],[1,(mmu?0x100000:0)+target]],label:'physical-code-write-before-group-crossing'});
 const mode=await prepare(false,'mode-change');for(const target of [32*8,32*8+1,32*8+2,32*8+3])await check(mode,{count:67,regs:[[2,target]],label:'BX-low-bits-and-mode-exit'});
 const report={passed:true,checks,scope:'Interpreter, original seed and grouped seed using original runtime only; exact architecture/TLB/memory counters and full1MiBRAM; literal tick tests also full snapshot hash; generated retirement/public calls/stale callbacks equal',oracleWasmSha256:sha(fs.readFileSync(path.join(here,'oracle.wasm'))),results};
 fs.writeFileSync(path.join(here,'focused-report.json'),JSON.stringify(report,null,2)+'\n');console.log(JSON.stringify({passed:true,checks,scope:report.scope}));
})().catch(e=>{console.error(e);process.exitCode=1;});
