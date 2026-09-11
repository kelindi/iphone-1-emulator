// These tests exercise the maintained controller and worker lifecycle with
// mocked firmware/WASM dependencies. They do not execute guest instructions;
// separate CPU oracles verify generated instruction semantics.
import test from 'node:test';
import fs from 'node:fs';
import vm from 'node:vm';
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';
const root = fileURLToPath(new URL('../', import.meta.url));
const controllerSource = fs.readFileSync(`${root}/seed-jit.js`, 'utf8');
const workerSource = fs.readFileSync(`${root}/emu-worker.js`, 'utf8');
const hash = x => crypto.createHash('sha256').update(x).digest('hex');
const bytes = x => Uint8Array.from(Buffer.from(x)).buffer;
const tick = async () => { for (let i = 0; i < 30; i++) await Promise.resolve(); };
async function timeout(promise, ms = 500) {
  let timer;
  try {
    return await Promise.race([
      promise,
      new Promise((_, reject) => {
        timer = setTimeout(() => reject(Error(`Unsettled after ${ms}ms`)), ms);
      })
    ]);
  } finally {
    clearTimeout(timer);
  }
}
const region = (pc=0,index=1) => ({pc,index,thumb:false,masks:[1,...Array(15).fill(0)]});
function setup(options={}) {
  const events=[], active=new Map(), fnIds=new Map(), allocated=new Set(), waiting=[];
  const memory={buffer:new ArrayBuffer(2*1024*1024)};
  let ptr=128, nextId=1, mallocs=0, admissions=0, mode=0;
  let deviceTicks=0, displayLit=1;
  const defs=options.defs || [region()];
  const binary=bytes('bounded fake seed');
  const definitions=bytes(JSON.stringify(defs));
  const abi={revision:1,offsets:Array.from({length:16},(_,i)=>i),moduleSha256:hash(Buffer.from(binary)),regionsSha256:hash(Buffer.from(definitions)),...options.abi};
  const payloads={'seed.wasm':binary,'regions.json':definitions,'abi.json':bytes(JSON.stringify(abi))};
  const api={jitMemory:memory,HEAPU8:new Uint8Array(memory.buffer),HEAPU32:new Uint32Array(memory.buffer),
    _malloc(n){mallocs++;if(options.mallocFail===mallocs)return 0;const p=ptr;ptr=(ptr+n+7)&~7;allocated.add(p);events.push(['malloc',p,n]);return p;},
    _free(p){assert(allocated.delete(p),`free unknown pointer ${p}`);events.push(['free',p]);},
    _emu_jit_abi(){return 1;},_emu_jit_offset(i){return i;},
    _emu_jit_enable(value){mode=value;events.push(['mode',value]);},
    _emu_jit_reset(){events.push(['reset']);},
    addFunction(fn){assert.equal(typeof fn,'function');if(fnIds.has(fn))return fnIds.get(fn);const id=nextId++;active.set(id,fn);fnIds.set(fn,id);events.push(['add',id]);return id;},
    removeFunction(id){assert(active.has(id),`double/unknown remove ${id}`);fnIds.delete(active.get(id));active.delete(id);events.push(['remove',id]);},
    _emu_jit_install(pc,thumb,id){events.push(['install',pc,thumb,id]);admissions++;if(options.installThrow===admissions)throw Error('admission exception');return options.installFail===admissions?0:1;},
    _emu_jit_retired(){return 0;},_emu_jit_stat(){return 0;},_emu_jit_stale(){},_emu_jit_span(){return 0;},
    ccall(name){if(name==='emu_init')api.jitMemory=memory;events.push(['ccall',name]);return name==='emu_error'?'bad snapshot':0;},
    cwrap(name){return (...args)=>{
      events.push(['api',name,...args]);
      // Model the startup button/clock contract; real lock pixels and gestures
      // are covered by open-lock.mjs in Chrome with the genuine guest.
      if(name==='emu_run'){deviceTicks+=args[0];return args[0];}
      if(name==='emu_ticks')return deviceTicks;
      if(name==='emu_frame')return displayLit;
      if(name==='emu_button'&&!args[1])displayLit=args[0]===1?1:0;
      if(name==='emu_snapshot_advance')return 1;
      if(name==='emu_snapshot_data')return 64;
      if(name==='emu_snapshot_size')return 16;
      return 0;
    };}
  };
  if(options.lateMemory)delete api.jitMemory;
  if(options.missingExports)for(const key of Object.keys(api))if(key.startsWith('_emu_jit_'))delete api[key];
  const exportsFor = () => Object.fromEntries(defs.map(r=>['b'+r.index,options.badExport?7:function generated(){}]));
  const sandbox={URL,URLSearchParams,Uint8Array,Uint32Array,ArrayBuffer,TextDecoder,TextEncoder,
    AbortController,AbortSignal,DOMException,performance,setTimeout:options.fastTimers?((fn,ms)=>setTimeout(fn,Math.min(ms,10))):setTimeout,clearTimeout,queueMicrotask,
    location:{search:options.disabled?'?jit=0':'?jit=1',href:`http://localhost/emu-worker.js?jit=${options.disabled?0:1}`},
    crypto:{subtle:{async digest(_,buf){return Uint8Array.from(crypto.createHash('sha256').update(Buffer.from(buf)).digest()).buffer;}}},
    console:{error(...args){events.push(['error',...args]);},log(){}},
    async fetch(path,init={}){path=String(path).split('/').at(-1);events.push(['fetch',path]);
      if(path==='manifest.json')return {ok:true,json:async()=>({kernel:['kernel'],devicetree:['tree'],snapshot:['snapshot']})};
      if(options.stalled)return new Promise((_,reject)=>init.signal?.addEventListener('abort',()=>{events.push(['abort',path]);reject(Error('aborted'));}));
      if(options.missing)return {ok:false,status:404};
      return {ok:true,headers:{get(){return null;}},arrayBuffer:async()=>payloads[path]};
    },
    WebAssembly:{async compile(){events.push(['compile']);return {};},instantiate(){events.push(['instantiate']);if(options.delayed)return new Promise(resolve=>waiting.push(()=>resolve({exports:exportsFor()})));return Promise.resolve({exports:exportsFor()});}}
  };
  vm.createContext(sandbox);
  // Keep the controller's lexical bindings private so the worker can receive
  // a constructor dependency without rewriting its lifecycle implementation.
  vm.runInContext('{\n'+controllerSource+'\nglobalThis.Controller=SeedJIT;\n}',sandbox);
  let controller;
  const create=()=>{controller=new sandbox.Controller(api);return controller;};
  return {events,active,allocated,waiting,api,sandbox,create,get controller(){return controller;},get mode(){return mode;}};
}
async function rejected(options){const x=setup(options),c=x.create();await timeout(c.ready);assert.equal(c.enabled,false);assert.equal(x.mode,0);assert.equal(x.active.size,0);return x;}
test('disabled engine avoids optional fetch and allocations',async()=>{const x=setup({disabled:true});const c=x.create();await c.ready;assert(!x.events.some(e=>e[0]==='fetch'));assert.equal(x.allocated.size,0);assert.equal(x.mode,0);});
test('HTTP missing seed preserves interpreter fallback',()=>rejected({missing:true}));
test('missing shared-memory binding disables the optional engine',()=>rejected({lateMemory:true}));
test('older runtime without optional exports remains usable',async()=>{const x=await rejected({missingExports:true});assert.equal(x.allocated.size,0);x.controller.stats();x.controller.pump();await x.controller.reset();assert.equal(x.mode,0);});
test('stalled optional fetch is aborted and falls back',async()=>{const x=await rejected({stalled:true,fastTimers:true});assert(x.events.some(e=>e[0]==='abort'));});
test('module hash mismatch disables optional engine',()=>rejected({abi:{moduleSha256:'wrong'}}));
test('missing module hash disables optional engine',()=>rejected({abi:{moduleSha256:undefined}}));
test('region hash mismatch disables optional engine',()=>rejected({abi:{regionsSha256:'wrong'}}));
test('ABI revision mismatch disables optional engine',()=>rejected({abi:{revision:2}}));
test('ABI offset mismatch disables optional engine',()=>rejected({abi:{offsets:Array(16).fill(0)}}));
test('false admission reclaims partial table installation',()=>rejected({defs:[region(),region(1024,2)],installFail:2}));
test('throwing admission reclaims the just-created table slot',()=>rejected({defs:[region(),region(1024,2)],installThrow:2}));
test('duplicate region key is rejected',()=>rejected({defs:[region(),region(0,2)]}));
test('duplicate export index is rejected',()=>rejected({defs:[region(),region(1024,1)]}));
test('invalid region PC is rejected',()=>rejected({defs:[region(-1024,1)]}));
test('invalid mask value is rejected',()=>rejected({defs:[{...region(),masks:[null,...Array(15).fill(0)]}]}));
test('non-boolean instruction state is rejected',()=>rejected({defs:[{...region(),thumb:'false'}]}));
test('non-function export disables optional engine',()=>rejected({badExport:true}));
test('first buffer allocation failure falls back without throwing',()=>rejected({mallocFail:1}));
test('second buffer allocation failure reclaims first buffer',async()=>{const x=await rejected({mallocFail:2});assert.equal(x.allocated.size,0);});
test('older asynchronous installation cannot survive two resets',async()=>{
  const x=setup({delayed:true}),c=x.create();await tick();assert.equal(x.waiting.length,1);
  const first=c.reset();await tick();const second=c.reset();await tick();assert.equal(x.waiting.length,3);
  x.waiting[1]();await first;assert.equal(x.active.size,0);
  x.waiting[0]();await c.ready;assert.equal(x.active.size,0);
  x.waiting[2]();await second;assert.equal(x.active.size,1);assert.equal(x.mode,1);
  const next=c.reset();assert.equal(x.active.size,0);assert.equal(x.mode,0);await tick();x.waiting[3]();await next;assert.equal(x.active.size,1);
});
function worker(x,Controller=x.sandbox.Controller) {
  let channels=0;const replies=[];
  x.sandbox.SeedJIT=Controller;x.sandbox.importScripts=()=>{};
  x.sandbox.postMessage=m=>replies.push(m);x.sandbox.createEmu=async()=>x.api;
  x.sandbox.assets={assetBytes:async()=>new Uint8Array(8)};
  x.sandbox.MessageChannel=class {constructor(){const capture=channels++===0;this.port1={onmessage:null};this.port2={postMessage:()=>{if(capture)queueMicrotask(()=>this.port1.onmessage?.({data:0}));}};}};
  const dependency="const {assetBytes} = await import('./assets.js');";
  assert(workerSource.includes(dependency));
  // Only the firmware transport import is replaced; lifecycle handlers are real.
  const source=workerSource.replace(dependency,'const {assetBytes} = globalThis.assets;');
  vm.runInContext(source+'\nglobalThis.workerStart=start;globalThis.workerJit=()=>jit;',x.sandbox);
  return {replies,start:()=>x.sandbox.workerStart({id:1,cpi:1}),message:m=>x.sandbox.onmessage({data:m})};
}
test('actual worker startup is independent of a stalled optional seed',async()=>{
  const x=setup({disabled:true});
  class StalledSeed {constructor(){this.ready=new Promise(()=>{});}reset(){}pump(){}stats(){return {};}}
  const w=worker(x,StalledSeed);await timeout(w.start(),500);assert(w.replies.some(m=>m.type==='ready'));
});
test('actual worker initializes and binds memory before installing the controller',async()=>{
  const x=setup({lateMemory:true}),w=worker(x);assert.equal(x.api.jitMemory,undefined);
  await w.start();await tick();assert(x.api.jitMemory?.buffer);assert.equal(x.mode,1);assert.equal(x.active.size,1);
  const init=x.events.findIndex(e=>e[0]==='ccall'&&e[1]==='emu_init');
  const enable=x.events.findIndex(e=>e[0]==='mode');assert(init>=0&&init<enable);
});
test('actual restore disables old table before loading guest and rejects old completion',async()=>{
  const x=setup({delayed:true}),w=worker(x);const started=w.start();await tick();assert.equal(x.waiting.length,1);x.waiting[0]();await started;
  const before=x.events.length;await w.message({type:'restore',id:2,buffer:new ArrayBuffer(8)});await tick();
  const events=x.events.slice(before),clear=events.findIndex(e=>e[0]==='reset'),load=events.findIndex(e=>e[0]==='ccall'&&e[1]==='emu_load_snapshot');assert(clear>=0&&clear<load);assert.equal(x.active.size,0);
  await w.message({type:'restore',id:3,buffer:new ArrayBuffer(8)});await tick();assert.equal(x.waiting.length,3);
  x.waiting[1]();await tick();assert.equal(x.active.size,0);x.waiting[2]();await tick();assert.equal(x.active.size,1);
  assert(w.replies.some(m=>m.type==='reply'&&m.id===2&&!m.error));assert(w.replies.some(m=>m.type==='reply'&&m.id===3&&!m.error));
});
for (const paused of [false,true]) test(`actual ${paused?'paused':'running'} capture preserves the clock anchor and derived tables`,async()=>{
  const x=setup(),w=worker(x);await w.start();await tick();
  const clockCalls=()=>x.events.filter(e=>e[0]==='api'&&e[1]==='emu_host_clock');
  const clocksBeforePause=clockCalls().length;
  await w.message({type:'pause',id:8,value:paused});
  assert.equal(clockCalls().length,clocksBeforePause+1);
  assert.equal(clockCalls().at(-1)[2],paused?0:1);
  const table=[...x.active.keys()],before=x.events.length,clocksBeforeCapture=clockCalls().length;
  await w.message({type:'snapshot',id:9});
  const captureEvents=x.events.slice(before);
  assert(captureEvents.some(e=>e[0]==='api'&&e[1]==='emu_snapshot_begin'));
  assert(captureEvents.some(e=>e[0]==='api'&&e[1]==='emu_snapshot_advance'));
  assert(captureEvents.some(e=>e[0]==='api'&&e[1]==='emu_free_snapshot'));
  assert.equal(clockCalls().length,clocksBeforeCapture,'ordinary capture must not discard elapsed foreground time by reanchoring');
  assert.deepEqual([...x.active.keys()],table);
  assert(!captureEvents.some(e=>e[0]==='reset'||e[0]==='remove'));
  assert(w.replies.some(m=>m.type==='reply'&&m.id===9&&m.buffer?.byteLength===16));
  await w.message({type:'pause',id:10,value:!paused});
  assert.equal(clockCalls().length,clocksBeforeCapture+1);
  assert.equal(clockCalls().at(-1)[2],paused?1:0);
});
