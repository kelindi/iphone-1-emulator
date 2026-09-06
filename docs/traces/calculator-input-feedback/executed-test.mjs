/** Measure genuine Calculator button feedback; no guest or product changes.
 * BASE_URL=http://localhost:8080 ARTIFACT_DIR=.context/input-feedback/report
 * node web/tests/input-feedback.mjs
 * Requires a quiet host for timing. Canvas observation is not photon timing.
 */
import {chromium} from 'playwright';
import assert from 'node:assert/strict';
import {mkdir, readFile, writeFile} from 'node:fs/promises';
import {resolve} from 'node:path';
import {createHash} from 'node:crypto';
import os from 'node:os';
const dir=resolve(process.env.ARTIFACT_DIR || '.context/input-feedback/report');
await mkdir(dir,{recursive:true});
const refs=JSON.parse(await readFile(new URL('./include/calculator-displays.json',import.meta.url)));
const referenceImages={};
for(const value of ['0','9']) referenceImages[value]={...refs.displays[value],url:'data:image/png;base64,'+(await readFile(new URL('./include/'+refs.displays[value].image,import.meta.url))).toString('base64')};
const report={date:new Date().toISOString(),url:process.env.BASE_URL || 'http://localhost:8080',host:{cpu:os.cpus()[0].model,cpus:os.cpus().length,memory:os.totalmem(),platform:os.platform(),release:os.release()},method:'30 real presses per key;30ms requested pulse; actual page pointer events; new guest frame sampled in requestAnimationFrame.40x40 key crop changes>=16pixels by>12RGB. Full320x88 result matches independently reviewed reference within2RGB/exactalpha, confirmed100ms later.650ms post-result spacing. Normal guest clock/autosave. Canvas observation, not physical display timing.',samples:[],warmup:[],assets:[],errors:[]};
const browser=await chromium.launch({executablePath:'/usr/bin/google-chrome',args:['--no-sandbox','--disable-dev-shm-usage']});
report.browser=browser.version();
const context=await browser.newContext({viewport:{width:1000,height:900},recordVideo:{dir,size:{width:1000,height:900}}});
const page=await context.newPage();const began=performance.now();page.setDefaultTimeout(10000);
page.on('pageerror',e=>report.errors.push(e.message));
const expectedAssets={};
for(const name of ['dist/emu.wasm','jit/seed.wasm','app.js','emu-worker.js','seed-jit.js','manifest.json'])expectedAssets[name.split('/').at(-1)]=createHash('sha256').update(await readFile(new URL('../'+name,import.meta.url))).digest('hex');
report.expectedAssets=expectedAssets;
const assetTasks=[];
context.on('response',r=>{if(/\/(?:emu\.wasm|seed\.wasm|app\.js|emu-worker\.js|seed-jit\.js|manifest\.json)(?:\?|$)/.test(r.url()))assetTasks.push((async()=>{try{const b=await r.body();report.assets.push({url:r.url(),sha256:createHash('sha256').update(b).digest('hex'),bytes:b.length});}catch(e){report.errors.push(e.message);}})());});
const json=value=>JSON.stringify(value,(_,v)=>typeof v==='bigint'?v.toString():v,2);
assert.equal(JSON.parse(json({metrics:{jit:{retired:123n}}})).metrics.jit.retired,'123');
const save=()=>writeFile(resolve(dir,'report.json'),json(report)+'\n');
async function tap(x,y){const b=await page.locator('#screen').boundingBox();await page.mouse.move(b.x+x*b.width/320,b.y+y*b.height/480);await page.mouse.down();await page.waitForTimeout(30);await page.mouse.up();}
try{
 await page.goto(report.url);await page.waitForFunction(()=>window.iphone?.ready,null,{timeout:120000});
 await page.evaluate(async references=>{
  const canvas=document.querySelector('#screen'),ctx=canvas.getContext('2d');
  const targets={};
  const hex=bytes=>[...new Uint8Array(bytes)].map(n=>n.toString(16).padStart(2,'0')).join('');
  for(const [value,ref] of Object.entries(references)){
   const image=new Image();image.src=ref.url;await image.decode();
   if(image.width!==320||image.height!==88)throw Error('Reference dimensions');
   const c=document.createElement('canvas');c.width=320;c.height=88;c.getContext('2d').drawImage(image,0,0);
   targets[value]=c.getContext('2d').getImageData(0,0,320,88).data;
   if(hex(await crypto.subtle.digest('SHA-256',targets[value]))!==ref.sha256)throw Error('Reference identity');
  }
  function match(value){const raw=ctx.getImageData(0,20,320,88).data,ref=targets[value];let maxRGBDelta=0,alphaExact=true;
   for(let i=0;i<raw.length;i+=4){for(let ch=0;ch<3;ch++)maxRGBDelta=Math.max(maxRGBDelta,Math.abs(raw[i+ch]-ref[i+ch]));alphaExact&&=raw[i+3]===ref[i+3];}
   return{passed:maxRGBDelta<=2&&alphaExact,maxRGBDelta,alphaExact};
  }
  let active=null;
  const api=window.inputFeedback={match,result:null,arm(key,value,x,y){
   if(active)throw Error('Input still active');
   const previous=value==='9'?'0':'9';if(!match(previous).passed)throw Error('Wrong previous Calculator display');
   api.result=null;active={key,value,x,y,baseline:ctx.getImageData(x-20,y-20,40,40).data,armedAt:performance.now(),down:null,up:null,firstButton:null,firstResult:null,samples:[],lastFrame:canvas.dataset.frame};
  }};
  document.addEventListener('pointerdown',e=>{if(e.target===canvas&&active&&active.down===null)active.down=performance.now();},{capture:true});
  document.addEventListener('pointerup',e=>{if(e.target===canvas&&active&&active.up===null)active.up=performance.now();},{capture:true});
  function sample(){
   const a=active;if(a&&a.down!==null){const now=performance.now(),frame=canvas.dataset.frame;
    if(frame!==a.lastFrame){a.lastFrame=frame;const raw=ctx.getImageData(a.x-20,a.y-20,40,40).data;let changed=0;
     for(let i=0;i<raw.length;i+=4)if(Math.max(Math.abs(raw[i]-a.baseline[i]),Math.abs(raw[i+1]-a.baseline[i+1]),Math.abs(raw[i+2]-a.baseline[i+2]))>12)changed++;
     if(a.firstButton===null&&changed>=16)a.firstButton=now;
     const comparison=match(a.value);if(a.firstResult===null&&comparison.passed)a.firstResult=now;
     a.samples.push({at:now,frame,changed,comparison});
    }
    const timedOut=now-a.down>5000;
    if(timedOut||(a.up!==null&&a.firstResult!==null&&now-a.firstResult>=100)){
     const confirmation=match(a.value);api.result={key:a.key,expected:a.value,down:a.down,up:a.up,firstButton:a.firstButton,firstResult:a.firstResult,downToButtonMs:a.firstButton===null?null:a.firstButton-a.down,downToResultMs:a.firstResult===null?null:a.firstResult-a.down,releaseToResultMs:a.firstResult===null||a.up===null?null:a.firstResult-a.up,pulseMs:a.up===null?null:a.up-a.down,confirmation,timedOut,samples:a.samples,savedAt:window.iphone.savedAt,metrics:window.iphone.metrics};active=null;
    }
   }requestAnimationFrame(sample);
  }requestAnimationFrame(sample);
 },referenceImages);
 await tap(120,250);
 await page.waitForFunction(()=>window.inputFeedback.match('0').passed,null,{timeout:30000});
 await page.waitForTimeout(1000);assert.ok(await page.evaluate(()=>window.inputFeedback.match('0').passed));
 await page.screenshot({path:resolve(dir,'initial.png')});
 for(let round=0;round<=30;round++)for(const [key,value,x,y] of [['9','9',200,224],['C','0',200,437]]){
  await page.evaluate(({key,value,x,y})=>window.inputFeedback.arm(key,value,x,y),{key,value,x,y});
  await tap(x,y);await page.waitForFunction(()=>window.inputFeedback.result,null,{timeout:7000});
  const result={round,videoSeconds:(performance.now()-began)/1000,...await page.evaluate(()=>window.inputFeedback.result)};
  (round===0?report.warmup:report.samples).push(result);await save();
  assert.ok(!result.timedOut&&result.confirmation.passed&&result.up!==null,'Genuine result/confirmation failed');
  if(round===1&&key==='9')await page.screenshot({path:resolve(dir,'nine.png')});
  await page.waitForTimeout(650);
 }
 const stats=values=>{const a=[...values].sort((a,b)=>a-b);return{count:a.length,medianMs:a.length?(a[Math.floor((a.length-1)/2)]+a[Math.floor(a.length/2)])/2:null,p95Ms:a.length>=30?a[Math.ceil(a.length*.95)-1]:null,maxMs:a.length?a.at(-1):null};};
 report.summary={};
 for(const key of ['9','C']){const rows=report.samples.filter(r=>r.key===key);report.summary[key]={button:stats(rows.filter(r=>r.downToButtonMs!==null).map(r=>r.downToButtonMs)),result:stats(rows.map(r=>r.downToResultMs)),releaseResult:stats(rows.map(r=>r.releaseToResultMs)),missingButtonFeedback:rows.filter(r=>r.downToButtonMs===null).length};}
 report.correctnessPassed=report.samples.length===60&&report.samples.every(r=>!r.timedOut&&r.confirmation.passed)&&report.errors.length===0;
 report.latencyPassed=Object.values(report.summary).every(s=>s.button.count===30&&s.button.p95Ms<=150);
 report.jit=await page.evaluate(()=>window.iphone.metrics.jit);assert.ok(report.jit?.enabled&&report.jit.retired>0,'Generated execution required');
 await page.screenshot({path:resolve(dir,'complete.png')});report.passed=report.correctnessPassed&&report.latencyPassed;
}catch(e){report.errors.push(e.stack);report.passed=false;await page.screenshot({path:resolve(dir,'failure.png')}).catch(()=>{});}
finally{await Promise.allSettled(assetTasks);for(const [name,expected] of Object.entries(expectedAssets)){const rows=report.assets.filter(a=>new URL(a.url).pathname.endsWith('/'+name));if(!rows.length||rows.some(a=>a.sha256!==expected))report.errors.push('Actual served hash mismatch or absent: '+name);}
 report.passed=report.passed&&report.errors.length===0;await context.close();report.video=await page.video().path();await browser.close();await save();}
console.log(JSON.stringify({passed:report.passed,correctnessPassed:report.correctnessPassed,latencyPassed:report.latencyPassed,summary:report.summary,errors:report.errors},null,2));
if(!report.passed)process.exitCode=1;
