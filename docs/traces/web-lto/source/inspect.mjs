import {readFile,writeFile} from 'node:fs/promises';
import {resolve} from 'node:path';
import {pathToFileURL} from 'node:url';
import {createHash} from 'node:crypto';
const p=resolve('.context/web-lto'),wanted=JSON.parse(await readFile('web/jit/abi.json','utf8')),out={};
for(const kind of ['control','candidate']){
 const binary=await readFile(`${p}/${kind}/emu.wasm`),compiled=await WebAssembly.compile(binary),factory=(await import(pathToFileURL(`${p}/${kind}/emu.js`))).default;
 const m=await factory({locateFile:f=>`${p}/${kind}/${f}`});
 const offsets=Array.from({length:16},(_,i)=>m._emu_jit_offset(i)),revision=m._emu_jit_abi();
 if(JSON.stringify(offsets)!==JSON.stringify(wanted.offsets)||revision!==wanted.revision)throw Error('JIT ABI differs');
 const functions=Object.keys(m).filter(x=>x.startsWith('_')&&typeof m[x]==='function').sort();
 out[kind]={bytes:binary.length,sha256:createHash('sha256').update(binary).digest('hex'),offsets,revision,functions,wasmImports:WebAssembly.Module.imports(compiled),wasmExports:WebAssembly.Module.exports(compiled),memoryBytes:m.HEAPU8.byteLength};
}
if(JSON.stringify(out.control.functions)!==JSON.stringify(out.candidate.functions))throw Error('Public JS function exports differ');
if(out.control.memoryBytes!==out.candidate.memoryBytes)throw Error('Initial memory differs');
// Internal minified import/export names may change under whole-program optimization.
const seed=await WebAssembly.compile(await readFile('web/jit/seed.wasm'));out.seedImports=WebAssembly.Module.imports(seed);out.passed=true;out.scope='No emulator initialization or guest instructions. Public API, live JIT ABI and initial memory equality. Internal symbol identities are not assumed.';
await writeFile(`${p}/structure.json`,JSON.stringify(out,null,2));console.log(JSON.stringify({passed:true,controlBytes:out.control.bytes,candidateBytes:out.candidate.bytes,publicFunctions:out.control.functions.length,abi:out.candidate.revision,offsetsExact:true}));
