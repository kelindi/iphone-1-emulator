import {chromium} from 'playwright';
import {Attribution,RawCDP} from './cdp.mjs';
import {readFile,writeFile,mkdir} from 'node:fs/promises';
import {resolve} from 'node:path';
import {createHash} from 'node:crypto';
const dir=resolve(process.env.ARTIFACT_DIR||'.context/first-use/run-1'); await mkdir(dir,{recursive:true});
const fixture=resolve('.context/graphics-speed/fixed-dismiss.snap');
const json=value=>JSON.stringify(value,(_,v)=>typeof v==='bigint'?v.toString():v,2);
if(JSON.parse(json({nested:[{cycles:123n}]})).nested[0].cycles!=='123')throw Error('JSON preflight');
const sha=b=>createHash('sha256').update(b).digest('hex');
const report={started:new Date().toISOString(),fixture,fixtureSha:sha(await readFile(fixture)),actions:[],errors:[],assets:[],seams:{app:sha(await readFile('.context/first-use/app.js')),worker:sha(await readFile('.context/first-use/emu-worker.js'))}};
const browser=await chromium.launch({executablePath:'/usr/bin/google-chrome',args:['--no-sandbox','--disable-dev-shm-usage','--remote-debugging-port=9238']});
const context=await browser.newContext({viewport:{width:1280,height:1000},serviceWorkers:'block'});
await context.route('**/app.js',r=>r.fulfill({path:resolve('.context/first-use/app.js'),contentType:'application/javascript'}));
await context.route('**/emu-worker.js*',r=>r.fulfill({path:resolve('.context/first-use/emu-worker.js'),contentType:'application/javascript'}));
await context.addInitScript(()=>{const Original=window.Worker;window.Worker=class extends Original{constructor(url,options){super(url,options);if(String(url).includes('emu-worker.js'))window.profileWorker=this;}};let id=900000;window.profileCounters=()=>new Promise((resolve,reject)=>{const w=window.profileWorker,n=++id,start=performance.now();const timer=setTimeout(()=>{w.removeEventListener('message',listener);reject(Error('counter timeout'));},5000);function listener(e){if(e.data.type==='profileCounters'&&e.data.id===n){clearTimeout(timer);w.removeEventListener('message',listener);resolve({...e.data,mainSend:start,mainReply:performance.now()});}}w.addEventListener('message',listener);w.postMessage({type:'profileCounters',id:n});});});
const page=await context.newPage();page.on('pageerror',e=>report.errors.push(e.message));
const assetTasks=[];context.on('response',r=>{if(/\/(emu|seed)\.wasm(?:\?|$)/.test(r.url()))assetTasks.push((async()=>{try{const bytes=await r.body();report.assets.push({url:r.url(),length:bytes.length,sha256:sha(bytes)});}catch(e){report.assets.push({url:r.url(),error:e.message});}})());});
const version=await(await fetch('http://127.0.0.1:9238/json/version')).json();report.version=version;
const client=new RawCDP(version.webSocketDebuggerUrl);await client.ready;
let attribution,tracing=false;const traceComplete=new Promise(resolve=>client.listeners.push(e=>{if(e.method==='Tracing.tracingComplete')resolve(e.params);}));
async function mark(label){await client.call('Tracing.recordClockSyncMarker',{syncId:label});}
async function counters(){return await page.evaluate(()=>window.profileCounters());}
async function importPaused(label){await mark(label+'-import-start');if(!await page.evaluate(()=>window.iphone.paused))await page.locator('#pause').click();await page.waitForFunction(()=>window.iphone.paused);await page.locator('#import').setInputFiles(fixture);await page.waitForFunction(()=>/backup is restored|Import failed/.test(document.querySelector('#status').textContent),null,{timeout:90000});if(/Import failed/.test(await page.locator('#status').textContent()))throw Error('Import failed');await page.waitForFunction(()=>!document.querySelector('#pause').disabled);let c=await counters();if(!c.jit.enabled||c.jit.regions!==512)throw Error('Seed not fully installed after import '+JSON.stringify(c));report[label+'Import']=c;await mark(label+'-import-end');await page.locator('#pause').click();await page.waitForFunction(()=>!window.iphone.paused);await page.waitForTimeout(1000);}
try{
 const available=await client.call('Tracing.getCategories');report.availableCategories=available.categories;
 const wanted=['v8','v8.wasm','disabled-by-default-v8.wasm.detailed','disabled-by-default-v8.compile'];const included=wanted.filter(x=>available.categories.includes(x));if(!included.includes('v8.wasm'))throw Error('WASM trace category absent');report.categories=included;
 await client.call('Tracing.start',{transferMode:'ReturnAsStream',streamFormat:'json',traceConfig:{recordMode:'recordUntilFull',traceBufferSizeInKb:131072,includedCategories:included}});tracing=true;
 await mark('navigation-start');await page.goto('http://localhost:8080');await page.waitForFunction(()=>window.iphone?.ready,null,{timeout:120000});await importPaused('C0');
 attribution=await Attribution.connect(9238,resolve(dir,'profiles'));
 await page.evaluate(()=>{
  const canvas=document.querySelector('#screen');
  const ctx=canvas.getContext('2d');
  function pixels(){const raw=ctx.getImageData(0,0,320,480).data;const out=[];let white=0;for(let y=30;y<480;y+=4)for(let x=2;x<320;x+=4){const i=(y*320+x)*4;out.push(raw[i],raw[i+1],raw[i+2]);if(Math.min(raw[i],raw[i+1],raw[i+2])>235)white++;}return {out,white:white/(out.length/3)};}
  function diff(a,b){let n=0;for(let i=0;i<a.out.length;i+=3)if(Math.max(Math.abs(a.out[i]-b.out[i]),Math.abs(a.out[i+1]-b.out[i+1]),Math.abs(a.out[i+2]-b.out[i+2]))>25)n++;return n/(a.out.length/3);}
  const home=pixels();
  window.phoneTiming={begin(kind){const before=pixels();const start=performance.now();let previous=before,stable=start,first=null,ready=null;this.result=null;this.releaseMs=null;this.timer=setInterval(()=>{const now=performance.now(),frame=pixels(),changed=diff(before,frame);if(first===null && changed>.015)first=now-start;const isReady=kind==='home'?diff(home,frame)<.04:frame.white>.25;if(diff(previous,frame)>.008 || !isReady || changed<.1){stable=now;ready=null;}else if(ready===null)ready=stable;previous=frame;if(ready!==null && now-ready>=500){clearInterval(this.timer);this.result={firstVisibleMs:first,readyMs:ready-start,releaseOffsetMs:this.releaseMs===null?null:this.releaseMs-start,releaseToFirstVisibleMs:this.releaseMs===null||first===null?null:start+first-this.releaseMs,releaseToReadyMs:this.releaseMs===null?null:ready-this.releaseMs,observedAtMs:now-start,metrics:window.iphone.metrics};}else if(now-start>90000){clearInterval(this.timer);this.result={error:'Readiness timed out',firstVisibleMs:first,changed,white:frame.white,metrics:window.iphone.metrics};}},50);},release(){if(this.releaseMs===null)this.releaseMs=performance.now();},get(){return this.result;}};
  document.addEventListener('pointerup',()=>window.phoneTiming.release(),{capture:true});
 });
 const box=await page.locator('#screen').boundingBox();
 for(const label of ['C0','H1','C1']){
  if(label==='C1')await importPaused(label);
  for(const kind of ['settings','home']){
   const point=kind==='settings'?{x:box.x+275/320*box.width,y:box.y+250/480*box.height}:await page.locator('[data-button="1"]').boundingBox().then(b=>({x:b.x+b.width/2,y:b.y+b.height/2}));
   await page.mouse.move(point.x,point.y);const before=await counters();await attribution.begin(label+'-'+kind,before);await mark(label+'-'+kind+'-start');
   await page.evaluate(k=>window.phoneTiming.begin(k),kind);await page.mouse.down();await page.waitForTimeout(30);await page.mouse.up();await page.waitForFunction(()=>window.phoneTiming.get(),null,{timeout:95000});const result=await page.evaluate(()=>window.phoneTiming.get());await mark(label+'-'+kind+'-end');const after=await counters();await attribution.end(after);
   report.actions.push({label,kind,before,after,...result});await writeFile(resolve(dir,'report-partial.json'),json(report));await page.locator('#screen').screenshot({path:resolve(dir,label+'-'+kind+'.png')});const raw=await page.evaluate(()=>Array.from(document.querySelector('#screen').getContext('2d').getImageData(0,0,320,480).data));await writeFile(resolve(dir,label+'-'+kind+'.rgba'),Buffer.from(raw));
   console.log(JSON.stringify({label,kind,readyMs:result.releaseToReadyMs,retired:after.retired-before.retired,generated:after.jit.retired-before.jit.retired,error:result.error}));if(result.error)throw Error(result.error);if(!(after.jit.retired>before.jit.retired))throw Error('No generated progress');await page.waitForTimeout(1000);
  }
 }
 report.passed=report.errors.length===0;
}catch(e){report.errors.push(e.stack||e.message);report.passed=false;}
finally{
 if(attribution){await attribution.close().catch(e=>report.errors.push(e.message));}
 if(tracing){try{await client.call('Tracing.end');const complete=await Promise.race([traceComplete,new Promise((_,reject)=>setTimeout(()=>reject(Error('Trace completion timeout')),20000))]);report.traceComplete=complete;await writeFile(resolve(dir,'report-partial.json'),json(report));if(complete.dataLossOccurred)report.passed=false;let chunks=[];for(;;){const r=await client.call('IO.read',{handle:complete.stream,size:1048576});chunks.push(r.base64Encoded?Buffer.from(r.data,'base64'):Buffer.from(r.data));if(r.eof)break;}await client.call('IO.close',{handle:complete.stream});await writeFile(resolve(dir,'trace.json'),Buffer.concat(chunks));}catch(e){report.errors.push(e.stack);report.passed=false;}}
 await Promise.allSettled(assetTasks);client.close();await context.close();await browser.close();await writeFile(resolve(dir,'report.json'),json(report));console.log(JSON.stringify({finished:true,passed:report.passed,errors:report.errors}));
}
if(!report.passed)process.exitCode=1;
