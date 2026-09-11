import test from 'node:test';
import assert from 'node:assert/strict';
import wabtFactory from 'wabt';
import {createWorkloadCompiler} from '../workload-compiler.mjs';
const wabt=await wabtFactory();
function runtime(){
 const memory=new WebAssembly.Memory({initial:4}),view=new DataView(memory.buffer);
 const ram=65536;view.setUint32(ram,0xe2800001,true);view.setUint32(ram+4,0xeafffffd,true);
 let allocation=8192,nextHandle=1,epoch=1,live,queue=[],reject=false;
 const functions=new Map(),events=[];
 const m={jitMemory:memory,_workload_layout:n=>[2,0,64,256,16,0,4,8,32,4][n],
  _malloc:n=>{const p=allocation;allocation=(allocation+n+7)&~7;return p;},_free:p=>events.push(['free',p]),
  _workload_ram:()=>ram,_workload_ram_size:()=>65536,_adaptive_epoch:()=>epoch,
  _adaptive_capture_at(pc,p){events.push(['capture',epoch,pc]);if(pc>=1024)return 0;view.setUint32(p,ram,true);view.setUint32(p+4,0,true);view.setUint32(p+8,1,true);view.setUint32(p+12,epoch,true);return 1;},
  _adaptive_guard:()=>1,_adaptive_read:()=>0,_adaptive_write:()=>0,
  addFunction(fn,signature){assert.equal(signature,'viii');const id=nextHandle++;functions.set(id,fn);events.push(['add',id,fn]);return id;},
  removeFunction(id){assert(functions.delete(id),'Handle removed once');events.push(['remove',id]);},
  _adaptive_install(pc,thumb,id,witnesses,count,capturedEpoch,bytes){assert.equal(pc,0);assert.equal(thumb,0);assert.equal(count,1);assert(bytes>0);assert.equal(capturedEpoch,epoch);assert.equal(view.getUint32(witnesses+12,true),epoch,'Fresh capture, never cached witness');assert(functions.has(id));events.push(['install',id,epoch]);if(reject)return 0;if(live)queue.push(live);live=id;return 1;},
  _adaptive_retired(p,capacity){const ids=queue.splice(0,capacity);ids.forEach((id,i)=>view.setUint32(p+i*4,id,true));return ids.length;},
  _adaptive_enable(enabled){assert.equal(enabled,0);if(live)queue.push(live);live=undefined;epoch++;},
 };
 return {m,events,functions,setEpoch:n=>{epoch=n;},setReject:v=>{reject=v;}};
}
test('owner caches only modules; cache hits still capture, instantiate and strictly install',()=>{
 const r=runtime(),c=createWorkloadCompiler(r.m,wabt);
 c.compile(0,false);assert.equal(c.stats.compiled,1);assert.equal(c.stats.moduleCache.misses,1);
 const first=r.events.find(e=>e[0]==='add');
 r.setEpoch(2);c.compile(0,false);
 const added=r.events.filter(e=>e[0]==='add');assert.equal(added.length,2);
 assert.notEqual(added[1][1],first[1],'Distinct owned handle');
 assert.notEqual(added[1][2],first[2],'Distinct real WASM instance export');
 assert.equal(c.stats.compiled,1);assert.equal(c.stats.moduleCache.hits,1);assert.equal(c.stats.installed,2);
 assert.deepEqual(r.events.filter(e=>e[0]==='capture').map(e=>e[1]),[1,2]);
 c.drain();assert(!r.functions.has(first[1]));
 r.setEpoch(3);r.setReject(true);c.compile(0,false);
 const rejected=r.events.filter(e=>e[0]==='add').at(-1)[1];
 assert.equal(c.stats.rejected,1);assert.equal(c.stats.moduleCache.hits,2);
 assert(!r.functions.has(rejected),'Rejected pending handle immediately removed');
 assert(r.functions.has(added[1][1]),'Previous live handle survives rejected replacement');
 c.close();assert.equal(r.functions.size,0);assert.equal(c.stats.moduleCache.bytes,0);assert.equal(c.stats.moduleCache.entries,0);
 assert.equal(r.events.filter(e=>e[0]==='remove').length,3);
 const next=createWorkloadCompiler(r.m,wabt);r.setReject(false);next.compile(0,false);
 assert.equal(next.stats.moduleCache.hits,0);assert.equal(next.stats.moduleCache.misses,1);assert.equal(next.stats.compiled,1);
 next.close();assert.equal(r.functions.size,0);
});
