import {test} from 'node:test';
import assert from 'node:assert/strict';
import {mkdtemp,mkdir,writeFile,readFile,rm,stat} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {classifyPerformanceScreen} from './helpers/performance-home.mjs';
import {freezeFiles,verifyFreeze} from '../../tools/jit/adaptive/freeze-lib.mjs';
const frame=()=>new Uint8Array(320*480*4);
function fill(p,x,y,w,h,r,g,b){for(let row=y;row<y+h;row++)for(let col=x;col<x+w;col++){const i=(row*320+col)*4;p.set([r,g,b,255],i);}return p;}
test('lock screen cannot seed the Home timing reference',()=>{const p=frame();fill(p,50,130,220,240,0,40,200);fill(p,26,414,60,20,220,220,220);assert.equal(classifyPerformanceScreen(p).kind,'locked');});
test('Home requires both SMS and dock evidence, not a single green patch',()=>{const p=frame();fill(p,20,40,60,60,0,200,0);assert.equal(classifyPerformanceScreen(p).kind,'unknown');fill(p,90,405,60,60,0,60,200);assert.equal(classifyPerformanceScreen(p).kind,'home');});
test('black screen and wrong dimensions fail Home recognition',()=>{assert.equal(classifyPerformanceScreen(frame()).kind,'asleep');assert.throws(()=>classifyPerformanceScreen(new Uint8Array(4)),/320x480/);});
test('frozen files survive source edits, reject overwrite, and detect artifact edits',async()=>{const p=await mkdtemp(join(tmpdir(),'iphone-baseline-'));try{const source=join(p,'worker.js'),dest=join(p,'frozen');await writeFile(source,'original');const rows=await freezeFiles([{source,relative:'site/worker.js'}],dest);assert.notEqual((await stat(source)).ino,(await stat(join(dest,'site/worker.js'))).ino);await writeFile(source,'concurrent change');assert.equal(await readFile(join(dest,'site/worker.js'),'utf8'),'original');assert.equal(await verifyFreeze(dest,rows),1);await assert.rejects(freezeFiles([],dest),/EEXIST/);await writeFile(join(dest,'site/worker.js'),'changed frozen artifact');await assert.rejects(verifyFreeze(dest,rows),/Frozen file changed/);}finally{await rm(p,{recursive:true,force:true});}});
test('freeze rejects destination escape',async()=>{const p=await mkdtemp(join(tmpdir(),'iphone-baseline-'));try{const source=join(p,'source');await writeFile(source,'x');await assert.rejects(freezeFiles([{source,relative:'../escaped'}],join(p,'frozen')),/Invalid freeze destination/);}finally{await rm(p,{recursive:true,force:true});}});

// Exercise the public summarizer against malformed evidence, not its arithmetic.
test('summary rejects duplicate rounds and missing first action',async()=>{
 const {execFileSync}=await import('node:child_process');
 const p=await mkdtemp(join(tmpdir(),'iphone-summary-'));
 try {
  const report={passed:true,preparation:{verified:{kind:'home'}},servedAssets:['index.html','emu-worker.js','dist/emu.wasm','jit/seed.wasm'].map(name=>({name,matches:true,expected:'a'.repeat(64)})),actions:[]};
  for(let round=0;round<31;round++)for(const kind of ['settings','home'])report.actions.push({round,kind,releaseToReadyMs:123});
  const file=join(p,'report.json'),run=()=>execFileSync(process.execPath,['tools/jit/adaptive/summarize-baseline.mjs',file],{stdio:'pipe'});
  await writeFile(file,JSON.stringify(report));assert.equal(JSON.parse(run()).actions.settings.warmSamples,30);
  report.actions[2].round=2;await writeFile(file,JSON.stringify(report));assert.throws(run);
  report.actions[2].round=1;report.actions[0].round=31;await writeFile(file,JSON.stringify(report));assert.throws(run);
 }finally{await rm(p,{recursive:true,force:true});}
});
