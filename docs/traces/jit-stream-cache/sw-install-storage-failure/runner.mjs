import {chromium} from 'playwright';
import {readFile,writeFile,mkdir,readdir,readlink} from 'node:fs/promises';
import {resolve} from 'node:path';
import {createHash} from 'node:crypto';
import {execFileSync} from 'node:child_process';
import {RawCDP} from '../../docs/traces/browser-first-use/cdp.mjs';
import {encodeBytes,decodeFrame} from './frame-packet.mjs';
import {compareRaw} from './frames.mjs';
const base=resolve('.context/jit-sw-cache'),phase=process.env.PHASE,dir=resolve(process.env.ARTIFACT_DIR);
if(!['train','return'].includes(phase))throw Error('Explicit phase required');
const pins=JSON.parse(await readFile(resolve(base,'site-pins.json'))),site=pins.sites.candidate,origin=pins.origin,profile=resolve(base,'profile');
const json=x=>JSON.stringify(x,(_,v)=>typeof v==='bigint'?v.toString():v,2),sha=x=>createHash('sha256').update(x).digest('hex');
const report={phase,started:new Date().toISOString(),scope:'Functional offline CacheStorage/native-code reuse and real autosave; no speed measurement. Installer is an explicit mechanism fixture, not a user UI flow.',checks:[],errors:[],assets:[],frames:[],profile,pins};
const check=(name,okay,details={})=>{report.checks.push({name,passed:Boolean(okay),...details});if(!okay)throw Error(name);};
await mkdir(dir,{recursive:true});
const context=await chromium.launchPersistentContext(profile,{executablePath:'/usr/bin/google-chrome',args:['--no-sandbox','--disable-dev-shm-usage','--remote-debugging-port=9241'],viewport:{width:1280,height:1000},recordVideo:{dir:resolve(dir,'video'),size:{width:1280,height:1000}},serviceWorkers:'allow'});
const page=context.pages()[0]||await context.newPage();const assets=[];
context.on('response',r=>{const url=new URL(r.url());if(url.origin!==origin)return;const path=url.pathname.slice(1);if(!site[path])return;assets.push((async()=>{try{const installer=r.request().serviceWorker();if(installer){report.assets.push({url:r.url(),path,requestServiceWorkerURL:installer.url(),bodyVerifiedViaInstalledCache:true,fromServiceWorker:r.fromServiceWorker(),headers:await r.allHeaders(),receivedUTC:new Date().toISOString()});return;}const bytes=await r.body();report.assets.push({url:r.url(),path,sha256:sha(bytes),length:bytes.length,fromServiceWorker:r.fromServiceWorker(),headers:await r.allHeaders(),receivedUTC:new Date().toISOString()});}catch(e){report.assets.push({url:r.url(),path,error:e.message});}})());});
page.on('pageerror',e=>report.errors.push(e.stack||e.message));report.console=[];page.on('console',e=>{if(e.type()==='error')report.console.push(e.text());});
await context.addInitScript({content:'window.packFrame='+encodeBytes.toString()});
await context.addInitScript(({returning})=>{
 window.savedIdentity=async()=>{
  const exists=(await indexedDB.databases()).some(x=>x.name==='iphone-1');if(!exists)return null;
  const db=await new Promise((res,rej)=>{const r=indexedDB.open('iphone-1',1);r.onsuccess=()=>res(r.result);r.onerror=()=>rej(r.error);});
  const record=await new Promise((res,rej)=>{const r=db.transaction('checkpoints').objectStore('checkpoints').get('current');r.onsuccess=()=>res(r.result);r.onerror=()=>rej(r.error);});db.close();if(!record)return null;
  const {data,...metadata}=record,bytes=await data.arrayBuffer(),hash=await crypto.subtle.digest('SHA-256',bytes);
  return {metadata,blobBytes:data.size,blobType:data.type,sha256:[...new Uint8Array(hash)].map(x=>x.toString(16).padStart(2,'0')).join('')};
 };
 if(returning)window.initialSavedIdentity=window.savedIdentity();
},{returning:phase==='return'});
const version=await(await fetch('http://127.0.0.1:9241/json/version')).json();const client=new RawCDP(version.webSocketDebuggerUrl);await client.ready;report.version=version;
async function processes(label){
 const info=(await client.call('SystemInfo.getProcessInfo')).processInfo,entries=[];
 for(const n of await readdir('/proc'))if(/^\d+$/.test(n)){try{const s=await readFile('/proc/'+n+'/stat','utf8'),f=s.slice(s.lastIndexOf(')')+2).split(' ');entries.push({pid:Number(n),ppid:Number(f[1]),startTicks:f[19],exe:await readlink('/proc/'+n+'/exe')});}catch{}}
 const scoped=new Set(info.map(p=>p.id));let changed=true;while(changed){changed=false;for(const p of entries)if(scoped.has(p.ppid)&&!scoped.has(p.pid)){scoped.add(p.pid);changed=true;}}
 report.processReceipts??=[];report.processReceipts.push({label,utc:new Date().toISOString(),info,processes:entries.filter(p=>scoped.has(p.pid))});await writeFile(resolve(dir,'process-receipt.json'),json({receipts:report.processReceipts}));
}
await processes('before-navigation');let tracing=false;
const traceComplete=new Promise(resolve=>client.listeners.push(e=>{if(e.method==='Tracing.tracingComplete')resolve(e.params);}));
async function startTrace(){const available=(await client.call('Tracing.getCategories')).categories;const included=['v8','v8.wasm','disabled-by-default-v8.wasm.detailed','disabled-by-default-devtools.timeline'].filter(x=>available.includes(x));check('required cache trace categories',included.length===4);await client.call('Tracing.start',{transferMode:'ReturnAsStream',streamFormat:'json',traceConfig:{recordMode:'recordUntilFull',traceBufferSizeInKb:131072,includedCategories:included}});tracing=true;}
async function stopTrace(){await client.call('Tracing.end');tracing=false;let timer;try{const complete=await Promise.race([traceComplete,new Promise((_,reject)=>{timer=setTimeout(()=>reject(Error('Trace completion bound')),20000);})]);report.traceComplete=complete;check('trace complete without loss',complete.dataLossOccurred===false);let chunks=[];for(;;){const r=await client.call('IO.read',{handle:complete.stream,size:1048576});chunks.push(r.base64Encoded?Buffer.from(r.data,'base64'):Buffer.from(r.data));if(r.eof)break;}await client.call('IO.close',{handle:complete.stream});await writeFile(resolve(dir,'trace.json'),Buffer.concat(chunks));}finally{clearTimeout(timer);}}
async function frame(name,kind){
 await page.waitForTimeout(1000);const shot=await page.evaluate(async()=>{const c=document.querySelector('#screen'),ctx=c.getContext('2d'),a=ctx.getImageData(0,0,320,480).data;await new Promise(r=>setTimeout(r,50));const b=ctx.getImageData(0,0,320,480).data,png=c.toDataURL('image/png');const hashes=await Promise.all([crypto.subtle.digest('SHA-256',a),crypto.subtle.digest('SHA-256',b)]);const hex=x=>[...new Uint8Array(x)].map(v=>v.toString(16).padStart(2,'0')).join('');return {a:{base64:window.packFrame(a),sha256:hex(hashes[0])},b:{base64:window.packFrame(b),sha256:hex(hashes[1])},png};});
 const a=decodeFrame(shot.a),b=decodeFrame(shot.b),reference=await readFile(resolve('.context/jit-return-visit-v3/references','C0-'+kind+'.rgba'));const stable=compareRaw(a,b),match=compareRaw(reference,b);
 await writeFile(resolve(dir,name+'-before.rgba'),a);await writeFile(resolve(dir,name+'.rgba'),b);await writeFile(resolve(dir,name+'.png'),Buffer.from(shot.png.split(',')[1],'base64'));await page.screenshot({path:resolve(dir,name+'-page.png')});
 const entry={name,kind,stable,reference:match,rawSha256:sha(b)};report.frames.push(entry);check('strict full guest frame '+name,stable.passed&&match.passed,entry);return b;
}
async function ready(){await page.waitForFunction(()=>window.iphone?.ready&&Number(document.querySelector('#screen').dataset.frame)>0,null,{timeout:90000});await page.waitForFunction(()=>window.iphone.metrics.jit?.enabled&&window.iphone.metrics.jit.retired>0,null,{timeout:30000});check('real phone ready with positive generated work',true,{metrics:await page.evaluate(()=>window.iphone.metrics)});check('latest startup indicator dismissed',await page.locator('#boot-loader').evaluate(x=>x.hidden));check('latest Save control remains hidden',!await page.locator('#save').isVisible());}
async function navigate(kind){
 const b=await page.locator('#screen').boundingBox(),p=kind==='settings'?{x:b.x+275*b.width/320,y:b.y+250*b.height/480}:await page.locator('[data-button="1"]').boundingBox().then(x=>({x:x.x+x.width/2,y:x.y+x.height/2}));
 await page.mouse.move(p.x,p.y);await page.mouse.down();await page.waitForTimeout(180);await page.mouse.up();
 await page.waitForFunction(k=>{const d=document.querySelector('#screen').getContext('2d').getImageData(0,30,320,450).data;let white=0,n=0;for(let i=0;i<d.length;i+=64){n++;if(Math.min(d[i],d[i+1],d[i+2])>235)white++;}return k==='settings'?white/n>.25:white/n<.10;},kind,{timeout:90000});await page.waitForTimeout(500);
}
try{
 if(phase==='train'){
  await page.goto(origin+'/setup.html');await page.waitForFunction(()=>typeof window.installOffline==='function');report.setup=await page.evaluate(()=>window.installOffline());check('real offline worker activated',report.setup.scriptURL===origin+'/service-worker.js');
  report.cacheEntries=await page.evaluate(async()=>{const names=(await caches.keys()).filter(n=>n.startsWith('iphone-1-shell-'));if(names.length!==1)throw Error('Expected one shell cache');const cache=await caches.open(names[0]),out=[];for(const request of await cache.keys()){const response=await cache.match(request),bytes=await response.arrayBuffer(),hash=await crypto.subtle.digest('SHA-256',bytes);out.push({url:request.url,sha256:[...new Uint8Array(hash)].map(x=>x.toString(16).padStart(2,'0')).join('')});}return {name:names[0],entries:out};});
  for(const e of report.cacheEntries.entries)check('verified installed shell '+new URL(e.url).pathname,e.sha256===site[new URL(e.url).pathname.slice(1)]);
 }
 await context.setOffline(true);report.disconnectedUTC=new Date().toISOString();await startTrace();await page.goto(origin);check('network disconnected',await page.evaluate(()=>navigator.onLine===false));check('real service worker controls phone',await page.evaluate(()=>Boolean(navigator.serviceWorker.controller)));
 if(phase==='return'){report.initialSaved=await page.evaluate(()=>window.initialSavedIdentity);const expected=JSON.parse(await readFile(resolve(base,'saved-after-training.json')));check('saved metadata and compressed bytes unchanged across process close',JSON.stringify(report.initialSaved)===JSON.stringify(expected));}
 await ready();
 if(phase==='train'){
  await frame('initial-home','home');for(let i=0;i<2;i++){await navigate('settings');await frame('cycle'+i+'-settings','settings');await navigate('home');await frame('cycle'+i+'-home','home');}
  await navigate('settings');await frame('autosave-settings','settings');const after=Date.now();report.autosaveWaitAfter=after;
  await page.waitForFunction(async since=>{const m=await import('./storage.js');const r=await m.readCheckpoint();return r&&r.savedAt>since;},after,{timeout:75000,polling:500});check('normal30-second autosave creates newer checkpoint',true);
  await page.waitForTimeout(10000);report.serializationWaitMs=10000;report.savedAfterTraining=await page.evaluate(()=>window.savedIdentity());check('actual compressed autosave record',report.savedAfterTraining?.metadata?.encoding==='gzip'&&report.savedAfterTraining.metadata.savedAt>after);
  await frame('saved-settings','settings');
 }else{
  await frame('restored-settings','settings');await navigate('home');await frame('returned-home','home');await navigate('settings');await frame('returned-settings','settings');
 }
 await stopTrace();report.cacheReceipt=JSON.parse(execFileSync('python3',[resolve(base,'check-receipt.py'),resolve(dir,'trace.json'),origin+'/jit/seed.wasm'],{encoding:'utf8'}));await writeFile(resolve(dir,'cache-receipt.json'),json(report.cacheReceipt));check('native-cache receipt matches phase',phase==='return'?report.cacheReceipt.accepted:!report.cacheReceipt.accepted);
 report.finalMetrics=await page.evaluate(()=>window.iphone.metrics);check('generated work remains active',report.finalMetrics.jit.enabled&&report.finalMetrics.jit.retired>0);report.passed=report.errors.length===0;
}catch(e){report.errors.push(e.stack||e.message);report.passed=false;}
finally{
 if(tracing)try{await stopTrace();}catch(e){report.errors.push(e.stack);report.passed=false;}
 await Promise.allSettled(assets);
 for(const asset of report.assets){const installed=asset.bodyVerifiedViaInstalledCache&&report.cacheEntries?.entries.some(e=>e.url===asset.url&&e.sha256===site[asset.path]);if(asset.error||(!installed&&asset.sha256!==site[asset.path])){report.errors.push('Actual response identity failed: '+asset.path+' '+(asset.error||asset.sha256));report.passed=false;}}
 for(const name of ['jit/seed.wasm','dist/emu.wasm']){const served=report.assets.filter(a=>a.path===name&&a.fromServiceWorker);if(!served.length){report.errors.push('Missing actual worker response from SW: '+name);report.passed=false;}}
 if(phase==='train'&&report.passed){try{report.closingSaved=await page.evaluate(()=>window.savedIdentity());check('closing checkpoint still contains real autosave',report.closingSaved?.metadata.savedAt>report.autosaveWaitAfter);await writeFile(resolve(base,'saved-after-training.json'),json(report.closingSaved));}catch(e){report.errors.push(e.stack);report.passed=false;}}
 try{await processes('before-close');}catch(e){report.errors.push(e.stack);report.passed=false;}
 client.close();await context.close();report.video=await page.video().path();report.browserClosed=true;report.passed=report.passed&&report.errors.length===0;await writeFile(resolve(dir,'report.json'),json(report));console.log(json({passed:report.passed,phase,checks:report.checks.length,frames:report.frames.length,errors:report.errors}));
}
if(!report.passed)process.exitCode=1;
