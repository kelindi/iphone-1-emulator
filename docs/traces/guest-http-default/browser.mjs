// One bounded, adaptive ORIGINAL touchscreen workflow. No guest text injection.
import {chromium} from 'playwright';
import {mkdir,writeFile,readFile,rm,stat,statfs} from 'node:fs/promises';
import {createReadStream} from 'node:fs';
import {createServer} from 'node:http';
import {createInterface} from 'node:readline';
import {resolve,extname} from 'node:path';
import {createHash} from 'node:crypto';
import {validateResponses} from './response_audit.mjs';
const base=resolve('.context/guest-http-default'),dir=resolve(base,'browser-run-1'),site=resolve(base,'site');
await mkdir(dir);const start=Date.now(),report={startedUTC:new Date().toISOString(),capSeconds:900,actions:[],checks:[],errors:[],console:[],requests:[],responses:[],requestFailures:[],serial:[],scope:'Diagnostic snapshot import; all subsequent guest input uses original touchscreen. Real interval autosave. Final quiet binary: rendered fresh form/bookmark evidence, no raw HTTP wire capture, offline or speed claim.'};
const json=v=>JSON.stringify(v,(_,x)=>typeof x==='bigint'?x.toString():x,2)+'\n';
const sha=b=>createHash('sha256').update(b).digest('hex');
const wait=ms=>new Promise(r=>setTimeout(r,ms));
async function fileDigest(path){const h=createHash('sha256');let bytes=0;for await(const chunk of createReadStream(path)){h.update(chunk);bytes+=chunk.length;}return {bytes,sha256:h.digest('hex')};}
async function persist(){await writeFile(resolve(dir,'report.json'),json(report));await writeFile(resolve(dir,'serial.log'),report.serial.map(x=>x.text).join(''));}
const mime={'.js':'text/javascript','.mjs':'text/javascript','.wasm':'application/wasm','.json':'application/json','.html':'text/html','.css':'text/css','.svg':'image/svg+xml'};
const server=createServer(async(req,res)=>{try{const u=new URL(req.url,'http://localhost:8213');let path=resolve(site,'.'+decodeURIComponent(u.pathname==='/'?'/index.html':u.pathname));if(!path.startsWith(site+'/'))throw Error('path');const s=await stat(path);if(!s.isFile())throw Error('not file');res.writeHead(200,{'Content-Type':mime[extname(path)]||'application/octet-stream','Content-Length':s.size,'Cache-Control':'no-cache'});createReadStream(path).pipe(res);}catch{res.writeHead(404);res.end();}});
await new Promise((ok,no)=>{server.once('error',no);server.listen(8213,'127.0.0.1',ok);});
let browser,context,page,video,closing=false;let forceTimer;const responseWork=new Set();
async function drainResponses(){let timer;try{await Promise.race([Promise.allSettled([...responseWork]),new Promise((_,reject)=>{timer=setTimeout(()=>reject(Error('Response-body collection exceeded 10 seconds')),10000);})]);}finally{clearTimeout(timer);}}
const input=createInterface({input:process.stdin,terminal:false});
async function finish(error){if(closing)return;closing=true;if(error)report.errors.push(String(error.stack||error));clearTimeout(forceTimer);input.close();
 try{if(page&&!page.isClosed()){report.finalState=await page.evaluate(()=>({ready:window.iphone?.ready,paused:window.iphone?.paused,savedAt:window.iphone?.savedAt,metrics:window.iphone?.metrics,status:document.querySelector('#status')?.textContent}));await page.screenshot({path:resolve(dir,'final.png')});video=page.video();}}catch(e){report.errors.push('final observation: '+e.message);}
 try{await drainResponses();report.responseAudit=validateResponses(report.responses,report.projection,'http://127.0.0.1:8213');}catch(e){report.errors.push('actual response audit: '+e.message);}
 try{await context?.close();await browser?.close();if(video)report.video=await video.path();}catch(e){report.errors.push('close: '+e.message);}
 try{await drainResponses();}catch(e){report.errors.push('response collection after close: '+e.message);}
 try{for(const [name,pin]of Object.entries(report.projection||{})){if(sha(await readFile(resolve(site,name)))!==pin.sha256)throw Error('Frozen served source changed: '+name);}report.projectionUnchanged=true;}catch(e){report.errors.push(e.message);}
 server.closeAllConnections();await new Promise(r=>server.close(r));report.elapsedSeconds=(Date.now()-start)/1000;report.passed=report.errors.length===0&&report.finished===true;await persist();console.log('END '+json({passed:report.passed,errors:report.errors,report:resolve(dir,'report.json')}));process.exitCode=report.passed?0:1;
}
forceTimer=setTimeout(()=>finish(Error('900-second one-workflow cap')),900000);
async function ready(){await page.waitForFunction(()=>window.iphone?.ready,null,{timeout:120000});await page.waitForFunction(()=>Number(document.querySelector('#screen').dataset.frame)>0);}
async function jit(label){await page.waitForFunction(()=>{const j=window.iphone?.metrics?.jit;return j?.enabled&&j.calls>0&&j.retired>0&&!window.iphone.paused;},null,{timeout:30000});report.checks.push({name:label,passed:true,jit:await page.evaluate(()=>window.iphone.metrics.jit)});}
async function tap([x,y]){if(!Number.isFinite(x)||!Number.isFinite(y)||x<0||x>=320||y<0||y>=480)throw Error('touch bounds');const b=await page.locator('#screen').boundingBox();await page.mouse.move(b.x+x*b.width/320,b.y+y*b.height/480);await page.mouse.down();await wait(180);await page.mouse.up();}
async function shot(name){if(!/^[a-zA-Z0-9_-]+$/.test(name))throw Error('shot name');await page.screenshot({path:resolve(dir,name+'.png')});await page.locator('#screen').screenshot({path:resolve(dir,name+'-guest.png')});report.lastState=await page.evaluate(()=>({savedAt:window.iphone.savedAt,paused:window.iphone.paused,metrics:window.iphone.metrics,status:document.querySelector('#status').textContent}));await persist();console.log('SHOT '+name);}
async function act(a){report.actions.push({...a,videoSeconds:(Date.now()-start)/1000});
 if(a.tap)await tap(a.tap);if(a.taps)for(const point of a.taps){await tap(point);await wait(a.interval??900);}
 if(a.home){const b=await page.locator('[data-button="1"]').boundingBox();await page.mouse.move(b.x+b.width/2,b.y+b.height/2);await page.mouse.down();await wait(180);await page.mouse.up();}
 if(a.wait)await wait(Math.min(a.wait,60000));

 if(a.requireRequest||a.requireNewRequest||a.requireResponse)throw Error('Quiet binary does not capture raw HTTP wire bytes');
 if(a.observe){
  if(!/^[a-zA-Z0-9_-]+\.png$/.test(a.frame??''))throw Error('Explicit retained framebuffer required');
  const evidence=await fileDigest(resolve(dir,a.frame));
  if(a.observe==='fresh-form-token-1'||a.observe==='fresh-form-token-2'){
   if(!/^[a-z]{1,8}$/.test(a.token??''))throw Error('Explicit short actually typed token required');
   report.formTokens??=[];if(report.formTokens.includes(a.token))throw Error('Post-reload form token must be fresh');report.formTokens.push(a.token);
  }
  report.checks.push({name:a.observe,passed:true,scope:'Human review of this retained original framebuffer',frame:a.frame,frameSHA256:evidence.sha256,token:a.token??null});
 }
 if(a.finalExport){
  report.exportDiagnostic={scope:'Hidden existing Export button, enabled wait, native DOM click; separate from real autosave',before:await page.evaluate(()=>({disabled:document.querySelector('#export').disabled,status:document.querySelector('#status').textContent,savedAt:window.iphone.savedAt,paused:window.iphone.paused}))};
  await page.waitForFunction(()=>{const b=document.querySelector('#export');return b&&!b.disabled;},null,{timeout:90000});
  const event=page.waitForEvent('download',{timeout:120000});
  report.exportDiagnostic.click=await page.locator('#export').evaluate(button=>{if(button.disabled)throw Error('Export became disabled');const receipt={disabled:button.disabled,status:document.querySelector('#status').textContent,savedAt:window.iphone.savedAt};button.click();return receipt;});
  const download=await event;report.exportDiagnostic.suggestedFilename=download.suggestedFilename();
  const target=resolve(dir,'final-home.snap');await download.saveAs(target);if(await download.failure())throw Error('Export download failed');
  report.exportDiagnostic.file={path:target,...await fileDigest(target)};
  report.checks.push({name:'diagnostic-final-export',passed:true,...report.exportDiagnostic.file});
 }
 if(a.autosaveReload){const old=await page.evaluate(()=>window.iphone.savedAt);report.autoSaveWait={oldSavedAt:old,startedUTC:new Date().toISOString()};await page.waitForFunction(old=>window.iphone.savedAt>old||/Save failed/.test(document.querySelector('#status').textContent),old,{timeout:70000});const saved=await page.evaluate(()=>window.iphone.savedAt);if(saved<=old)throw Error('Real interval autosave did not advance');report.checks.push({name:'real-interval-autosave',passed:true,oldSavedAt:old,savedAt:saved});await shot('autosaved');await page.reload({waitUntil:'domcontentloaded'});await ready();const restored=await page.evaluate(()=>window.iphone.savedAt);if(restored!==saved)throw Error('Reload record mismatch');report.checks.push({name:'actual-autosave-record-restored',passed:true,savedAt:restored});await jit('restored-generated-execution');await shot('restored');}
 if(a.shot)await shot(a.shot);await persist();console.log('READY');
}
try{const fs=await statfs(dir);report.initialAvailableBytes=Number(fs.bavail)*Number(fs.bsize);if(report.initialAvailableBytes<5000000000)throw Error('Fresh workflow requires 5GB free before launch');
 const native=JSON.parse(await readFile(resolve(base,'native-run-1/report.json'),'utf8'));
 const health=JSON.parse(await readFile(resolve(base,'native-run-1/process-health/report.json'),'utf8'));
 const fixture=await fileDigest(resolve(base,'native-run-1/listener.snap.gz'));
 if(!native.passed||!health.passed||fixture.sha256!==native.transport.receipt.gzipSHA256||fixture.bytes!==native.transport.receipt.gzipBytes)throw Error('Unqualified native listener/Home fixture');
 report.fixture={...fixture,rawSHA256:native.transport.receipt.rawSHA256,rawBytes:native.transport.receipt.rawBytes,sourcePinsSHA256:native.sourcePinsSHA256,processHealth:health.processAudit};
 report.projection=JSON.parse(await readFile(resolve(base,'site-pins.json'),'utf8'));for(const [name,pin]of Object.entries(report.projection)){const bytes=await readFile(resolve(site,name));if(bytes.length!==pin.bytes||sha(bytes)!==pin.sha256)throw Error('Prelaunch frozen source mismatch: '+name);}
 browser=await chromium.launch({executablePath:'/usr/bin/google-chrome',args:['--no-sandbox','--disable-dev-shm-usage']});context=await browser.newContext({viewport:{width:1280,height:1000},recordVideo:{dir,size:{width:1280,height:1000}}});
 await context.exposeBinding('__retainGuestSerial',(_,text)=>{if(typeof text==='string')report.serial.push({seconds:(Date.now()-start)/1000,text});});
 await context.addInitScript(()=>{const Native=window.Worker;window.Worker=class extends Native{constructor(...args){super(...args);this.addEventListener('message',event=>{if(event.data?.serial)window.__retainGuestSerial(event.data.serial);if(event.data?.type==='log')window.__retainGuestSerial(event.data.message+'\n');});}};});
 context.on('request',r=>report.requests.push({seconds:(Date.now()-start)/1000,url:r.url(),method:r.method()}));context.on('requestfailed',r=>report.requestFailures.push({url:r.url(),error:r.failure(),seconds:(Date.now()-start)/1000}));
 context.on('response',r=>{const row={url:r.url(),status:r.status(),fromSW:r.fromServiceWorker()};report.responses.push(row);const task=(async()=>{try{const url=new URL(row.url);if(url.origin==='http://127.0.0.1:8213'){const bytes=await r.body();row.bodyBytes=bytes.length;row.bodySHA256=sha(bytes);}}catch(error){row.bodyError=error.message;report.errors.push('response body: '+error.message);}})();responseWork.add(task);task.finally(()=>responseWork.delete(task));});
 page=await context.newPage();page.setDefaultTimeout(15000);page.on('pageerror',e=>{report.errors.push(e.message);console.error('PAGE ERROR '+e.message);});page.on('console',m=>report.console.push({type:m.type(),text:m.text()}));
 await page.goto('http://127.0.0.1:8213',{waitUntil:'domcontentloaded'});await ready();await jit('fresh-generated-execution');await shot('00-default-home');
 await page.waitForFunction(()=>!document.querySelector('#import').disabled,null,{timeout:60000});await page.locator('#import').setInputFiles(resolve(base,'native-run-1/listener.snap.gz'));await page.waitForFunction(()=>/backup is restored|Import failed/.test(document.querySelector('#status').textContent),null,{timeout:90000});if(/Import failed/.test(await page.locator('#status').textContent()))throw Error('Import failed');await jit('listener-restore-generated-execution');await wait(3000);await shot('01-listener-restored');console.log('READY');
 for await(const line of input){if(closing)break;if(!line.trim())continue;if(line.trim()==='finish'){const required=["pristine-settings-rendered","pristine-contacts-rendered","local-home-rendered","local-next-rendered","fresh-form-token-1","original-bookmark-created","original-bookmark-revisited","real-interval-autosave","actual-autosave-record-restored","post-reload-bookmark-rendered","fresh-form-token-2","final-home-rendered","diagnostic-final-export"];for(const name of required)if(!report.checks.some(c=>c.name===name&&c.passed))throw Error('Unfinished workflow gate: '+name);report.finished=true;break;}await act(JSON.parse(line));const fs=await statfs(dir);if(Number(fs.bavail)*Number(fs.bsize)<4300000000)throw Error('4.3GB disk reserve reached');}
 if(!closing){await jit('final-unpaused-generated-execution');report.rawHTTPWireCaptured=false;await finish();}
}catch(error){await finish(error);}
