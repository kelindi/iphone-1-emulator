import test from 'node:test';
import assert from 'node:assert/strict';
import vm from 'node:vm';
import fs from 'node:fs';
import {createHash, webcrypto} from 'node:crypto';
const source=fs.readFileSync(process.env.LOADER_SOURCE || new URL('./seed-jit.js',import.meta.url),'utf8');
const binary=Uint8Array.from([0,97,115,109,1,0,0,0,1,5,1,96,0,1,127,3,2,1,0,7,5,1,1,102,0,0,10,6,1,4,0,65,42,11]);
const sha=b=>createHash('sha256').update(b).digest('hex');
function setup(options={}) {
 const events=[], responses=[], pending=[];let timerMs;
 const defs=Buffer.from(options.defsText??'[]');
 const abi={revision:1,offsets:Array.from({length:16},(_,i)=>i),moduleSha256:options.badBinaryHash?'0'.repeat(64):sha(binary),regionsSha256:options.badDefsHash?'0'.repeat(64):sha(defs)};
 const sandbox={URL,Uint8Array,Uint32Array,ArrayBuffer,TextDecoder,TextEncoder,AbortController,TypeError,console,
  location:{href:'http://phone.test/emu-worker.js'+(options.disabled?'?jit=0':'')},
  crypto:{subtle:{async digest(type,bytes){events.push('hash');if(options.stalledHash)return new Promise(resolve=>pending.push(()=>resolve(webcrypto.subtle.digest(type,bytes))));return webcrypto.subtle.digest(type,bytes);}}},
  setTimeout(fn,ms){timerMs=ms;return setTimeout(fn,options.fastTimeout?15:ms);},clearTimeout,
  WebAssembly:{async compile(bytes){events.push('binaryCompile');assert.equal(sha(Buffer.from(bytes)),sha(binary));return WebAssembly.compile(bytes);},
   instantiate:WebAssembly.instantiate,
   ...(options.noStreaming?{}:{async compileStreaming(response){events.push('streamCompile');assert.equal(events.filter(x=>x==='hash').length,2,'compile before both verified hashes');assert.equal(response,responses.find(x=>x.kind==='seed').response,'must retain actual original Response');assert.equal(response.bodyUsed,false,'original body already consumed');if(options.streamStall)return new Promise(resolve=>pending.push(async()=>resolve(await WebAssembly.compile(binary))));if(options.streamFailure==='type')throw new TypeError('unsupported streaming');if(options.streamFailure==='compile')throw new WebAssembly.CompileError('invalid module');return WebAssembly.compileStreaming(response);}})},
  async fetch(path,init){events.push('fetch:'+path);if(options.stalledFetch)return new Promise((_,reject)=>init.signal.addEventListener('abort',()=>{events.push('fetchAbort');reject(Error('aborted'));}));
   const kind=path.endsWith('.wasm')?'seed':path.endsWith('regions.json')?'defs':'abi';
   if(options.lateFetch && kind==='seed')await new Promise(resolve=>pending.push(resolve));
   const bytes=kind==='seed'?(options.corruptBinary?new Uint8Array([1,2,3]):binary):kind==='defs'?defs:Buffer.from(JSON.stringify(abi));
   const response=new Response(bytes,{status:options.missing&&kind==='seed'?404:200,headers:{'Content-Type':kind==='seed'?(options.mime??'application/wasm'):'application/json',...(options.tooLargeHeader&&kind==='seed'?{'Content-Length':String(32*1024*1024+1)}:{})}});
   const cancel=response.body.cancel.bind(response.body);response.body.cancel=(...args)=>{events.push('cancel:'+kind);return cancel(...args);};
   if(options.noClone&&kind==='seed')response.clone=undefined;
   if(options.cloneThrows&&kind==='seed')response.clone=()=>{throw new TypeError('clone unavailable');};
   responses.push({kind,response});return response;
  }
 };
 vm.createContext(sandbox);vm.runInContext(source+'\nglobalThis.result=seedPromise;globalThis.readBounded=readSeedResponse;globalThis.owned=seedResponses;globalThis.abort=seedAbort;globalThis.limits=[SEED_MAX_BYTES,SEED_MAX_REGIONS,SEED_PREPARE_TIMEOUT_MS];',sandbox);
 return {sandbox,events,responses,pending,get timerMs(){return timerMs;}};
}
async function ready(s){const value=await s.sandbox.result;assert(!value.error,value.error?.stack);assert.equal(value[0].bytes,binary.length);assert.equal((await WebAssembly.instantiate(value[0].module)).exports.f(),42);assert.equal(s.sandbox.owned.size,0);return value;}
test('actual tiny WASM streams only after both hashes; original Response consumed, clone verified',async()=>{const s=setup();await ready(s);assert.equal(s.events.filter(x=>x==='streamCompile').length,1);assert(!s.events.includes('binaryCompile'));assert(s.responses.find(x=>x.kind==='seed').response.bodyUsed);assert.deepEqual(Array.from(s.sandbox.limits),[33554432,512,8000]);assert.equal(s.timerMs,8000);});
for(const [name,options]of Object.entries({unavailable:{noStreaming:true},wrongMime:{mime:'application/octet-stream'},absentClone:{noClone:true},cloneFailure:{cloneThrows:true}}))test('verified binary fallback: '+name,async()=>{const s=setup(options);await ready(s);assert(!s.events.includes('streamCompile'));assert(s.events.includes('binaryCompile'));});
test('streaming TypeError falls back using already verified bytes without refetch',async()=>{const s=setup({streamFailure:'type'});await ready(s);assert(s.events.includes('streamCompile'));assert(s.events.includes('binaryCompile'));assert.equal(s.events.filter(x=>x.startsWith('fetch:')).length,3);});
test('real streaming CompileError is retained, no duplicate compilation',async()=>{const s=setup({streamFailure:'compile'});const result=await s.sandbox.result;assert.match(result.error.message,/invalid module/);assert(!s.events.includes('binaryCompile'));assert.equal(s.sandbox.owned.size,0);});
for(const [name,options]of Object.entries({binaryHash:{badBinaryHash:true},definitionsHash:{badDefsHash:true},corruptBytes:{corruptBinary:true},invalidDefinitionsJson:{defsText:'{'},HTTP404:{missing:true},contentLengthCap:{tooLargeHeader:true}}))test('reject before compilation: '+name,async()=>{const s=setup(options);const result=await s.sandbox.result;assert(result.error);assert(!s.events.some(x=>/Compile$/.test(x)));assert(s.sandbox.abort.signal.aborted);assert.equal(s.sandbox.owned.size,0);});
test('disabled engine performs no fetch/compile and owns no responses',async()=>{const s=setup({disabled:true});assert.equal(s.sandbox.result,null);assert.deepEqual(s.events,[]);});
test('eight-second production bound aborts all stalled fetches',async()=>{const s=setup({stalledFetch:true,fastTimeout:true});const result=await s.sandbox.result;assert(result.error);assert.equal(s.timerMs,8000);assert.equal(s.events.filter(x=>x==='fetchAbort').length,3);assert.equal(s.sandbox.owned.size,0);});
test('late digest after timeout cannot start compilation',async()=>{const s=setup({stalledHash:true,fastTimeout:true});await s.sandbox.result;for(const resolve of s.pending)await resolve();await new Promise(r=>setTimeout(r,10));assert(!s.events.some(x=>/Compile$/.test(x)));assert.equal(s.sandbox.owned.size,0);});
test('late fetch after timeout is canceled instead of retained',async()=>{const s=setup({lateFetch:true,fastTimeout:true});await s.sandbox.result;s.pending[0]();await new Promise(r=>setTimeout(r,10));assert(!s.events.some(x=>/Compile$/.test(x)));assert(s.events.includes('cancel:seed'));assert.equal(s.sandbox.owned.size,0);});
test('late native compilation cannot replace timeout or trigger binary fallback',async()=>{const s=setup({streamStall:true,fastTimeout:true});const result=await s.sandbox.result;assert(result.error);for(const resolve of s.pending)await resolve();await new Promise(r=>setTimeout(r,10));assert.equal(await s.sandbox.result,result);assert(!s.events.includes('binaryCompile'));assert.equal(s.sandbox.owned.size,0);});
test('stream byte cap does not wait forever on an unread tee branch',async()=>{const s=setup({disabled:true});const response=new Response(new Uint8Array(17));const clone=response.clone();s.sandbox.owned.add(response);s.sandbox.owned.add(clone);const result=await Promise.race([s.sandbox.readBounded(clone,16).then(()=>null,e=>e),new Promise((_,reject)=>setTimeout(()=>reject(Error('tee cancellation deadlock')),100))]);assert.match(result.message,/byte cap/);await response.body.cancel();});
test('chunked bounded reader accepts the exact cap without trusting Content-Length',async()=>{const s=setup({disabled:true});const response=new Response(new ReadableStream({start(c){c.enqueue(new Uint8Array([1,2,3]));c.enqueue(new Uint8Array([4,5]));c.close();}}));s.sandbox.owned.add(response);const bytes=await s.sandbox.readBounded(response,5);assert.deepEqual(Array.from(new Uint8Array(bytes)),[1,2,3,4,5]);assert.equal(s.sandbox.owned.size,0);});
test('stream error releases its reader and owned response',async()=>{const s=setup({disabled:true});const response=new Response(new ReadableStream({pull(c){c.error(Error('transport failed'));}}));s.sandbox.owned.add(response);await assert.rejects(s.sandbox.readBounded(response,10),/transport failed/);assert.equal(response.body.locked,false);assert.equal(s.sandbox.owned.size,0);});
test('integrity failure cancels the unread original streaming branch',async()=>{const s=setup({badBinaryHash:true});assert((await s.sandbox.result).error);assert(s.responses.find(x=>x.kind==='seed').response.bodyUsed,'cancellation must disturb original post-tee branch');assert.equal(s.sandbox.owned.size,0);});
