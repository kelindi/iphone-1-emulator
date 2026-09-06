import {execFileSync} from 'node:child_process';
import {compareRaw} from './frames.mjs';
import {encodeBytes,decodeFrame} from './frame-packet.mjs';
import {chromium} from 'playwright';
import {RawCDP} from '../../docs/traces/browser-first-use/cdp.mjs';
import {readFile,writeFile,mkdir,readdir,readlink} from 'node:fs/promises';
import {resolve} from 'node:path';
import {createHash} from 'node:crypto';
const dir=resolve(process.env.ARTIFACT_DIR||'.context/first-use/run-1'); await mkdir(dir,{recursive:true});
const variant=process.env.VARIANT,phase=process.env.PHASE;
if(!['original','candidate'].includes(variant)||!['training','measured'].includes(phase))throw Error('Explicit VARIANT/PHASE required');
const pins=JSON.parse(await readFile('.context/jit-return-visit-v3/site-pins.json'));
const site=pins.sites[variant],expectedSeed=site['jit/seed.wasm'];
const profile=resolve('.context/jit-return-visit-v3/profiles',variant);
const origin=pins.origins[variant];
const references=resolve('.context/jit-return-visit-v3/references');await mkdir(references,{recursive:true});
const fixture=resolve('.context/graphics-speed/fixed-dismiss.snap');
const json=value=>JSON.stringify(value,(_,v)=>typeof v==='bigint'?v.toString():v,2);
if(JSON.parse(json({nested:[{cycles:123n}]})).nested[0].cycles!=='123')throw Error('JSON preflight');
const sha=b=>createHash('sha256').update(b).digest('hex');
const report={variant,expectedSeed,started:new Date().toISOString(),fixture,fixtureSha:sha(await readFile(fixture)),actions:[],errors:[],assets:[],profile,origin,pins,phase,validationPassed:true,seams:{app:site['app.js'],worker:site['emu-worker.js']},method:'Return-visit loader-only no-cache/ETag; fixed paused Import/512regions and1second resume; no Profiler/Debugger/interception; measured trace ends before resume; fixed1second idle+50ms paired raw validation; ordinary30second Save' };
const context=await chromium.launchPersistentContext(profile,{executablePath:'/usr/bin/google-chrome',args:['--no-sandbox','--disable-dev-shm-usage','--remote-debugging-port=9240'],viewport:{width:1280,height:1000},recordVideo:{dir:resolve(dir,'video'),size:{width:1280,height:1000}},serviceWorkers:'block'});
await context.addInitScript({content:'window.packFrame='+encodeBytes.toString()});
await context.addInitScript(()=>{const Original=window.Worker;window.Worker=class extends Original{constructor(url,options){super(url,options);if(String(url).includes('emu-worker.js'))window.profileWorker=this;}};let id=900000;window.profileCounters=()=>new Promise((resolve,reject)=>{const w=window.profileWorker,n=++id,start=performance.now();const timer=setTimeout(()=>{w.removeEventListener('message',listener);reject(Error('counter timeout'));},5000);function listener(e){if(e.data.type==='profileCounters'&&e.data.id===n){clearTimeout(timer);w.removeEventListener('message',listener);resolve({...e.data,mainSend:start,mainReply:performance.now()});}}w.addEventListener('message',listener);w.postMessage({type:'profileCounters',id:n});});});
const page=context.pages()[0]||await context.newPage();
const network=await context.newCDPSession(page);await network.send('Network.enable');await network.send('Network.setBypassServiceWorker',{bypass:true});report.network=[];network.on('Network.responseReceived',event=>{if(/\/(emu|seed)\.wasm(?:\?|$)/.test(event.response.url))report.network.push(event);});page.on('pageerror',e=>report.errors.push(e.message));
const assetTasks=[];context.on('response',r=>{if(/\/(emu|seed)\.wasm(?:\?|$)/.test(r.url()))assetTasks.push((async()=>{try{const bytes=await r.body();report.assets.push({url:r.url(),length:bytes.length,sha256:sha(bytes),headers:await r.allHeaders(),fromServiceWorker:r.fromServiceWorker()});}catch(e){report.assets.push({url:r.url(),error:e.message});}})());});
const version=await(await fetch('http://127.0.0.1:9240/json/version')).json();report.version=version;
const client=new RawCDP(version.webSocketDebuggerUrl);await client.ready;
let tracing=false;
async function processReceipt(phase){
 const info=(await client.call('SystemInfo.getProcessInfo')).processInfo;
 const entries=[];for(const name of await readdir('/proc'))if(/^\d+$/.test(name)){try{const raw=await readFile('/proc/'+name+'/stat','utf8'),end=raw.lastIndexOf(')'),fields=raw.slice(end+2).split(' ');entries.push({pid:Number(name),ppid:Number(fields[1]),startTicks:fields[19],exe:await readlink('/proc/'+name+'/exe')});}catch{}}
 const scoped=new Set(info.map(p=>p.id));let changed=true;while(changed){changed=false;for(const p of entries)if(scoped.has(p.ppid)&&!scoped.has(p.pid)){scoped.add(p.pid);changed=true;}}
 const receipt={phase,time:new Date().toISOString(),info,processes:entries.filter(p=>scoped.has(p.pid))};report.processReceipts??=[];report.processReceipts.push(receipt);await writeFile(resolve(dir,'process-receipt.json'),json({receipts:report.processReceipts}));return receipt;
}
report.processes=(await processReceipt('before-navigation')).info;const traceComplete=new Promise(resolve=>client.listeners.push(e=>{if(e.method==='Tracing.tracingComplete')resolve(e.params);}));
async function mark(label){if(tracing)await client.call('Tracing.recordClockSyncMarker',{syncId:label});}
async function counters(){return await page.evaluate(()=>window.profileCounters());}
async function importPaused(label){await mark(label+'-import-start');if(!await page.evaluate(()=>window.iphone.paused))await page.locator('#pause').click();await page.waitForFunction(()=>window.iphone.paused);await page.locator('#import').setInputFiles(fixture);await page.waitForFunction(()=>/backup is restored|Import failed/.test(document.querySelector('#status').textContent),null,{timeout:90000});if(/Import failed/.test(await page.locator('#status').textContent()))throw Error('Import failed');await page.waitForFunction(()=>!document.querySelector('#pause').disabled);let c;for(let n=0;n<100;n++){c=await counters();if(c.jit?.enabled&&c.jit.regions===512)break;await page.waitForTimeout(100);}if(!c.jit?.enabled||c.jit.regions!==512)throw Error('Seed not fully installed after paused import '+JSON.stringify(c));report[label+'Import']=c;await mark(label+'-import-end');}
async function stopTrace(){
 await client.call('Tracing.end');tracing=false;let timer;
 try{
  const complete=await Promise.race([traceComplete,new Promise((_,reject)=>{timer=setTimeout(()=>reject(Error('Trace completion timeout')),20000);})]);report.traceComplete=complete;
  await writeFile(resolve(dir,'report-partial.json'),json(report));if(complete.dataLossOccurred)throw Error('Trace data loss');
  let chunks=[];for(;;){const r=await client.call('IO.read',{handle:complete.stream,size:1048576});chunks.push(r.base64Encoded?Buffer.from(r.data,'base64'):Buffer.from(r.data));if(r.eof)break;}
  await client.call('IO.close',{handle:complete.stream});await writeFile(resolve(dir,'trace.json'),Buffer.concat(chunks));
 }finally{clearTimeout(timer);}
}
async function settled(label,kind,referenceLabel){
 const settledStart=performance.now();await page.waitForTimeout(1000);const evaluationStart=performance.now();
 const shot=await page.evaluate(async()=>{
  const canvas=document.querySelector('#screen'),ctx=canvas.getContext('2d'),begin=performance.now();
  const a=ctx.getImageData(0,0,320,480).data,firstDone=performance.now();await new Promise(r=>setTimeout(r,50));
  const secondAt=performance.now(),b=ctx.getImageData(0,0,320,480).data,secondDone=performance.now();
  const png=canvas.toDataURL('image/png'),pngDone=performance.now();
  const encodedA=window.packFrame(a),encodedB=window.packFrame(b),encodingDone=performance.now();
  const hashes=await Promise.all([crypto.subtle.digest('SHA-256',a),crypto.subtle.digest('SHA-256',b)]);
  const hex=buffer=>Array.from(new Uint8Array(buffer),v=>v.toString(16).padStart(2,'0')).join('');
  const result={a:{base64:encodedA,sha256:hex(hashes[0])},b:{base64:encodedB,sha256:hex(hashes[1])},png,separationMs:secondAt-begin};
  const end=performance.now();result.phaseMs={firstCapture:firstDone-begin,pairWait:secondAt-firstDone,secondCapture:secondDone-secondAt,png:pngDone-secondDone,encoding:encodingDone-pngDone,hash:end-encodingDone,inPageTotal:end-begin};return result;
 });
 const evaluationEnd=performance.now(),a=decodeFrame(shot.a),b=decodeFrame(shot.b),decodeEnd=performance.now(),name=label+'-'+kind;
 await writeFile(resolve(dir,name+'-before.rgba'),a);await writeFile(resolve(dir,name+'.rgba'),b);await writeFile(resolve(dir,name+'.png'),Buffer.from(shot.png.split(',')[1],'base64'));
 const writesEnd=performance.now(),stable=compareRaw(a,b);let reference;
 if(phase==='training'&&variant==='original'){
  if(stable.passed){await writeFile(resolve(references,name+'.rgba'),b);await writeFile(resolve(references,name+'.png'),Buffer.from(shot.png.split(',')[1],'base64'));}
  reference={passed:stable.passed,role:'independent original training reference',sha256:sha(b)};
 }else{const expected=await readFile(resolve(references,referenceLabel+'-'+kind+'.rgba'));reference={...compareRaw(expected,b),sha256:sha(expected)};}
 const done=performance.now();
 const result={fixedIdleMs:1000,requestedPairDelayMs:50,actualPairDelayMs:shot.separationMs,stable,reference,rawSha256:sha(b),passed:stable.passed&&reference.passed,phaseMs:{...shot.phaseMs,evaluationWall:evaluationEnd-evaluationStart,evaluationOutsideBody:evaluationEnd-evaluationStart-shot.phaseMs.inPageTotal,decodeAndIdentity:decodeEnd-evaluationEnd,rawPngWrites:writesEnd-decodeEnd,referenceAndCompare:done-writesEnd,totalSettled:done-settledStart}};
 if(!result.passed)report.validationPassed=false;return result;
}

try{
 const available=await client.call('Tracing.getCategories');report.availableCategories=available.categories;
 const wanted=['v8','v8.wasm','v8.wasm.detailed','disabled-by-default-v8.wasm.detailed','disabled-by-default-v8.compile','disabled-by-default-devtools.timeline'];const included=wanted.filter(x=>available.categories.includes(x));if(!included.includes('v8.wasm')||!included.includes('disabled-by-default-devtools.timeline')||!included.includes('disabled-by-default-v8.wasm.detailed'))throw Error('Required WASM/native-cache trace category absent');report.categories=included;
 await client.call('Tracing.start',{transferMode:'ReturnAsStream',streamFormat:'json',traceConfig:{recordMode:'recordUntilFull',traceBufferSizeInKb:131072,includedCategories:included}});tracing=true;
 await mark('navigation-start');await page.goto(origin);await page.waitForFunction(()=>window.iphone?.ready,null,{timeout:120000});await importPaused('C0');
 if(phase==='measured'){
  await stopTrace();
  report.cacheReceipt=JSON.parse(execFileSync('python3',['.context/jit-return-visit-v3/check-receipt.py',resolve(dir,'trace.json'),origin+'/jit/seed.wasm'],{encoding:'utf8'}));
  await writeFile(resolve(dir,'cache-receipt.json'),json(report.cacheReceipt));
  if(variant==='candidate'&&!report.cacheReceipt.accepted)throw Error('Candidate native cache receipt failed before timed resume');
  if(variant==='original'&&report.cacheReceipt.accepted)throw Error('Original unexpectedly accepted streaming seed cache');
 }
 await page.locator('#pause').click();await page.waitForFunction(()=>!window.iphone.paused);await page.waitForTimeout(1000);
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
 for(let round=1;round<=(phase==='training'?2:11);round++){const label=round===1?'C0':'H'+(round-1),referenceLabel=round===1?'C0':'H1';
  for(const kind of ['settings','home']){
   const point=kind==='settings'?{x:box.x+275/320*box.width,y:box.y+250/480*box.height}:await page.locator('[data-button="1"]').boundingBox().then(b=>({x:b.x+b.width/2,y:b.y+b.height/2}));
   await page.mouse.move(point.x,point.y);const before=await counters();await mark(label+'-'+kind+'-start');
   await page.evaluate(k=>window.phoneTiming.begin(k),kind);await page.mouse.down();await page.waitForTimeout(30);await page.mouse.up();await page.waitForFunction(()=>window.phoneTiming.get(),null,{timeout:95000});const result=await page.evaluate(()=>window.phoneTiming.get());await mark(label+'-'+kind+'-end');const after=await counters();
   const validation=await settled(label,kind,referenceLabel);report.actions.push({round,label,kind,before,after,...result,validation});await writeFile(resolve(dir,'report-partial.json'),json(report));
   console.log(JSON.stringify({label,kind,readyMs:result.releaseToReadyMs,retired:after.retired-before.retired,generated:after.jit.retired-before.jit.retired,validation:validation.passed,error:result.error}));if(result.error)throw Error(result.error);if(!(after.jit.retired>before.jit.retired))throw Error('No generated progress');
  }
 }
 if(!await page.evaluate(()=>window.iphone.paused))await page.locator('#pause').click();await page.waitForFunction(()=>window.iphone.paused);report.afterFixedWork=await counters();if(phase==='training'){await mark('serialization-wait-start');await page.waitForTimeout(10000);await mark('serialization-wait-end');report.serializationWaitMs=10000;}
 report.passed=report.errors.length===0&&report.validationPassed;
}catch(e){report.errors.push(e.stack||e.message);report.passed=false;}
finally{
 if(tracing){try{await stopTrace();}catch(e){report.errors.push(e.stack);report.passed=false;}}
 await Promise.allSettled(assetTasks);
 for(const [file,expected]of [['seed.wasm',expectedSeed],['emu.wasm','b3237ef3843412aea164d3db111299650ea8433bf16311417116799959ab6606']]){
  const served=report.assets.filter(a=>new URL(a.url).pathname.endsWith('/'+file));
  if(!served.length||served.some(a=>a.sha256!==expected)){report.errors.push('Actual served hash mismatch: '+file);report.passed=false;}
 }
 try{await processReceipt('before-close');}catch(e){report.errors.push('Process receipt: '+e.message);report.passed=false;}
 client.close();await context.close();report.video=await page.video().path();report.browserClosed=true;report.passed=report.passed&&report.errors.length===0;await writeFile(resolve(dir,'report.json'),json(report));console.log(JSON.stringify({finished:true,passed:report.passed,errors:report.errors}));
}
if(!report.passed)process.exitCode=1;
