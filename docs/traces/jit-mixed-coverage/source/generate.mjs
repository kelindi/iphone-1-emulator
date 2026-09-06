import {readFile,writeFile} from 'node:fs/promises';
import {gunzipSync} from 'node:zlib';
import {createHash} from 'node:crypto';
import {createRequire} from 'node:module';
import {pathToFileURL} from 'node:url';
import {resolve} from 'node:path';
import assert from 'node:assert/strict';
const require=createRequire(import.meta.url),compiler=require('./original/compiler.cjs'),wabt=await require('wabt')(),p=resolve('.context/jit-mixed-coverage');
const {default:create}=await import(pathToFileURL(p+'/candidate/dist/emu.js'));const m=await create({locateFile:x=>p+'/candidate/dist/'+x});
const abi=JSON.parse(await readFile(p+'/original/abi.json')),offsets=Array.from({length:16},(_,i)=>m._emu_jit_offset(i));assert.equal(m._emu_jit_abi(),abi.revision);assert.deepEqual(offsets,abi.offsets);compiler.configure(offsets);
const originals=new Map(),reports=[];const sha=b=>createHash('sha256').update(b).digest('hex');let previous;
function bodies(bytes){let p=8;const u=()=>{let n=0,s=0,b;do{b=bytes[p++];n|=(b&127)<<s;s+=7;}while(b&128);return n>>>0;};while(p<bytes.length){const id=bytes[p++],len=u(),end=p+len;if(id===10){const count=u(),out=[];for(let i=0;i<count;i++){const n=u();out.push(bytes.subarray(p,p+n));p+=n;}assert.equal(p,end);return out;}p=end;}throw Error('Missing code section');}
for(const variant of ['original','candidate']){
 const input=JSON.parse(gunzipSync(await readFile(p+'/'+variant+'/seed-input.json.gz')));assert(input.regions.length<=576);
 const funcs=[],defs=[],bodyComparison=[];
 for(const r of input.regions){const b=compiler.compileRegion({...r,ops:new Map(r.ops)});assert(b);funcs.push(b.wat);const masks=b.masks.map(v=>v>>>0);defs.push({pc:r.pc,thumb:r.thumb,index:r.index,masks});if(variant==='original')originals.set(r.index,{wat:b.wat,masks});else if(r.index<512){const old=originals.get(r.index);for(let i=0;i<16;i++)assert.equal((old.masks[i]&~masks[i])>>>0,0);bodyComparison.push({index:r.index,watByteIdentical:old.wat===b.wat});}}
 const wat=`(module(import "env" "memory"(memory 8192))(import "env" "stale"(func $stale(param i32 i32)))(import "env" "span"(func $span(param i32 i32 i32 i32 i32)(result i32)))${funcs.join('')})`;
 const parsed=wabt.parseWat('seed.wat',wat);let binary;try{parsed.validate();binary=Buffer.from(parsed.toBinary({}).buffer);}finally{parsed.destroy();}
 const code=bodies(binary),maxBody=Math.max(...code.map(x=>x.length));assert(binary.length<=16*1024*1024);assert(maxBody<=80*1024);
 if(variant==='original'){assert.equal(sha(binary),abi.moduleSha256);previous=code;}else{
  assert.equal(bodyComparison.filter(x=>x.watByteIdentical).length,441);for(const row of bodyComparison){row.wasmBodyByteIdentical=previous[row.index].equals(code[row.index]);assert.equal(row.watByteIdentical,row.wasmBodyByteIdentical);}
 }
 const definitions=Buffer.from(JSON.stringify(defs)+'\n'),metadata={revision:m._emu_jit_abi(),offsets,moduleSha256:sha(binary),regionsSha256:sha(definitions)};
 if(variant==='candidate'){await writeFile(p+'/candidate/seed.wasm',binary);await writeFile(p+'/candidate/regions.json',definitions);await writeFile(p+'/candidate/abi.json',JSON.stringify(metadata,null,2)+'\n');}
 reports.push({variant,regions:defs.length,seedBytes:binary.length,seedSha256:sha(binary),maxBodyBytes:maxBody,codeBytes:code.reduce((s,x)=>s+x.length,0),bodyComparison});
}
await writeFile(p+'/generation.json',JSON.stringify({passed:true,compilerSha256:sha(await readFile(p+'/original/compiler.cjs')),abiOffsetsUnchanged:true,reports},null,2)+'\n');console.log(JSON.stringify(reports.map(({bodyComparison,...r})=>({...r,unchangedOriginalBodies:bodyComparison?.filter(x=>x.wasmBodyByteIdentical).length}))));
