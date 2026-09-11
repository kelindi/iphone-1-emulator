// Interactive original-touchscreen qualification, not a speed benchmark.
import {chromium} from 'playwright';
import {mkdir,writeFile,readFile,statfs,unlink} from 'node:fs/promises';
import {createReadStream} from 'node:fs';
import {createServer} from 'node:http';
import {createInterface} from 'node:readline';
import {resolve,extname,basename} from 'node:path';
import {pathToFileURL} from 'node:url';
import {createHash} from 'node:crypto';
import {bounded,loadPerformanceFixture} from '../../../web/tests/helpers/performance-lifecycle.mjs';

export const requiredAssets=['index.html','phone.css','app.js','assets.js','storage.js',
 'checkpoint-worker.js','emu-worker.js','seed-jit.js','manifest.json','icon.svg',
 'dist/emu.js','dist/emu.wasm','jit/abi.json','jit/regions.json','jit/seed.wasm'];
const json=v=>JSON.stringify(v,(_,x)=>typeof x==='bigint'?x.toString():x,2)+'\n';
const sha=b=>createHash('sha256').update(b).digest('hex');
const wait=ms=>new Promise(r=>setTimeout(r,ms));

export function validateSwipe(value){
 if(!value||typeof value!=='object'||Array.isArray(value))throw Error('swipe must be an object');
 for(const point of [value.from,value.to]){
  if(!Array.isArray(point)||point.length!==2||!point.every(Number.isFinite)||point[0]<0||point[0]>=320||point[1]<0||point[1]>=480)
   throw Error('swipe endpoints must be finite in-bounds guest coordinates');
 }
 const steps=value.steps??20,duration=value.duration??600;
 if(!Number.isInteger(steps)||steps<2||steps>120||!Number.isInteger(duration)||duration<100||duration>3000||duration<steps*5)
  throw Error('swipe requires2..120 steps,100..3000ms duration, at least5ms per step');
 return {from:[...value.from],to:[...value.to],steps,duration};
}
export async function swipeScreen(page,value,sleep=wait){
 const swipe=validateSwipe(value),b=await page.locator('#screen').boundingBox();
 if(!b||![b.x,b.y,b.width,b.height].every(Number.isFinite)||b.width<=0||b.height<=0)throw Error('Guest screen has no usable bounding box');
 const position=t=>({x:b.x+(swipe.from[0]+(swipe.to[0]-swipe.from[0])*t)*b.width/320,
                    y:b.y+(swipe.from[1]+(swipe.to[1]-swipe.from[1])*t)*b.height/480});
 const began=Date.now(),first=position(0);await page.mouse.move(first.x,first.y);
 try{
  await page.mouse.down();
  for(let i=1;i<=swipe.steps;i++){await sleep(swipe.duration/swipe.steps);const p=position(i/swipe.steps);await page.mouse.move(p.x,p.y);}
 }finally{await page.mouse.up();}
 return {...swipe,elapsedMs:Date.now()-began,scope:'Original page mouse/touch path; requested duration plus scheduling overhead, not a latency measurement'};
}

export function parseRequirements(text='[]'){
 const value=JSON.parse(text);
 if(!Array.isArray(value)||value.length>100||value.some(x=>typeof x!=='string'||!/^[-a-zA-Z0-9_]{1,100}$/.test(x))||new Set(value).size!==value.length)
  throw Error('REQUIREMENTS_JSON must be a unique array of bounded check labels');
 return value;
}
export function validateResponses(rows,pins,origin){
 const seen=new Set();let accepted=0;
 for(const row of rows){
  if(typeof row.url!=='string')throw Error('Response URL must be a string');
  const url=new URL(row.url);if(url.origin!==origin)continue;
  const path=url.pathname==='/'?'index.html':decodeURIComponent(url.pathname.slice(1));
  const pin=pins[path];
  if(!pin||row.status!==200||row.bodyError)throw Error('Unexpected or unreadable local response: '+path);
  if(row.bodyBytes!==pin.bytes||row.bodySHA256!==pin.sha256)throw Error('Local response bytes differ: '+path);
  seen.add(path);accepted++;
 }
 for(const name of requiredAssets)if(!seen.has(name))throw Error('Missing actual served asset: '+name);
 return {passed:true,requiredAssets:[...requiredAssets],acceptedLocalResponses:accepted,
  scope:'Actual received local bodies equal pinned site bytes; no offline/native-cache/guest HTTP claim.'};
}
export function workflowResult(report){
 const runCompleted=report.finished===true&&report.errors.length===0;
 const missing=(report.requirements||[]).filter(name=>!report.checks.some(c=>c.name===name&&c.passed===true));
 return {runCompleted,missingRequirements:missing,
  passed:report.requirements?.length?runCompleted&&missing.length===0:null};
}
async function fileDigest(path){const h=createHash('sha256');let bytes=0;for await(const chunk of createReadStream(path)){h.update(chunk);bytes+=chunk.length;}return {bytes,sha256:h.digest('hex')};}

export async function main(env=process.env){
 const start=Date.now(),dir=resolve(env.ARTIFACT_DIR||'.context/services-browser-qualification');
 // New evidence directory only. Existing attempts are never overwritten.
 await mkdir(dir,{recursive:false});
 const report={startedUTC:new Date().toISOString(),actions:[],checks:[],observations:[],frames:[],
  errors:[],console:[],requests:[],responses:[],requestFailures:[],serial:[],cleanup:[],
  scope:'Diagnostic snapshot import followed by original touchscreen input; separate native boot qualification required. No speed or offline claim.'};
 let site,origin,server,browser,browserLaunch,context,page,video,input,lines,forceTimer,finishPromise,minFreeBytes;
 let closing=false;const responseWork=new Set();
 let uploadPath,uploadOwned=false;
 async function verifyUpload(stage){
  const actual=await fileDigest(uploadPath);
  report.upload[stage]=actual;
  if(actual.bytes!==report.fixture.bytes||actual.sha256!==report.fixture.sha256)throw Error('Private snapshot upload changed: '+stage);
 }
 async function cleanupUpload(){
  if(!uploadOwned||report.upload.removed)return;
  await verifyUpload('beforeCleanup');
  if(report.upload.importComplete){await unlink(uploadPath);report.upload.removed=true;}
  else report.upload.retained=true;
 }
 async function persist(){await writeFile(resolve(dir,'report.json'),json(report));await writeFile(resolve(dir,'serial.log'),report.serial.map(x=>x.text).join(''));}
 async function drainResponses(){await bounded(()=>Promise.allSettled([...responseWork]),10000,'Response body collection');}
 async function finish(error){
  if(finishPromise)return finishPromise;
  closing=true;
  finishPromise=(async()=>{
   if(error)report.errors.push(String(error.stack||error));clearTimeout(forceTimer);input?.close();
   // Launch has its own30s bound. Retain/close a browser whose launch resolves
   // after the workflow deadline instead of losing its ownership in this race.
   if(browserLaunch&&!browser)try{await bounded(()=>browserLaunch,35000,'Pending browser launch');}
   catch(e){report.errors.push('pending browser launch: '+e.message);}
   try{if(page&&!page.isClosed()){
    video=page.video();report.finalState=await bounded(()=>page.evaluate(()=>({ready:window.iphone?.ready,paused:window.iphone?.paused,savedAt:window.iphone?.savedAt,metrics:window.iphone?.metrics,status:document.querySelector('#status')?.textContent})),5000,'Final state');
    await bounded(()=>page.screenshot({path:resolve(dir,'final.png')}),5000,'Final screenshot');
   }}catch(e){report.errors.push('final observation: '+e.message);}
   try{await drainResponses();}catch(e){report.errors.push(e.message);}
   for(const [name,resource]of [['context',context],['browser',browser]]){
    if(!resource){report.cleanup.push({name,status:'not-created'});continue;}
    try{await bounded(()=>resource.close(),10000,name+' close');report.cleanup.push({name,status:'closed'});}
    catch(e){report.cleanup.push({name,status:'failed',error:e.message});report.errors.push(name+' close: '+e.message);}
   }
   if(video)try{report.video=await bounded(()=>video.path(),5000,'Video finalization');report.videoFile=await fileDigest(report.video);}
   catch(e){report.errors.push('video: '+e.message);}
   try{await drainResponses();if(report.projection)report.responseAudit=validateResponses(report.responses,report.projection,origin);}
   catch(e){report.errors.push('actual response audit: '+e.message);}
   try{for(const [name,pin]of Object.entries(report.projection||{})){const got=await fileDigest(resolve(site,name));if(got.bytes!==pin.bytes||got.sha256!==pin.sha256)throw Error('Frozen site changed: '+name);}report.projectionUnchanged=Boolean(report.projection);}
   catch(e){report.errors.push(e.message);}
   if(server)try{server.closeAllConnections();await bounded(()=>new Promise((ok,no)=>server.close(e=>e?no(e):ok())),5000,'Server close');report.cleanup.push({name:'server',status:'closed'});}
   catch(e){report.cleanup.push({name:'server',status:'failed',error:e.message});report.errors.push('server close: '+e.message);}
   try{await cleanupUpload();}catch(e){report.errors.push('upload cleanup: '+e.message);}
   report.elapsedSeconds=(Date.now()-start)/1000;Object.assign(report,workflowResult(report));
   await persist();console.log('END '+json({passed:report.passed,runCompleted:report.runCompleted,errors:report.errors,report:resolve(dir,'report.json')}));
   process.exitCode=report.runCompleted&&(report.passed!==false)?0:1;
  })();
  return finishPromise;
 }
 // Original helper/action implementations follow; no guest text setter exists.
 async function ready(){await page.waitForFunction(()=>window.iphone?.ready,null,{timeout:120000});await page.waitForFunction(()=>Number(document.querySelector('#screen').dataset.frame)>0);}
async function jit(label){await page.waitForFunction(()=>{const j=window.iphone?.metrics?.jit;return j?.enabled&&j.calls>0&&j.retired>0&&!window.iphone.paused;},null,{timeout:30000});report.checks.push({name:label,passed:true,jit:await page.evaluate(()=>window.iphone.metrics.jit)});}
async function tap([x,y]){if(!Number.isFinite(x)||!Number.isFinite(y)||x<0||x>=320||y<0||y>=480)throw Error('touch bounds');const b=await page.locator('#screen').boundingBox();await page.mouse.move(b.x+x*b.width/320,b.y+y*b.height/480);await page.mouse.down();await wait(180);await page.mouse.up();}
async function shot(name){if(!/^[a-zA-Z0-9_-]+$/.test(name))throw Error('shot name');if(report.frames.some(f=>f.name===name))throw Error('Duplicate screenshot name');await page.screenshot({path:resolve(dir,name+'.png')});await page.locator('#screen').screenshot({path:resolve(dir,name+'-guest.png')});report.frames.push({name,guest:name+'-guest.png',page:name+'.png',elapsedSeconds:(Date.now()-start)/1000});report.lastState=await page.evaluate(()=>({savedAt:window.iphone.savedAt,paused:window.iphone.paused,metrics:window.iphone.metrics,status:document.querySelector('#status').textContent}));await persist();console.log('SHOT '+name);}
async function act(a){if(!a||typeof a!=='object'||Array.isArray(a))throw Error('Action must be an object');report.actions.push({...a,elapsedSeconds:(Date.now()-start)/1000});
 if(a.tap)await tap(a.tap);if(a.taps)for(const point of a.taps){await tap(point);await wait(a.interval??900);}
 if(a.swipe!==undefined){report.swipes??=[];report.swipes.push(await swipeScreen(page,a.swipe));}
 if(a.home){const b=await page.locator('[data-button="1"]').boundingBox();await page.mouse.move(b.x+b.width/2,b.y+b.height/2);await page.mouse.down();await wait(180);await page.mouse.up();}
 if(a.wait!==undefined){if(!Number.isFinite(a.wait)||a.wait<0||a.wait>60000)throw Error('wait must be 0..60000ms');await wait(a.wait);}

 if(a.requireRequest||a.requireNewRequest||a.requireResponse)throw Error('Quiet binary does not capture raw HTTP wire bytes');
 if(a.observe||a.assert){
  const label=a.observe||a.assert;
  if(a.observe&&a.assert&&a.observe!==a.assert)throw Error('Observation/assertion label mismatch');
  if(typeof label!=='string'||!/^[-a-zA-Z0-9_]{1,100}$/.test(label))throw Error('Observation label');
  if(!/^[a-zA-Z0-9_-]+\.png$/.test(a.frame??''))throw Error('Explicit retained framebuffer required');
  if(!report.frames.some(f=>f.guest===a.frame))throw Error('Frame must be a guest screenshot retained by this run');
  const evidence=await fileDigest(resolve(dir,a.frame));
  const observation={name:label,scope:'Operator review of retained original framebuffer; no automatic app recognition',frame:a.frame,frameSHA256:evidence.sha256,expected:a.expected??null};
  report.observations.push(observation);
  if(a.assert){if(typeof a.expected!=='string'||!a.expected.trim()||a.expected.length>1000)throw Error('Explicit expected visible content required for assertion');
   if(report.checks.some(c=>c.name===label))throw Error('Duplicate assertion label');
   report.checks.push({...observation,passed:true,assertedBy:'operator'});
  }
 }
 if(a.finalExport){
  if(report.exportDiagnostic)throw Error('Only one retained final diagnostic export is permitted');
  report.exportDiagnostic={scope:'Hidden existing Export button, enabled wait, native DOM click; separate from real autosave',before:await page.evaluate(()=>({disabled:document.querySelector('#export').disabled,status:document.querySelector('#status').textContent,savedAt:window.iphone.savedAt,paused:window.iphone.paused}))};
  await page.waitForFunction(()=>{const b=document.querySelector('#export');return b&&!b.disabled;},null,{timeout:90000});
  const event=page.waitForEvent('download',{timeout:120000});
  report.exportDiagnostic.click=await page.locator('#export').evaluate(button=>{if(button.disabled)throw Error('Export became disabled');const receipt={disabled:button.disabled,status:document.querySelector('#status').textContent,savedAt:window.iphone.savedAt};button.click();return receipt;});
  const download=await event;report.exportDiagnostic.suggestedFilename=download.suggestedFilename();
  const target=resolve(dir,'final-home.snap');await download.saveAs(target);if(await download.failure())throw Error('Export download failed');
  report.exportDiagnostic.file={path:target,...await fileDigest(target)};
  report.checks.push({name:'diagnostic-final-export',passed:true,...report.exportDiagnostic.file});
 }
 if(a.autosaveReload){const old=await page.evaluate(()=>window.iphone.savedAt);report.autoSaveWait={oldSavedAt:old,startedUTC:new Date().toISOString()};await page.waitForFunction(old=>window.iphone.savedAt>old||/Save failed/.test(document.querySelector('#status').textContent),old,{timeout:70000});const saved=await page.evaluate(()=>window.iphone.savedAt);if(saved<=old)throw Error('Real interval autosave did not advance');report.checks.push({name:'real-interval-autosave',passed:true,oldSavedAt:old,savedAt:saved});const sequence=(report.autosaveReloads??0)+1;report.autosaveReloads=sequence;await shot('autosaved-'+sequence);await page.reload({waitUntil:'domcontentloaded'});await ready();const restored=await page.evaluate(()=>window.iphone.savedAt);if(restored!==saved)throw Error('Reload record mismatch');report.checks.push({name:'actual-autosave-record-restored',passed:true,savedAt:restored});await jit('restored-generated-execution');await shot('restored-'+sequence);}
 if(a.shot)await shot(a.shot);await persist();console.log('READY');
}

 try{
  if(!env.SITE_ROOT||!env.SNAPSHOT_PATH)throw Error('SITE_ROOT and SNAPSHOT_PATH are required');
  site=resolve(env.SITE_ROOT);const port=Number(env.PORT||8231),cap=Number(env.CAP_SECONDS||900);
  minFreeBytes=Number(env.MIN_FREE_BYTES??1073741824);
  if(!Number.isSafeInteger(minFreeBytes)||minFreeBytes<=0)throw Error('MIN_FREE_BYTES must be a positive safe integer');
  if(!Number.isInteger(port)||port<1024||port>65535||!Number.isInteger(cap)||cap<1||cap>900)throw Error('PORT 1024..65535 and CAP_SECONDS 1..900 required');
  origin='http://127.0.0.1:'+port;report.siteRoot=site;report.origin=origin;report.capSeconds=cap;report.minFreeBytes=minFreeBytes;
  report.requirements=parseRequirements(env.REQUIREMENTS_JSON);report.mode=report.requirements.length?'explicit-workflow-assertions':'observation-only';
  forceTimer=setTimeout(()=>{void finish(Error(cap+'-second one-workflow cap'));},cap*1000);
  const fs=await statfs(dir);report.initialAvailableBytes=Number(fs.bavail)*Number(fs.bsize);
  if(report.initialAvailableBytes<minFreeBytes)throw Error('Configured disk reserve not met before launch');
  let fixture=await loadPerformanceFixture(env.SNAPSHOT_PATH);report.fixture=fixture.metadata;
  if(env.SNAPSHOT_SHA256&&fixture.metadata.sha256!==env.SNAPSHOT_SHA256)throw Error('Expected snapshot SHA256 mismatch');
  uploadPath=resolve(dir,'import-'+basename(fixture.metadata.path));
  report.upload={path:uploadPath,ownedCopy:true,removed:false,importComplete:false};
  await writeFile(uploadPath,fixture.upload.buffer,{flag:'wx'});uploadOwned=true;fixture=null;
  await verifyUpload('prepared');
  report.fixtureScope='Exact imported file bytes; native listener/process health must be independently qualified.';
  report.projection={};const pinnedBytes=new Map();
  for(const name of [...requiredAssets,'service-worker.js']){const bytes=await readFile(resolve(site,name));pinnedBytes.set(name,bytes);report.projection[name]={bytes:bytes.length,sha256:sha(bytes)};}
  report.sourcePins={};for(const path of [new URL(import.meta.url),new URL('../../../web/tests/helpers/performance-lifecycle.mjs',import.meta.url)])report.sourcePins[path.pathname]=await fileDigest(path);
  await persist();
  const mime={'.js':'text/javascript; charset=utf-8','.mjs':'text/javascript; charset=utf-8','.wasm':'application/wasm','.json':'application/json; charset=utf-8','.html':'text/html; charset=utf-8','.css':'text/css; charset=utf-8','.svg':'image/svg+xml; charset=utf-8'};
  if(closing)throw Error('Run closed during input preparation');
  server=createServer((req,res)=>{try{const url=new URL(req.url,origin),name=url.pathname==='/'?'index.html':decodeURIComponent(url.pathname.slice(1));const bytes=pinnedBytes.get(name);if(req.method!=='GET'||!bytes){res.writeHead(404);res.end();return;}res.writeHead(200,{'Content-Type':mime[extname(name)]||'application/octet-stream','Content-Length':bytes.length,'Cache-Control':'no-cache'});res.end(bytes);}catch{res.writeHead(400);res.end();}});
  await new Promise((ok,no)=>{server.once('error',no);server.listen(port,'127.0.0.1',ok);});
  if(closing)throw Error('Run closed during listener startup');
  input=createInterface({input:process.stdin,terminal:false});lines=input[Symbol.asyncIterator]();
  browserLaunch=chromium.launch({executablePath:env.CHROME_PATH||'/usr/bin/google-chrome',args:['--no-sandbox','--disable-dev-shm-usage'],timeout:30000}).then(value=>{browser=value;return value;});
  await browserLaunch;if(closing)throw Error('Run closed during browser launch');
  context=await browser.newContext({viewport:{width:1280,height:1000},recordVideo:{dir,size:{width:1280,height:1000}}});
  if(closing)throw Error('Run closed during context creation');
  await context.exposeBinding('__retainGuestSerial',(_,text)=>{if(typeof text==='string')report.serial.push({seconds:(Date.now()-start)/1000,text});});
  await context.addInitScript(()=>{const Native=window.Worker;window.Worker=class extends Native{constructor(...args){super(...args);this.addEventListener('message',event=>{if(event.data?.serial)window.__retainGuestSerial(event.data.serial);if(event.data?.type==='log')window.__retainGuestSerial(event.data.message+'\n');});}};});
  context.on('request',r=>report.requests.push({seconds:(Date.now()-start)/1000,url:r.url(),method:r.method()}));
  context.on('requestfailed',r=>report.requestFailures.push({url:r.url(),error:r.failure(),seconds:(Date.now()-start)/1000,duringCleanup:closing}));
  context.on('response',r=>{const row={url:r.url(),status:r.status(),fromSW:r.fromServiceWorker()};report.responses.push(row);
   const task=(async()=>{try{if(new URL(row.url).origin===origin){const bytes=await bounded(()=>r.body(),10000,'Response body '+row.url);row.bodyBytes=bytes.length;row.bodySHA256=sha(bytes);}}catch(e){row.bodyError=e.message;report.errors.push('response body: '+e.message);}})();
   responseWork.add(task);task.finally(()=>responseWork.delete(task));
  });
  page=await context.newPage();page.setDefaultTimeout(15000);
  page.on('pageerror',e=>{report.errors.push(e.message);console.error('PAGE ERROR '+e.message);});page.on('console',m=>report.console.push({type:m.type(),text:m.text()}));
  await page.goto(origin,{waitUntil:'domcontentloaded'});await ready();await jit('fresh-generated-execution');await shot('00-default-home');
  await page.waitForFunction(()=>!document.querySelector('#import').disabled,null,{timeout:60000});
  await verifyUpload('beforeImport');
  await page.locator('#import').setInputFiles(uploadPath);
  await page.waitForFunction(()=>/backup is restored|Import failed/.test(document.querySelector('#status').textContent),null,{timeout:90000});
  if(/Import failed/.test(await page.locator('#status').textContent()))throw Error('Import failed');
  await verifyUpload('afterImport');report.upload.importComplete=true;
  await cleanupUpload();
  await jit('imported-generated-execution');await wait(3000);await shot('01-imported');console.log('READY');
  for await(const line of lines){
   if(closing)break;if(!line.trim())continue;if(line.length>65536)throw Error('Action JSON exceeds64KiB');
   if(line.trim()==='finish'){
    const missing=workflowResult(report).missingRequirements;if(missing.length)throw Error('Unfinished workflow gates: '+missing.join(', '));
    report.finished=true;break;
   }
   await act(JSON.parse(line));const fs=await statfs(dir);if(Number(fs.bavail)*Number(fs.bsize)<minFreeBytes)throw Error('Configured disk reserve reached');
  }
  if(!closing){await jit('final-unpaused-generated-execution');report.rawHTTPWireCaptured=false;await finish();}
  else await finishPromise;
 }catch(error){await finish(error);}
}
if(process.argv[1]&&import.meta.url===pathToFileURL(resolve(process.argv[1])).href){
 main().catch(error=>{console.error(error.stack||error);process.exitCode=1;});
}
