import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtemp,mkdir,writeFile,rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {cacheServer} from './cache-server.mjs';
test('actual HTTP serves frozen seed bytes, stable strong ETag, conditional304 and correct MIME/cache headers',async()=>{
 const dir=await mkdtemp(join(tmpdir(),'seed-http-'));let server;
 try{
  await mkdir(join(dir,'jit'));await writeFile(join(dir,'jit/seed.wasm'),new Uint8Array([0,97,115,109]));await writeFile(join(dir,'index.html'),'frozen shell');
  const result=await cacheServer(dir);server=result.server;await new Promise(r=>server.listen(0,'127.0.0.1',r));const origin='http://127.0.0.1:'+server.address().port;
  const first=await fetch(origin+'/jit/seed.wasm');assert.equal(first.status,200);assert.equal(first.headers.get('content-type'),'application/wasm');assert.equal(first.headers.get('cache-control'),'public, max-age=3600');assert.equal(first.headers.get('etag'),result.seed.etag);assert.deepEqual([...new Uint8Array(await first.arrayBuffer())],[0,97,115,109]);
  const conditional=await fetch(origin+'/jit/seed.wasm',{headers:{'If-None-Match':result.seed.etag}});assert.equal(conditional.status,304);assert.equal((await conditional.arrayBuffer()).byteLength,0);assert.equal(conditional.headers.get('etag'),result.seed.etag);
  await writeFile(join(dir,'jit/seed.wasm'),'changed on disk');const frozen=await fetch(origin+'/jit/seed.wasm');assert.deepEqual([...new Uint8Array(await frozen.arrayBuffer())],[0,97,115,109]);
  const head=await fetch(origin+'/jit/seed.wasm',{method:'HEAD'});assert.equal(head.headers.get('content-length'),'4');assert.equal((await head.arrayBuffer()).byteLength,0);
  const html=await fetch(origin+'/');assert.equal(html.headers.get('cache-control'),'no-cache');assert.equal(await html.text(),'frozen shell');
 }finally{if(server)await new Promise(r=>server.close(r));await rm(dir,{recursive:true,force:true});}
});
