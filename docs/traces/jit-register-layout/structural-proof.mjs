import {readFile,writeFile} from 'node:fs/promises';
import {gunzipSync} from 'node:zlib';
import {createRequire} from 'node:module';
import assert from 'node:assert/strict';
const require=createRequire(import.meta.url);
const original=require('../../tools/jit/compiler.cjs');
const candidate=require('./compiler.cjs');
const abi=JSON.parse(await readFile('web/jit/abi.json','utf8'));
original.configure(abi.offsets); candidate.configure(abi.offsets);
const input=JSON.parse(gunzipSync(await readFile('tools/jit/seed-input.json.gz')));
let removedLoads=0,removedSpills=0;
for(const region of input.regions) {
 const entry={...region,ops:new Map(region.ops)};
 const before=original.compileRegion(entry),after=candidate.compileRegion(entry);
 assert.deepEqual({...before,wat:null},{...after,wat:null});
 let body=before.wat;
 const transfers=Array.from({length:16},(_,n)=>({
  load:`(local.set $r${n} (i32.load offset=${abi.offsets[0]+n*4} (local.get $c)))`,
  spill:`(i32.store offset=${abi.offsets[0]+n*4} (local.get $c)(local.get $r${n}))`
 }));
 for(const {load,spill} of transfers) {
  assert.equal(body.split(load).length,2);assert.equal(body.split(spill).length,2);
  body=body.replace(load,'').replace(spill,'');
 }
 let expected=before.wat;
 for(let n=0;n<16;n++) {
  const reads=body.includes(`(local.get $r${n})`);
  const writes=body.includes(`(local.set $r${n} `);
  if(!writes) {
   expected=expected.replace(transfers[n].spill,'');removedSpills++;
   if(!reads) {expected=expected.replace(transfers[n].load,'');removedLoads++;}
  }
 }
 assert.equal(after.wat,expected);
 // Strip all retained transfers: every guest instruction and every guard must
 // remain textually identical, including placement, conditions and writeback.
 let afterBody=after.wat;
 for(const {load,spill} of transfers) afterBody=afterBody.replace(load,'').replace(spill,'');
 assert.equal(afterBody,body);
}
assert.equal(removedLoads,1580);assert.equal(removedSpills,1980);
assert.deepEqual(await readFile('web/jit/regions.json'),await readFile('.context/jit-register-layout/seed/regions.json'));
const report={passed:true,regions:input.regions.length,removedLoads,removedSpills,
 scope:'Exact non-transfer WAT and all compiler metadata unchanged for every original region. Admission metadata byte-identical. This is source structure proof, not a claim about host optimizer behavior or speed.'};
await writeFile('.context/jit-register-layout/structural-proof.json',JSON.stringify(report,null,2)+'\n');
console.log(JSON.stringify(report));
