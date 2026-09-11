import {test} from 'node:test';
import assert from 'node:assert/strict';
import {mkdtemp,writeFile,rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {createHash} from 'node:crypto';
import {finishPerformanceRun,loadPerformanceFixture,bounded} from './helpers/performance-lifecycle.mjs';

const pending=()=>new Promise(()=>{});
const report=()=>({passed:true,errors:[]});

test('setup failure without a browser still writes a failed report',async()=>{
  const r={passed:false,errors:['Invalid baseline JSON']};let written;
  await finishPerformanceRun({report:r,writeReport:value=>{written=value;}});
  assert.equal(written,r);assert.equal(r.passed,false);
  assert.deepEqual(r.cleanup.map(x=>x.status),['not-created','not-created']);
});

test('successful collection closes both resources and retains video/identity',async()=>{
  const r=report(),events=[];
  await finishPerformanceRun({report:r,assetTasks:[{name:'emu-worker.js',promise:Promise.resolve({name:'emu-worker.js',matches:true,sha256:'verified'})}],required:['emu-worker.js'],
    context:{close:()=>events.push('context')},browser:{close:()=>events.push('browser')},page:{video:()=>({path:()=>{events.push('video');return 'actual.webm';}})},writeReport:()=>events.push('write')});
  assert.deepEqual(events,['context','browser','video','write']);assert.equal(r.video,'actual.webm');assert.equal(r.passed,true);
});

test('hanging response is bounded while completed identities survive',async()=>{
  const r=report(),events=[];
  await finishPerformanceRun({report:r,bodyTimeoutMs:15,assetTasks:[
    {name:'ok.js',promise:Promise.resolve({name:'ok.js',matches:true})},
    {name:'stalled.wasm',promise:pending()}],context:{close:()=>events.push('context')},browser:{close:()=>events.push('browser')},writeReport:()=>events.push('write')});
  assert.equal(r.passed,false);assert.equal(r.servedAssets[0].matches,true);
  assert.match(r.servedAssets[1].error,/timed out/);assert.deepEqual(events,['context','browser','write']);
});

test('failed context close cannot skip browser close, video failure or report',async()=>{
  const r=report(),events=[];
  await finishPerformanceRun({report:r,context:{close:()=>{events.push('context');throw Error('context error');}},browser:{close:()=>events.push('browser')},
    page:{video:()=>({path:()=>{throw Error('video error');}})},writeReport:()=>events.push('write')});
  assert.deepEqual(events,['context','browser','write']);assert.equal(r.passed,false);
  assert.equal(r.cleanup[0].status,'failed');assert.equal(r.cleanup[1].status,'closed');
  assert(r.errors.some(x=>x.includes('context error')));assert(r.errors.some(x=>x.includes('video error')));
});

test('hung closes and video are failures, never successful closure receipts',async()=>{
  const r=report();let writes=0;
  await finishPerformanceRun({report:r,closeTimeoutMs:10,videoTimeoutMs:10,
    context:{close:pending},browser:{close:pending},page:{video:()=>({path:pending})},writeReport:()=>writes++});
  assert.equal(writes,1);assert.equal(r.passed,false);
  assert.deepEqual(r.cleanup.map(x=>x.status),['failed','failed']);assert.equal(r.video,undefined);
});

test('missing required identity and mismatched body fail the report',async()=>{
  const r=report();
  await finishPerformanceRun({report:r,required:['missing.js'],assetTasks:[{name:'wrong.js',promise:Promise.resolve({name:'wrong.js',matches:false})}],writeReport:()=>{}});
  assert.equal(r.passed,false);assert(r.errors.some(x=>x.includes('Missing served identity: missing.js')));
});

test('failed report write is propagated after cleanup attempts',async()=>{
  const events=[];
  await assert.rejects(finishPerformanceRun({report:report(),context:{close:()=>events.push('context')},browser:{close:()=>events.push('browser')},writeReport:()=>{events.push('write');throw Error('disk full');}}),/disk full/);
  assert.deepEqual(events,['context','browser','write']);
});

test('fixture hash covers exact upload bytes despite later source mutation',async()=>{
  const dir=await mkdtemp(join(tmpdir(),'performance-fixture-'));
  try {
    const file=join(dir,'phone.snap'),bytes=Buffer.from(Array.from({length:256},(_,i)=>i));await writeFile(file,bytes);
    const result=await loadPerformanceFixture(file);await writeFile(file,'replacement');
    assert.deepEqual(result.upload.buffer,bytes);assert.equal(result.metadata.bytes,256);
    assert.equal(result.metadata.sha256,createHash('sha256').update(result.upload.buffer).digest('hex'));
    assert.equal(result.upload.name,'phone.snap');assert.equal(result.metadata.path,file);
  } finally {await rm(dir,{recursive:true,force:true});}
});

test('bounded operations preserve original rejections',async()=>{
  await assert.rejects(bounded(()=>{throw Error('original failure');},100,'operation'),/original failure/);
});
