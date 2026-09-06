/** Measure genuine Calculator button feedback; no guest or product changes.
 * BASE_URL=http://localhost:8080 ARTIFACT_DIR=.context/input-feedback/report
 * node web/tests/input-feedback.mjs
 * Requires a quiet host for timing. Canvas observation is not photon timing.
 */
import {chromium} from 'playwright';
import {persistCrops} from './packets.mjs';
import assert from 'node:assert/strict';
import {mkdir, readFile, writeFile,readdir,readlink,rename} from 'node:fs/promises';
import {resolve} from 'node:path';
import {createHash} from 'node:crypto';
import os from 'node:os';
const dir=resolve(process.env.ARTIFACT_DIR || '.context/input-delivery/run-1');
await mkdir(dir,{recursive:true});
const refs=JSON.parse(await readFile(new URL('../../web/tests/include/calculator-displays.json',import.meta.url)));
const referenceImages={};
for(const value of ['0','9']) referenceImages[value]={...refs.displays[value],url:'data:image/png;base64,'+(await readFile(new URL('../../web/tests/include/'+refs.displays[value].image,import.meta.url))).toString('base64')};
const report={date:new Date().toISOString(),url:process.env.BASE_URL || 'http://localhost:8080',host:{cpu:os.cpus()[0].model,cpus:os.cpus().length,memory:os.totalmem(),platform:os.platform(),release:os.release()},method:'Separate harness-only per-frame delivery diagnostic.30 real presses/key after one warmup pair; unchanged30ms pulse/650ms spacing, guest clock/autosave. Early actual worker frame receipt→original canvas put completion→legacy RAF observations. Same40x40 button threshold/full320x88<=2RGB exactalpha results. Source/canvas ROI byte equality and raw crops retained. Confirmation100ms after legacy RAF first result; no favorable-latency retry. Observer copying/analysis overhead recorded; not physical display timing or an uninstrumented optimization.',samples:[],warmup:[],assets:[],errors:[]};
const browser=await chromium.launch({executablePath:'/usr/bin/google-chrome',args:['--no-sandbox','--disable-dev-shm-usage']});
report.browser=browser.version();
const context=await browser.newContext({viewport:{width:1000,height:900},recordVideo:{dir,size:{width:1000,height:900}}});
await context.addInitScript({path:resolve('.context/input-delivery/observer.js')});
const browserCDP=await browser.newBrowserCDPSession();
async function processReceipt(label){const info=(await browserCDP.send('SystemInfo.getProcessInfo')).processInfo,all=[];for(const name of await readdir('/proc'))if(/^\d+$/.test(name)){try{const raw=await readFile('/proc/'+name+'/stat','utf8'),f=raw.slice(raw.lastIndexOf(')')+2).split(' ');all.push({pid:+name,ppid:+f[1],startTicks:f[19],exe:await readlink('/proc/'+name+'/exe')});}catch{}}const scope=new Set(info.map(x=>x.id));let changed=true;while(changed){changed=false;for(const p of all)if(scope.has(p.ppid)&&!scope.has(p.pid)){scope.add(p.pid);changed=true;}}report.processReceipts??=[];report.processReceipts.push({label,time:new Date().toISOString(),info,processes:all.filter(x=>scope.has(x.pid))});await writeFile(resolve(dir,'processes.tmp'),JSON.stringify(report.processReceipts,null,2));await rename(resolve(dir,'processes.tmp'),resolve(dir,'processes.json'));}
await processReceipt('before-navigation');
const page=await context.newPage();const began=performance.now();page.setDefaultTimeout(10000);
page.on('pageerror',e=>report.errors.push(e.message));
const expectedAssets={};
for(const name of ['dist/emu.wasm','jit/seed.wasm','app.js','emu-worker.js','seed-jit.js','manifest.json'])expectedAssets[name.split('/').at(-1)]=createHash('sha256').update(await readFile(new URL('../../web/'+name,import.meta.url))).digest('hex');
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
  const targets={};const hex=b=>[...new Uint8Array(b)].map(v=>v.toString(16).padStart(2,'0')).join('');
  for(const [value,ref]of Object.entries(references)){const image=new Image();image.src=ref.url;await image.decode();if(image.width!==320||image.height!==88)throw Error('Reference dimensions');const c=document.createElement('canvas');c.width=320;c.height=88;c.getContext('2d').drawImage(image,0,0);const bytes=c.getContext('2d').getImageData(0,0,320,88).data;if(hex(await crypto.subtle.digest('SHA-256',bytes))!==ref.sha256)throw Error('Reference identity');targets[value]=bytes;}
  window.deliveryObserver.configure(targets);
 },referenceImages);
 await tap(120,250);
 await page.waitForFunction(()=>window.deliveryObserver.match('0').passed,null,{timeout:30000});
 await page.waitForTimeout(1000);assert.ok(await page.evaluate(()=>window.deliveryObserver.match('0').passed));
 await page.screenshot({path:resolve(dir,'initial.png')});
 for(let round=0;round<=30;round++)for(const [key,value,x,y] of [['9','9',200,224],['C','0',200,437]]){
  await page.evaluate(({key,value,x,y})=>window.deliveryObserver.arm(key,value,x,y),{key,value,x,y});
  await tap(x,y);await page.waitForFunction(()=>window.deliveryObserver.result,null,{timeout:7000});
  const encodingStart=performance.now();const packet=await page.evaluate(()=>window.deliveryObserver.takeResult());const serialized=performance.now();const raw=await persistCrops(dir,packet,String(round).padStart(2,'0')+'-'+key);const result={round,videoSeconds:(performance.now()-began)/1000,...raw,transportMs:serialized-encodingStart,cropWriteMs:performance.now()-serialized};
  (round===0?report.warmup:report.samples).push(result);await save();
  assert.ok(!result.failed&&!result.timedOut&&result.confirmation.passed&&result.up!==null&&!result.errors.length,'Genuine result/confirmation or frame association failed');assert.ok(result.firstButton&&result.firstResult&&result.rafFirstButton&&result.rafFirstResult,'Required genuine frame evidence missing');
  if(round===1&&key==='9')await page.screenshot({path:resolve(dir,'nine.png')});
  await page.waitForTimeout(650);
 }
 const stats=values=>{const a=[...values].sort((a,b)=>a-b);return{count:a.length,medianMs:a.length?(a[Math.floor((a.length-1)/2)]+a[Math.floor(a.length/2)])/2:null,p95Ms:a.length>=30?a[Math.ceil(a.length*.95)-1]:null,maxMs:a.length?a.at(-1):null};};
 report.summary={};
 const fields={receiptButton:r=>r.firstButton.receivedAt-r.down,canvasButton:r=>r.firstButton.canvasDoneAt-r.down,rafButton:r=>r.rafFirstButton.at-r.down,canvasResult:r=>r.firstResult.canvasDoneAt-r.down,rafResult:r=>r.rafFirstResult.at-r.down,releaseCanvasResult:r=>r.firstResult.canvasDoneAt-r.up,releaseRafResult:r=>r.rafFirstResult.at-r.up};
 for(const key of ['9','C']){const rows=report.samples.filter(r=>r.key===key);report.summary[key]=Object.fromEntries(Object.entries(fields).map(([name,fn])=>[name,stats(rows.map(fn))]));}
 report.correctnessPassed=report.samples.length===60&&report.samples.every(r=>!r.timedOut&&r.confirmation.passed&&!r.errors.length)&&report.errors.length===0;
 report.latencyPassed=Object.values(report.summary).every(s=>s.canvasButton.count===30&&s.canvasButton.p95Ms<=150);
 report.observerErrors=await page.evaluate(()=>window.deliveryObserver.errors);assert.deepEqual(report.observerErrors,[]);
 report.jit=await page.evaluate(()=>window.iphone.metrics.jit);assert.ok(report.jit?.enabled&&report.jit.retired>0,'Generated execution required');
 await page.screenshot({path:resolve(dir,'complete.png')});report.passed=report.correctnessPassed; // Diagnostic completion does not depend on a favorable latency result.
}catch(e){report.errors.push(e.stack);report.passed=false;await page.screenshot({path:resolve(dir,'failure.png')}).catch(()=>{});}
finally{await Promise.allSettled(assetTasks);for(const [name,expected] of Object.entries(expectedAssets)){const rows=report.assets.filter(a=>new URL(a.url).pathname.endsWith('/'+name));if(!rows.length||rows.some(a=>a.sha256!==expected))report.errors.push('Actual served hash mismatch or absent: '+name);}
 report.passed=report.passed&&report.errors.length===0;try{await processReceipt('before-close');}catch(e){report.errors.push(e.message);report.passed=false;}await context.close();report.video=await page.video().path();await browser.close();await save();}
console.log(JSON.stringify({passed:report.passed,correctnessPassed:report.correctnessPassed,latencyPassed:report.latencyPassed,summary:report.summary,errors:report.errors},null,2));
if(!report.passed)process.exitCode=1;
