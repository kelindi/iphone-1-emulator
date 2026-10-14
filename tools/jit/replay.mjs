// Whole-machine equivalence: interpreter and generated code retire identical work.
import createEmu from '../../web/dist/emu.js';
import {gunzipSync} from 'node:zlib';
import {fileURLToPath} from 'node:url';
import {readFile} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import {basename,resolve} from 'node:path';
const repo=fileURLToPath(new URL('../../',import.meta.url));
const path=p=>resolve(repo,p);
const m=await createEmu({locateFile:p=>path('web/dist/'+p)});
const alloc=b=>{const p=m._malloc(b.length);if(!p)throw Error('allocation');m.HEAPU8.set(b,p);return p;};
const kernel=await readFile(path('web/firmware/kernel_1A543a.macho')),tree=await readFile(path('web/firmware/devicetree_m68ap.bin'));
const k=alloc(kernel),d=alloc(tree);
if(m._emu_init(k,kernel.length,d,tree.length,0,0,256))throw Error('init');m._free(k);m._free(d);
const manifest=JSON.parse(await readFile(path('web/manifest.json'),'utf8'));
let fixture;
if(process.env.FIXTURE_SNAPSHOT) fixture=await readFile(resolve(process.env.FIXTURE_SNAPSHOT));
else {
 const parts=Array.isArray(manifest.snapshot)?manifest.snapshot:[manifest.snapshot];
 if(!parts[0]) throw Error('A home snapshot is required');
 fixture=Buffer.concat(await Promise.all(parts.map(p=>readFile(/^https?:/.test(p)?path('web/firmware/'+basename(new URL(p).pathname)):path('web/'+p)))));
}
if(fixture[0]===0x1f && fixture[1]===0x8b) fixture=gunzipSync(fixture);
const s=alloc(fixture);
const compiled=await WebAssembly.compile(await readFile(path('web/jit/seed.wasm'))),defs=JSON.parse(await readFile(path('web/jit/regions.json'),'utf8'));
const expected=[],ids=[],mask=m._malloc(64),total=Number(process.env.INSTRUCTIONS||600e6);
if(!Number.isSafeInteger(total)||total<1||total>0xffffffff) throw Error('INSTRUCTIONS must be an integer in 1..4294967295');
console.log(JSON.stringify({fixtureSha256:createHash('sha256').update(fixture).digest('hex'),instructions:total,regions:defs.length}));
for(const enabled of [0,1]){
 m._emu_jit_reset();for(const fn of ids.splice(0))m.removeFunction(fn);
 if(m._emu_load_snapshot(s,fixture.length))throw Error('restore');
 if(enabled){const instance=await WebAssembly.instantiate(compiled,{env:{memory:m.jitMemory,stale:m._emu_jit_stale,span:m._emu_jit_span}});for(const r of defs){const fn=m.addFunction(instance.exports['b'+r.index],'iii');ids.push(fn);new Uint32Array(m.jitMemory.buffer,mask,16).set(r.masks);if(!m._emu_jit_install(r.pc,r.thumb?1:0,fn,mask))throw Error('install');}}
 m._emu_jit_enable(enabled);m._emu_touch(1,275,250);const start=performance.now();
 for(let done=0;done<total;){const count=Math.min(1e6,total-done);if(m._emu_run(count)!==count)throw Error('run');done+=count;if(done===5e6)m._emu_touch(0,275,250);if(done===300e6)m._emu_button(1,1);if(done===305e6)m._emu_button(1,0);
 if(done%100e6===0||done===total){const p=m._emu_save_snapshot(),length=m._emu_snapshot_size();if(!p||!length)throw Error('snapshot');const hash=createHash('sha256').update(m.HEAPU8.subarray(p,p+length)).digest('hex');m._emu_free_snapshot();console.log(JSON.stringify({enabled,done,hash,generated:m._emu_jit_stat(0),calls:m._emu_jit_stat(1),regions:m._emu_jit_stat(2),ms:performance.now()-start}));if(!enabled)expected.push(hash);else if(hash!==expected[Math.ceil(done/100e6)-1])throw Error('Full snapshot mismatch');}
 }
 if(enabled && m._emu_jit_stat(0)<=0) throw Error('Generated code never retired an instruction');
 if(!enabled && m._emu_jit_stat(0)!==0) throw Error('Interpreter control retired generated instructions');
}

m._emu_jit_reset();for(const fn of ids)m.removeFunction(fn);m._free(mask);m._free(s);
console.log('Full machine snapshot equivalence passed');
