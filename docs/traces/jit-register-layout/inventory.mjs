// Static opportunity inventory only; does not modify or execute generated code.
import {readFile,writeFile} from 'node:fs/promises';
import {gunzipSync} from 'node:zlib';
import {createRequire} from 'node:module';
import {createHash} from 'node:crypto';
const require=createRequire(import.meta.url);
const compiler=require('../../tools/jit/compiler.cjs');
const abi=JSON.parse(await readFile('web/jit/abi.json','utf8'));
compiler.configure(abi.offsets);
const input=JSON.parse(gunzipSync(await readFile('tools/jit/seed-input.json.gz')));
const regions=[];
for(const region of input.regions){
 const result=compiler.compileRegion({...region,ops:new Map(region.ops)});
 let wat=result.wat;
 for(let n=0;n<16;n++){
  const load=`(local.set $r${n} (i32.load offset=${abi.offsets[0]+n*4} (local.get $c)))`;
  const spill=`(i32.store offset=${abi.offsets[0]+n*4} (local.get $c)(local.get $r${n}))`;
  if(!wat.includes(load)||!wat.includes(spill))throw Error('Emitter boilerplate changed');
  wat=wat.replace(load,'').replace(spill,'');
 }
 const reads=new Set([...wat.matchAll(/\(local.get \$r(\d+)\)/g)].map(m=>Number(m[1])));
 const writes=new Set([...wat.matchAll(/\(local.set \$r(\d+) /g)].map(m=>Number(m[1])));
 const all=Array.from({length:16},(_,n)=>n);
 regions.push({index:region.index,pc:region.pc,thumb:region.thumb,
  readRegisters:[...reads].sort((a,b)=>a-b),writtenRegisters:[...writes].sort((a,b)=>a-b),
  neverWritten:all.filter(n=>!writes.has(n)),neverReferenced:all.filter(n=>!reads.has(n)&&!writes.has(n))});
}
const report={scope:'Static region-wide register references under the unchanged original compiler/input. No runtime weighting, compiler optimization measurement, speed estimate, policy change or generated execution.',
 compilerSha256:createHash('sha256').update(await readFile('tools/jit/compiler.cjs')).digest('hex'),
 regions:regions.length,totalNominalSpills:regions.length*16,
 regionRegisterPairsNeverWritten:regions.reduce((n,r)=>n+r.neverWritten.length,0),
 regionRegisterPairsNeverReferenced:regions.reduce((n,r)=>n+r.neverReferenced.length,0),
 details:regions};
await writeFile('.context/jit-register-layout/inventory.json',JSON.stringify(report,null,2)+'\n');
console.log(JSON.stringify({...report,details:undefined}));
