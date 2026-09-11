// Real saved-phone API/lifetime smoke. Empty registry is deliberately not
// evidence of generated execution; the full compiler workload gate is separate.
import {readFile,writeFile} from 'node:fs/promises';
import {resolve} from 'node:path';
import {pathToFileURL} from 'node:url';
import {createHash} from 'node:crypto';
import assert from 'node:assert/strict';
const out=resolve(process.env.ARTIFACT_DIR||'.context/adaptive-compiler/workload-runtime');
const sha=b=>createHash('sha256').update(b).digest('hex');
const build=JSON.parse(await readFile(out+'/build-report.json'));assert(build.passed);
assert.equal(sha(await readFile(out+'/workload.wasm')),build.wasmSha256);
const frozen=JSON.parse(await readFile('docs/traces/adaptive-workloads/report.json'));
const baseline=JSON.parse(await readFile('.context/adaptive-compiler/baseline-final/freeze-manifest.json'));
const fixture=await readFile(process.env.FIXTURE_SNAPSHOT||'.context/adaptive-compiler/workload-home.snap');
assert.equal(sha(fixture),frozen.fixture.sha256);
const kernel=await readFile('web/firmware/kernel_1A543a.macho'),tree=await readFile('web/firmware/devicetree_m68ap.bin');
for(const [name,bytes]of [['kernel',kernel],['devicetree',tree]])assert.equal(sha(bytes),baseline.firmware.assetSha256[baseline.firmware[name]]);
const report={passed:false,scope:'Real saved-phone empty-registry API/restore smoke only; no compiled execution or speed claim.',wasmSha256:build.wasmSha256,fixtureSha256:sha(fixture),runs:[]};
try{
 const {default:create}=await import(pathToFileURL(out+'/workload.cjs'));
 const m=await create(),ptrs=[];
 const alloc=b=>{const p=m._malloc(b.length);assert(p);ptrs.push(p);m.HEAPU8.set(b,p);return p;};
 const k=alloc(kernel),d=alloc(tree),s=alloc(fixture);
 assert.equal(m._workload_layout(0),2);assert.equal(m._workload_layout(8),32);assert.equal(m._workload_layout(9),4);
 let previousEpoch=0, firstSnapshot;
 for(const enabled of [false,true]){
  // Reset derived MMU stamps identically, not just serialized guest state.
  assert.equal(m._emu_init(k,kernel.length,d,tree.length,0,0,256),0);
  assert.equal(m._emu_load_snapshot(s,fixture.length),0);
  assert.equal(m._workload_prepare(),1);assert.equal(m._adaptive_units(),0);
  const epoch=m._adaptive_epoch();assert(epoch>previousEpoch);previousEpoch=epoch;
  if(enabled)m._adaptive_enable(1);
  assert.equal(m._emu_run(10000),10000);assert.equal(m._emu_status(),0);assert.equal(m._adaptive_failed(),0);
  const snap=m._emu_save_snapshot(),bytes=m._emu_snapshot_size();assert(snap&&bytes);
  const snapshot=Buffer.from(m.HEAPU8.subarray(snap,snap+bytes));
  report.runs.push({enabled,epoch,credits:10000,bytes,sha256:sha(snapshot)});m._emu_free_snapshot();
  if(!firstSnapshot)firstSnapshot=snapshot;
  else {report.differenceCount=0;report.differences=[];for(let i=0;i<snapshot.length;i++)if(snapshot[i]!==firstSnapshot[i]){report.differenceCount++;if(report.differences.length<128)report.differences.push({offset:i,reference:firstSnapshot[i],adaptive:snapshot[i]});}}
 }
 assert.equal(report.runs[0].sha256,report.runs[1].sha256,'Complete real machine snapshot equality with empty adaptive registry');
 m._adaptive_enable(0);for(const p of ptrs)m._free(p);report.passed=true;
}catch(e){report.error=e.stack;process.exitCode=1;}
await writeFile(out+'/smoke-report.json',JSON.stringify(report,null,2)+'\n');console.log(JSON.stringify(report));
