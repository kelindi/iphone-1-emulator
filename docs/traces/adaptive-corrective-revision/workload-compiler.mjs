// Synchronous Stage 2 compiler owner. Guest execution cannot run across an await
// here; WABT must be initialized before constructing this owner.
import assert from 'node:assert/strict';
import {buildUnit} from './unit.mjs';
import {emitUnit} from './emit.mjs';
import {createModuleCache} from './module-cache.mjs';
export function createWorkloadCompiler(m,wabt){
 assert.equal(m._workload_layout(0),2);assert.equal(m._workload_layout(9),4);
 const witnessSize=m._workload_layout(4),witnesses=m._malloc(witnessSize*8),retired=m._malloc(2048*4);
 assert(witnesses&&retired);
 const cache=createModuleCache();
 const owned=new Map(),stats={attempts:0,captureRefusals:0,unsupported:0,compiled:0,installed:0,rejected:0,binaryBytes:0,compileCpuMs:0,compileWallMs:0,moduleCache:cache.stats};
 const layout={registers:m._workload_layout(1),cpsr:m._workload_layout(2)};
 const offsets={host:m._workload_layout(5),block:m._workload_layout(6),version:m._workload_layout(7)};
 function drain(){
  const n=m._adaptive_retired(retired,2048),ids=Array.from(new Uint32Array(m.jitMemory.buffer,retired,n));
  for(const id of ids){assert(owned.delete(id),'Runtime returned unknown function handle');m.removeFunction(id);}
 }
 function compile(entryPc,thumb){
  entryPc>>>=0;thumb=!!thumb;stats.attempts++;
  const cpu=process.cpuUsage(),start=performance.now();
  let pendingId;
  try{
   drain();
   const epoch=m._adaptive_epoch(),regions=[],seen=new Set();
   function capture(pc){
    const base=(pc&~1023)>>>0;if(seen.has(base)||regions.length===8)return false;seen.add(base);
    const ptr=witnesses+regions.length*witnessSize;
    if(!m._adaptive_capture_at(pc>>>0,ptr)){stats.captureRefusals++;return false;}
    const view=new DataView(m.jitMemory.buffer),host=view.getUint32(ptr+offsets.host,true),virtualBase=view.getUint32(ptr+offsets.block,true),version=view.getUint32(ptr+offsets.version,true);
    const ram=m._workload_ram()>>>0,size=m._workload_ram_size()>>>0;
    assert(host>=ram&&host-ram<=size-1024&&version,'Capture must belong to tracked main RAM');
    regions.push({virtualBase,physicalBase:host-ram,version,bytes:Uint8Array.from(new Uint8Array(m.jitMemory.buffer,host,1024))});return true;
   }
   if(!capture(entryPc))return;
   let unit;
   // Only follow actual connected edges, never inspect arbitrary guest memory.
   while(true){
    unit=buildUnit({entryPc,thumb,regions});
    let added=false;
    for(const refusal of unit.refusals)if(refusal.reason==='uncaptured'&&capture(refusal.pc))added=true;
    if(!added)break;
   }
   if(!unit.blocks.some(b=>b.instructions.some(i=>i.op!=='fallback'))){stats.unsupported++;return;}
   const required=new Set(unit.blocks.flatMap(b=>b.instructions.map(i=>(i.pc&~1023)>>>0)));
   const selected=regions.map((r,index)=>({r,index})).filter(({r})=>required.has(r.virtualBase));
   const captured=Uint8Array.from(new Uint8Array(m.jitMemory.buffer,witnesses,regions.length*witnessSize));
   selected.forEach(({index},slot)=>new Uint8Array(m.jitMemory.buffer,witnesses+slot*witnessSize,witnessSize).set(captured.subarray(index*witnessSize,(index+1)*witnessSize)));
   const wat=emitUnit(unit,layout),cached=cache.get(wat);
   let module,binaryBytes;
   if(cached){module=cached.module;binaryBytes=cached.binaryBytes;}
   else{
    const parsed=wabt.parseWat('workload-unit.wat',wat);let binary;
    try{parsed.validate();binary=parsed.toBinary({}).buffer;}finally{parsed.destroy();}
    module=new WebAssembly.Module(binary);binaryBytes=binary.length;
    cache.put(wat,module,binaryBytes);stats.compiled++;stats.binaryBytes+=binaryBytes;
   }
   const instance=new WebAssembly.Instance(module,{env:{memory:m.jitMemory,guard:m._adaptive_guard,read:m._adaptive_read,write:m._adaptive_write}});
   pendingId=m.addFunction(instance.exports.run,'viii');
   if(!m._adaptive_install(entryPc,+thumb,pendingId,witnesses,selected.length,epoch,binaryBytes)){stats.rejected++;return;}
   owned.set(pendingId,instance);pendingId=undefined;stats.installed++;
  }finally{
   if(pendingId!==undefined)m.removeFunction(pendingId);
   const delta=process.cpuUsage(cpu);stats.compileCpuMs+=(delta.user+delta.system)/1000;stats.compileWallMs+=performance.now()-start;
  }
 }
 function close(){m._adaptive_enable(0);drain();assert.equal(owned.size,0);cache.clear();m._free(witnesses);m._free(retired);}
 return {compile,drain,close,stats};
}
