import test from 'node:test';import assert from 'node:assert/strict';import {readFileSync} from 'node:fs';import {createHash} from 'node:crypto';import {encodeBytes,decodeFrame} from './frame-packet.mjs';
const packet=b=>({base64:encodeBytes(b),sha256:createHash('sha256').update(b).digest('hex')});
test('all256 byte values roundtrip exactly',()=>{const b=Uint8Array.from({length:256},(_,i)=>i);assert.deepEqual(decodeFrame(packet(b),256),Buffer.from(b));});
for(const label of ['C0','H1'])for(const kind of ['settings','home'])test('actual originalRGBA roundtrip '+label+' '+kind,()=>{const b=readFileSync('.context/jit-return-visit/references/'+label+'-'+kind+'.rgba');assert.deepEqual(decodeFrame(packet(b)),b);});
test('legal-base64 byte corruption fails full identity even in statusrow',()=>{const b=Buffer.alloc(614400,20),p=packet(b);b[0]++;p.base64=encodeBytes(b);assert.throws(()=>decodeFrame(p),/identity/);});
test('wronghash and shape reject',()=>{const p=packet(Buffer.alloc(614400));assert.throws(()=>decodeFrame({...p,sha256:'0'.repeat(64)}),/identity/);assert.throws(()=>decodeFrame(p,1),/length/);});
test('malformedbase64 rejects instead of permissive decoder truncation',()=>{const p=packet(Buffer.alloc(614400));for(const bad of [p.base64+'!',p.base64.slice(1),'====','A==='])assert.throws(()=>decodeFrame({...p,base64:bad}),/encoding/);});
