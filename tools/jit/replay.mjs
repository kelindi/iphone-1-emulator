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
const mask=m._malloc(64),expected=[],ids=[],total=Number(process.env.INSTRUCTIONS||600e6);
if(!Number.isSafeInteger(total)||total<1||total>0xffffffff) throw Error('INSTRUCTIONS must be an integer in 1..4294967295');
console.log(JSON.stringify({fixtureSha256:createHash('sha256').update(fixture).digest('hex'),instructions:total,regions:defs.length}));
for(const enabled of (process.env.PASSES||'0,1').split(',').map(Number)){
 m._emu_jit_reset();for(const fn of ids.splice(0))m.removeFunction(fn);
 if(m._emu_load_snapshot(s,fixture.length))throw Error('restore');
 // PASSES: 0 interpreter, 1 seed, 2 compiled while running, 3 both.
 const table=new WebAssembly.Table({element:'anyfunc',initial:enabled&1?defs.length:0}),env={memory:m.jitMemory,stale:m._emu_jit_stale,refill:m._emu_jit_refill,link:m._emu_jit_link(),table};let runner=false,live=0;
 const pump=()=>{while(m._emu_jit_compile_hot(16,table.length)){const len=m._emu_jit_compiled(1)>>>0;if(!len)continue;const at=m._emu_jit_compiled(0)>>>0,n=m._emu_jit_compiled(3)>>>0,d=m._emu_jit_compiled(2)>>>2;table.grow(n);const inst=new WebAssembly.Instance(new WebAssembly.Module(m.HEAPU8.slice(at,at+len)),{env});if(!runner){const fn=m.addFunction(inst.exports.run,'iiii');ids.push(fn);if(!m._emu_jit_runner(fn))throw Error('runner');runner=true;}for(let i=0;i<n;i++){const r=m.HEAPU32.slice(d+i*19,d+i*19+19);new Uint32Array(m.jitMemory.buffer,mask,16).set(r.subarray(3));if(!m._emu_jit_install(r[0],r[1],r[2],mask))throw Error('runtime install');}live+=n;}m._emu_jit_compiled_free();};
 if(enabled&2)m._emu_jit_hot(1);
 if(enabled&1){runner=true;const instance=await WebAssembly.instantiate(compiled,{env});const fn=m.addFunction(instance.exports.run,'iiii');ids.push(fn);if(!m._emu_jit_runner(fn))throw Error('runner');for(const r of defs){new Uint32Array(m.jitMemory.buffer,mask,16).set(r.masks);if(!m._emu_jit_install(r.pc,r.thumb?1:0,r.index,mask))throw Error('install '+r.index+' '+r.pc.toString(16)+' '+r.thumb);}}
 m._emu_jit_enable(enabled?1:0);m._emu_touch(1,275,250);const start=performance.now();let runMs=0;
 for(let done=0;done<total;){const count=Math.min(1e6,total-done),t0=performance.now();if(m._emu_run(count)!==count)throw Error('run');if(enabled&2)pump();runMs+=performance.now()-t0;done+=count;if(done===5e6)m._emu_touch(0,275,250);if(done===300e6)m._emu_button(1,1);if(done===305e6)m._emu_button(1,0);
 if(done%100e6===0||done===total){const p=m._emu_save_snapshot(),length=m._emu_snapshot_size();if(!p||!length)throw Error('snapshot');const hash=createHash('sha256').update(m.HEAPU8.subarray(p,p+length)).digest('hex');m._emu_free_snapshot();console.log(JSON.stringify({enabled,done,hash,generated:m._emu_jit_stat(0),calls:m._emu_jit_stat(1),regions:m._emu_jit_stat(2),compiledLive:live,ms:performance.now()-start,runMs}));if(!enabled)expected.push(hash);else if(hash!==expected[Math.ceil(done/100e6)-1])throw Error('Full snapshot mismatch');}
 }
 if(enabled && m._emu_jit_stat(0)<=0) throw Error('Generated code never retired an instruction');
 if(!enabled && m._emu_jit_stat(0)!==0) throw Error('Interpreter control retired generated instructions');
}

m._emu_jit_reset();for(const fn of ids)m.removeFunction(fn);m._free(s);
console.log('Full machine snapshot equivalence passed');
