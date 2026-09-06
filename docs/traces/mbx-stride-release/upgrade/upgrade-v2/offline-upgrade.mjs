// Saved-phone upgrade gate for the narrow MBX render-target stride repair.
// Exact old/new cores differ; diagnostic shell/manifest markers identify test generations.
// All serving/cache/profile mutations are isolated from the live web root.
import {chromium} from 'playwright';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import {resolve, dirname, join} from 'node:path';
import {createHash} from 'node:crypto';
import {gzipSync} from 'node:zlib';
import {validateAssets,responseMime} from './validate-assets.mjs';
import {encodeBytes,decodeFrame} from '../../jit-fallback-census/frame-packet.mjs';
import {createServer} from 'node:http';
import {createReadStream} from 'node:fs';
import {trackSockets, stopServer} from '../../jit-sw-cache-v4/physical-server.mjs';

const directory = resolve('.context/mbx-stride-integration/upgrade-v2/run-1');
const root = join(directory, 'web');
const sourceRoot = resolve('.context/mbx-stride-integration/candidate');
const previousRoot = resolve('.context/mbx-stride-integration/previous');
assert.notEqual(root,sourceRoot,'Upgrade test must use an isolated web root');
assert.notEqual(root,previousRoot,'Upgrade test must preserve its previous firmware source');
const port = 8236;
const url = `http://localhost:${port}/`;
const files = ['index.html','phone.css','icon.svg','app.js','storage.js','assets.js',
  'checkpoint-worker.js','emu-worker.js','manifest.json','dist/emu.js','dist/emu.wasm',
  'seed-jit.js','jit/seed.wasm','jit/regions.json','jit/abi.json'];
const json = value => JSON.stringify(value,(_,v)=>typeof v==='bigint'?v.toString():v,2);
const sha = bytes => createHash('sha256').update(bytes).digest('hex');
await fs.mkdir(root, {recursive:true});
// Link immutable firmware within the isolated root; do not alter production files.
for (const name of await fs.readdir(join(sourceRoot, 'firmware'))) {
  const source = join(sourceRoot, 'firmware', name), target = join(root, 'firmware', name);
  if (!(await fs.stat(source)).isFile()) continue;
  await fs.mkdir(dirname(target), {recursive:true});
  // The artifact directory may contain links from an older firmware run.
  // Replacing the link does not modify the preserved source inode.
  await fs.rm(target,{force:true});
  await fs.link(source,target);
}
let phase='prepare';const servedAssets=[],bodyTasks=[],workerEvents=[];let requestSequence=0,pageSequence=0;const requestIds=new WeakMap(),pageIds=new WeakMap();
function requestMeta(request){if(!requestIds.has(request))requestIds.set(request,++requestSequence);let pageId=null;try{pageId=pageIds.get(request.frame().page())??null;}catch{}return {requestId:requestIds.get(request),pageId};}const originRequests=[];const browserRequests=[];const browserResponses=[];const requestFailures=[];const consoleMessages=[];const barriers=[];const actions=[];
const source = Object.fromEntries(await Promise.all(files.map(async name => [name, await fs.readFile(join(sourceRoot,name))])));
const previousSource=Object.fromEntries(await Promise.all(files.map(async name=>[name,await fs.readFile(join(previousRoot,name))])));
const previousManifest=previousSource['manifest.json'];
const template = await fs.readFile(join(sourceRoot,'service-worker.template.js'),'utf8');
const variants = {};
async function publish(version) {
  const selected=version==='A'?previousSource:source;const contents = {...selected};
  contents['app.js'] = Buffer.concat([selected['app.js'], Buffer.from(`\ndocument.documentElement.dataset.testBundle = '${version}';\n`)]);
  const firmwareManifest = JSON.parse(version === 'A' && previousManifest ? previousManifest : source['manifest.json']);
  contents['manifest.json'] = Buffer.from(JSON.stringify({...firmwareManifest, testBundle:version}));
  if (previousRoot) {
    // Exercise a real runtime generation change while retaining the saved
    // phone. Replace directory entries, never mutate shared hardlinked bytes.
    const firmwareRoot = version === 'A' ? previousRoot : sourceRoot;
    for (const name of new Set(['kernel','devicetree','rootfs','snapshot'].flatMap(key => [firmwareManifest[key]].flat()).filter(name=>Boolean(name)&&!/^https?:/.test(name)))) {
      const target = join(root,name), staged = `${target}.next`;
      await fs.mkdir(dirname(target),{recursive:true});
      await fs.link(join(firmwareRoot,name),staged);
      await fs.rename(staged,target);
      // POSIX rename is a no-op when both names already identify the same
      // inode (unchanged kernel/tree), so remove that leftover staging name.
      await fs.rm(staged,{force:true});
    }
  }
  // Actual old/new WASM bytes remain exact: no synthetic custom section.
  const digest = createHash('sha256').update(template), hashes = {};
  for (const name of files) {
    digest.update(name).update(contents[name]); hashes[name]=sha(contents[name]);
    const path=join(root,name); await fs.mkdir(dirname(path),{recursive:true});
    await fs.writeFile(path, contents[name]);
  }
  const generation = digest.digest('hex').slice(0,24);
  const sw=template.replace('__VERSION__',generation).replace('__FILES__',JSON.stringify(hashes));
  await fs.writeFile(join(root,'service-worker.next'),sw);
  await fs.rename(join(root,'service-worker.next'),join(root,'service-worker.js'));
  hashes['service-worker.js']=sha(Buffer.from(sw));return variants[version]={generation,hashes};
}
assert.equal(sha(previousSource['dist/emu.wasm']),'7a72add6d886440f64a853511375ba51cbbb23f2263831b6a05c51fde7969088');
assert.equal(sha(source['dist/emu.wasm']),'3b43ccf4a8292a4da776491aa92185b0ea88e2fc38a3002a6e1fe1d5a6aa4463');
assert.equal(sha(source['seed-jit.js']),'3c2cc4122a9b32ab22db44d253e26920650c885c1c019b170a2951002ce597e9');
assert.equal(sha(source['jit/seed.wasm']),sha(previousSource['jit/seed.wasm']));
const disk=await fs.statfs(directory);assert(disk.bavail*disk.bsize>=6*1073741824,'Require6GiB initial disk headroom');
await publish('A');
let server,sockets;
async function startOrigin(){
 server=createServer(async(req,res)=>{originRequests.push({phase,url:req.url,method:req.method,at:Date.now()});try{const pathname=decodeURIComponent(new URL(req.url,url).pathname);const file=await fs.realpath(resolve(root,`.${pathname.endsWith('/')?pathname+'index.html':pathname}`));if(!file.startsWith(root+'/')){res.writeHead(403);res.end();return;}const stat=await fs.stat(file);if(!stat.isFile())throw Error('not file');const type=file.endsWith('.wasm')?'application/wasm':file.endsWith('.js')?'text/javascript':file.endsWith('.json')?'application/json':file.endsWith('.html')?'text/html':file.endsWith('.css')?'text/css':file.endsWith('.svg')?'image/svg+xml':'application/octet-stream';res.writeHead(200,{'Content-Type':responseMime(type),'Content-Length':stat.size,'Cache-Control':'no-cache','X-Content-Type-Options':'nosniff'});createReadStream(file).on('error',()=>res.destroy()).pipe(res);}catch{res.writeHead(404);res.end('Not found')}});
 sockets=trackSockets(server);await new Promise((resolve,reject)=>{server.once('error',reject);server.listen(port,'127.0.0.1',resolve)});
}
await startOrigin();
const browser=await chromium.launch({executablePath:'/usr/bin/google-chrome',args:['--no-sandbox','--disable-dev-shm-usage']});
const context=await browser.newContext({viewport:{width:1280,height:1050},
  ...(process.argv.includes('--record') ? {recordVideo:{dir:directory,size:{width:1280,height:1050}}}: {})});
await context.addInitScript({content:'window.packDiagnosticFrame='+encodeBytes.toString()});
context.on('page',p=>{const pageId=++pageSequence;pageIds.set(p,pageId);p.on('worker',w=>{const row={pageId,url:w.url(),created:Date.now(),createdPhase:phase};workerEvents.push(row);w.on('close',()=>{row.closed=Date.now();row.closedPhase=phase;});});});
context.on('request',r=>browserRequests.push({...requestMeta(r),phase,url:r.url(),method:r.method(),at:Date.now()}));
context.on('response',r=>{
 const row={...requestMeta(r.request()),phase,url:r.url(),status:r.status(),fromServiceWorker:r.fromServiceWorker(),headers:r.headers(),at:Date.now()};browserResponses.push(row);
 const u=new URL(row.url),path=u.pathname.replace(/^\//,'')||'index.html';
 if(u.origin===new URL(url).origin&&[...files,'service-worker.js'].includes(path))bodyTasks.push((async()=>{
  const item={...row,path};servedAssets.push(item);
  try{const bytes=await r.body();item.bytes=bytes.length;item.sha256=sha(bytes);await fs.mkdir(join(directory,'served'),{recursive:true});item.file='served/'+item.sha256+'.bin';await fs.writeFile(join(directory,item.file),bytes,{flag:'wx'}).catch(e=>{if(e.code!=='EEXIST')throw e;});}catch(e){item.error=e.message;item.errorAt=Date.now();}
 })());
});
context.on('requestfailed',r=>requestFailures.push({...requestMeta(r),phase,url:r.url(),failure:r.failure(),at:Date.now()}));
context.on('console',m=>consoleMessages.push({phase,type:m.type(),text:m.text(),at:Date.now()}));
let page=await context.newPage();
const report={processReceipts:[],startedAt:new Date().toISOString(),sourceRoot,previousRoot,checks:[],errors:[],servedAssets,workerEvents,variants,originRequests,browserRequests,browserResponses,requestFailures,consoleMessages,barriers,actions};
let reportWrite=Promise.resolve();const persist=()=>{reportWrite=reportWrite.then(()=>fs.writeFile(join(directory,'report.json'),json(report)+'\n'));return reportWrite;};
await persist();
const browserCDP=await browser.newBrowserCDPSession();
async function processes(label){
 const info=(await browserCDP.send('SystemInfo.getProcessInfo')).processInfo;const identities=[];
 for(const item of info){try{const raw=await fs.readFile('/proc/'+item.id+'/stat','utf8'),f=raw.slice(raw.lastIndexOf(')')+2).split(' ');identities.push({pid:item.id,startTicks:f[19],type:item.type});}catch{}}
 report.processReceipts.push({label,at:Date.now(),identities});await fs.writeFile(join(directory,'process-receipts.json'),json(report.processReceipts)+'\n');
}
await processes('before-navigation');
const began=performance.now();const outer=setTimeout(()=>{report.result='fail';report.error='600s outer workflow cap';fs.writeFile(join(directory,'report.json'),json(report)+'\n').finally(async()=>{await browser.close().catch(()=>{});if(server.listening)await stopServer(server,sockets,port).catch(()=>{});process.exit(124)});},600000);
const check=(name,condition,detail)=> { assert.ok(condition,name); report.checks.push({name,atSeconds:(performance.now()-began)/1000,...(detail?{detail}:{})});void persist(); };
const observe=page=>page.on('pageerror',error=>report.errors.push(error.message)); observe(page);
const ready=async()=> { await page.waitForFunction(()=>window.iphone?.ready,{}, {timeout:60000}); };
const pause=async()=> { if (!await page.evaluate(()=>window.iphone.paused)) {await diagnosticClick('pause');await page.waitForFunction(()=>window.iphone.paused);} };
async function bundle() {
  return page.evaluate(async()=> {
    const bytes=await (await fetch('dist/emu.wasm')).arrayBuffer();
    const hash=[...new Uint8Array(await crypto.subtle.digest('SHA-256',bytes))].map(n=>n.toString(16).padStart(2,'0')).join('');
    return {shell:document.documentElement.dataset.testBundle,manifest:(await (await fetch('manifest.json')).json()).testBundle,wasm:hash};
  });
}
async function installedEngine() {
  // Restored checkpoints can be paused before the worker's first metrics
  // message. Briefly resume only the live guest to observe installation, then
  // restore Pause. The durable checkpoint bytes remain untouched.
  const wasPaused=await page.evaluate(()=>window.iphone.paused);
  if(wasPaused) await diagnosticClick('pause');
  await page.waitForFunction(()=>window.iphone.metrics.jit?.enabled &&
    window.iphone.metrics.jit.regions>0 && window.iphone.metrics.jit.calls>0 && window.iphone.metrics.jit.retired>0,{}, {timeout:30000});
  const initial=await page.evaluate(()=>window.iphone.metrics.jit);await page.waitForFunction(old=>window.iphone.metrics.jit.retired>old.retired && window.iphone.metrics.jit.calls>old.calls,initial,{timeout:15000});
  const stats=await page.evaluate(()=>window.iphone.metrics.jit);
  if(wasPaused) await pause();
  return stats;
}
async function saved() {
  return page.evaluate(async()=> {
    const {readCheckpoint}=await import('./storage.js'); const record=await readCheckpoint();
    const hash=[...new Uint8Array(await crypto.subtle.digest('SHA-256',await record.data.arrayBuffer()))].map(n=>n.toString(16).padStart(2,'0')).join('');
    return {savedAt:record.savedAt,version:record.version,size:record.size,storedBytes:record.data.size,hash,metadata:Object.fromEntries(Object.entries(record).filter(([key])=>key!=='data'))};
  });
}
async function screen(path){
 const packet=await page.evaluate(async()=>{const c=document.querySelector('#screen');if(!c)return null;const raw=c.getContext('2d').getImageData(0,0,320,480).data;return {base64:window.packDiagnosticFrame(raw),sha256:[...new Uint8Array(await crypto.subtle.digest('SHA-256',raw))].map(x=>x.toString(16).padStart(2,'0')).join('')};});
 if(packet)await fs.writeFile(path.replace(/\.png$/,'.rgba.gz'),gzipSync(decodeFrame(packet)));
 return page.screenshot({path});
}
async function diagnosticClick(id){
 await page.locator('#'+id).waitFor({state:'attached',timeout:90000});
 actions.push({phase,id,stage:'before wait',...await page.evaluate(id=>({disabled:document.getElementById(id).disabled,status:document.querySelector('#status').textContent,savedAt:iphone.savedAt,at:Date.now()}),id)});
 await page.waitForFunction(id=>!document.getElementById(id).disabled,id,{timeout:90000});
 const receipt=await page.locator('#'+id).evaluate(b=>{const before={disabled:b.disabled,status:document.querySelector('#status').textContent,savedAt:iphone.savedAt,at:Date.now()};if(b.disabled)return {clicked:false,before};b.click();return{clicked:true,before}});actions.push({phase,id,stage:'atomic enabled click',...receipt});assert(receipt.clicked,'Diagnostic control became disabled before click');
}
async function goOffline(name){
 phase='closing-'+name;await context.setOffline(true);const closed=await stopServer(server,sockets,port);phase=name;const before=originRequests.length;barriers.push({phase,originRequestCount:before,...closed});
 const nonce='offline-proof-'+Date.now();const remote='https://iphone.files.garden/'+nonce;
 const probes=await page.evaluate(async({nonce,remote})=>{const request=async url=>{try{const r=await fetch(url,{cache:'no-store'});return {url,status:r.status}}catch(e){return{url,error:e.message}}};const main=await request('/'+nonce),external=await request(remote);const text=`Promise.all([fetch(${JSON.stringify('/'+nonce+'-worker')},{cache:'no-store'}).then(r=>({status:r.status})).catch(e=>({error:e.message})),fetch(${JSON.stringify(remote+'-worker')},{cache:'no-store'}).then(r=>({status:r.status})).catch(e=>({error:e.message}))]).then(result=>postMessage(result));`;const u=URL.createObjectURL(new Blob([text],{type:'text/javascript'}));const worker=await new Promise((resolve,reject)=>{const w=new Worker(u),timer=setTimeout(()=>{w.terminate();reject(Error('Offline worker probe timed out'))},5000);w.onmessage=e=>{clearTimeout(timer);w.terminate();resolve(e.data)};w.onerror=e=>{clearTimeout(timer);w.terminate();reject(Error(e.message))}});URL.revokeObjectURL(u);return{main,external,worker}}, {nonce,remote});
 const sw=context.serviceWorkers().find(w=>w.url().startsWith(url));assert(sw,'Active worker required');probes.serviceWorker=await sw.evaluate(async remote=>{try{const r=await fetch(remote+'-sw',{cache:'no-store'});return {status:r.status}}catch(e){return{error:e.message}}},remote);
 assert(probes.main.error&&probes.external.error&&probes.worker.every(x=>x.error)&&probes.serviceWorker.error,'Every actual offline transport probe must fail');assert.equal(originRequests.length,before,'Closed origin receives no requests');barriers.at(-1).probes=probes;
}
async function goOnline(name){phase=name;await startOrigin();await context.setOffline(false);}
async function cacheName(){return page.evaluate(async()=>{const names=(await caches.keys()).filter(n=>n.startsWith('iphone-1-shell-'));if(names.length!==1)throw Error('Expected one active shell cache');return names[0]});}
async function extraGates(original){
 // Explicit optional accelerator corruption: finite cached data only, never a fabricated core.
 phase='corrupt-seed-offline';const shell=await cacheName();await page.evaluate(async name=>{await(await caches.open(name)).put(new URL('jit/seed.wasm',location.href),new Response('corrupt optional seed',{headers:{'Content-Type':'application/wasm'}}))},shell);
 await page.reload();await ready();await page.waitForFunction(()=>window.iphone.metrics.jit&&!iphone.metrics.jit.enabled,{}, {timeout:30000});const first=await page.evaluate(()=>String(iphone.metrics.cycles));await page.waitForFunction(old=>BigInt(iphone.metrics.cycles)>BigInt(old),first,{timeout:15000});await pause();
 check('corrupt optional offline seed falls back to actual running interpreter',await page.evaluate(()=>iphone.ready&&!iphone.metrics.jit.enabled));check('optional fallback preserves the original saved phone',JSON.stringify(await saved())===JSON.stringify(original));
 // Restore exact seed bytes to this disposable cache; retained snapshot is untouched.
 await page.evaluate(async({name,bytes})=>{const raw=Uint8Array.from(atob(bytes),c=>c.charCodeAt(0));await(await caches.open(name)).put(new URL('jit/seed.wasm',location.href),new Response(raw,{headers:{'Content-Type':'application/wasm'}}))},{name:shell,bytes:source['jit/seed.wasm'].toString('base64')});
 await goOnline('corrupt-update-C');await publish('C');await fs.writeFile(join(root,'dist/emu.wasm'),Buffer.from('intentionally corrupt update body'));
 await page.evaluate(async()=>{const registration=await navigator.serviceWorker.getRegistration();window.upgradeInstallStates=[];registration.addEventListener('updatefound',()=>{const worker=registration.installing;upgradeInstallStates.push(worker.state);worker.addEventListener('statechange',()=>upgradeInstallStates.push(worker.state))});await registration.update()});
 await page.waitForFunction(()=>window.upgradeInstallStates.includes('redundant'),{}, {timeout:60000});report.corruptUpdateStates=await page.evaluate(()=>upgradeInstallStates);check('corrupt update is rejected and leaves no waiting replacement',await page.evaluate(async()=>!(await navigator.serviceWorker.getRegistration()).waiting));
 check('failed update deletes its incomplete shell cache',!await page.evaluate(async name=>(await caches.keys()).includes(name),'iphone-1-shell-'+variants.C.generation));
 const afterBad=await bundle();check('corrupt update retains matching active B shell and actual MBX stride-fixed runtime',afterBad.shell==='B'&&afterBad.wasm===variants.B.hashes['dist/emu.wasm']);check('failed update preserves original saved phone',JSON.stringify(await saved())===JSON.stringify(original));
 await publish('B');await goOffline('corrupt-kernel-offline');
 const corruption=await page.evaluate(async()=>{const manifest=await(await fetch('manifest.json')).json(),kernelURL=new URL(manifest.kernel,location.href).href,cache=await caches.open('iphone-1-assets-1a543a-v1');await cache.put(kernelURL,new Response('corrupted test entry'));const bytes=await(await cache.match(kernelURL)).arrayBuffer();return {kernelURL,bytes:bytes.byteLength,sha256:[...new Uint8Array(await crypto.subtle.digest('SHA-256',bytes))].map(x=>x.toString(16).padStart(2,'0')).join('')};});
 report.negativeStartup={phase,corruption,pageId:pageIds.get(page),started:Date.now(),physicalBarrier:barriers.at(-1)};
 await page.reload();await page.waitForFunction(()=>!document.querySelector('#retry').hidden&&!document.querySelector('#retry').disabled,{}, {timeout:30000});
 check('corrupt offline firmware stops the guest and enables recovery',!await page.evaluate(()=>iphone.ready));check('corrupt cache entry was removed',await page.evaluate(async()=>{const m=await(await fetch('manifest.json')).json();return !(await(await caches.open('iphone-1-assets-1a543a-v1')).match(new URL(m.kernel,location.href)))}));await screen(join(directory,'corrupt-offline.png'));
 report.negativeStartup.observed=await page.evaluate(async()=>{const m=await(await fetch('manifest.json')).json();return {ready:iphone.ready,retryEnabled:!document.querySelector('#retry').hidden&&!document.querySelector('#retry').disabled,kernelURL:new URL(m.kernel,location.href).href,kernelCacheRemoved:!(await(await caches.open('iphone-1-assets-1a543a-v1')).match(new URL(m.kernel,location.href))),status:document.querySelector('#status').textContent};});
 await page.waitForTimeout(50);report.negativeStartup.ended=Date.now();report.negativeStartup.workerClosures=workerEvents.filter(w=>w.pageId===report.negativeStartup.pageId&&w.createdPhase===phase&&w.closedPhase===phase&&w.closed>=report.negativeStartup.started);report.negativeStartup.kernelRequestDenied=requestFailures.some(r=>r.phase===phase&&r.url===report.negativeStartup.observed.kernelURL&&r.failure?.errorText==='net::ERR_INTERNET_DISCONNECTED');

 await goOnline('verified-recovery');await diagnosticClick('retry');await ready();report.recoveredEngine=await installedEngine();await pause();check('retry downloads verified firmware and restores guest',await page.evaluate(()=>iphone.ready));check('verified recovery keeps the original saved phone',JSON.stringify(await saved())===JSON.stringify(original));check('recovered candidate executes its verified seeded engine',report.recoveredEngine.enabled&&report.recoveredEngine.retired>0);await screen(join(directory,'recovered.png'));
 // Every local response received in offline phases must be service-worker supplied.
 const offlineResponses=browserResponses.filter(r=>r.phase==='disconnected-B'||r.phase==='corrupt-seed-offline'||r.phase==='corrupt-kernel-offline');assert(offlineResponses.every(r=>r.url.startsWith('blob:')||r.fromServiceWorker),'Offline response escaped verified SW cache');report.offlineResponses=offlineResponses;
}

try {
  phase='online-A';await page.goto(url); await ready();
  check('new visitor is not automatically registered for offline use',await page.evaluate(async()=>!(await navigator.serviceWorker.getRegistration())));
  await diagnosticClick('offline');
  await page.waitForFunction(()=>document.getElementById('status').textContent.includes('available offline'),{}, {timeout:60000});
  await page.waitForFunction(()=>navigator.serviceWorker.controller);
  await page.reload(); await ready();
  check('version A controls a genuine running guest', (await bundle()).shell==='A');
  report.previousEngine=await installedEngine();check('actual old runtime executes the verified seed before saving',report.previousEngine.enabled&&report.previousEngine.retired>0,report.previousEngine);
  // Settings is a genuine distinct guest screen; save it while paused.
  const box=await page.locator('#screen').boundingBox();
  await page.mouse.click(box.x+275/320*box.width,box.y+250/480*box.height,{delay:30});
  await page.waitForTimeout(12000); await pause();
  await screen(join(directory,'settings-before-update.png'));
  const beforeSave=await page.evaluate(()=>window.iphone.savedAt);
  await diagnosticClick('save');
  await page.waitForFunction(old=>window.iphone.savedAt>old && !document.getElementById('save').disabled,beforeSave,{timeout:60000});
  const original=await saved(); report.savedBefore=original;
  const downloadPromise=page.waitForEvent('download',{timeout:30000});
  await page.evaluate(async()=>{const {readCheckpoint}=await import('./storage.js');const record=await readCheckpoint();const link=document.createElement('a');link.href=URL.createObjectURL(record.data);link.download='saved-before.checkpoint';document.body.append(link);link.click();link.remove();});
  const download=await downloadPromise;await download.saveAs(join(directory,'saved-before.checkpoint'));const savedBytes=await fs.readFile(join(directory,'saved-before.checkpoint'));assert.equal(savedBytes.length,original.storedBytes);assert.equal(sha(savedBytes),original.hash);report.savedBeforeArtifact={file:'saved-before.checkpoint',bytes:savedBytes.length,sha256:sha(savedBytes),scope:'Already encoded current IDB Blob; no extra guest capture'};

  check('real guest checkpoint is durably stored before upgrade',original.size>100*1048576);
  phase='waiting-B';await publish('B');
  // Reload remains controlled by A, whose startup check discovers B.
  await page.reload(); await ready(); await pause();
  await page.waitForFunction(()=>!document.querySelector('#update-notice').hidden,{}, {timeout:60000}); // Companion stays hidden; inspect intended notice state.
  check('startup detects waiting update without another Keep offline click',await page.evaluate(async()=>Boolean((await navigator.serviceWorker.getRegistration()).waiting)));
  const oldBundle=await bundle(); report.waitingBundle=oldBundle;
  check('waiting update preserves matching old shell, manifest and WASM',oldBundle.shell==='A' && oldBundle.manifest==='A' && oldBundle.wasm===variants.A.hashes['dist/emu.wasm']);
  check('waiting update leaves saved checkpoint unchanged',JSON.stringify(await saved())===JSON.stringify(original));
  await screen(join(directory,'update-waiting.png'));
  const notice=await page.locator('#update-notice').textContent();
  check('notice asks to Save and close every phone tab',notice.includes('Save') && notice.includes('close all phone tabs'));
  // A second controlled tab also holds A active; closing only one cannot update.
  const second=await context.newPage(); await second.goto(url);
  await second.waitForFunction(()=>window.iphone?.ready,{}, {timeout:60000});
  check('another phone tab cannot own the saved profile',!await second.evaluate(()=>window.iphone.profileOwned));
  await page.close(); page=second; await pause();
  check('remaining phone tab retains version A while B waits',(await bundle()).shell==='A' && await page.evaluate(async()=>Boolean((await navigator.serviceWorker.getRegistration()).waiting)));
  await page.close();
  // No in-scope clients now remain. Let the browser complete normal activation.
  await new Promise(resolve=>setTimeout(resolve,2000));
  phase='activate-B';page=await context.newPage(); observe(page); await page.goto(url); await ready(); await pause();
  const newBundle=await bundle(); report.activatedBundle=newBundle;
  check('closing all tabs and reopening activates matching version B',newBundle.shell==='B' && newBundle.manifest==='B' && newBundle.wasm===variants.B.hashes['dist/emu.wasm']);
  const after=await saved(); report.savedAfter=after;
  check('upgrade retains identical checkpoint bytes and metadata',JSON.stringify(after)===JSON.stringify(original));
  check('new bundle restores genuine saved guest and profile ownership',await page.evaluate(()=>window.iphone.ready && window.iphone.profileOwned && window.iphone.savedAt>0));
  report.activatedEngine=await installedEngine();
  check('version B installs the matching seeded engine',report.activatedEngine.enabled && report.activatedEngine.regions>0,report.activatedEngine);

  check('waiting notice clears after normal activation',await page.evaluate(()=>document.querySelector('#update-notice').hidden));
  await screen(join(directory,'settings-after-update.png'));
  await goOffline('disconnected-B'); await page.reload(); await ready(); await pause();
  report.disconnected = {...await bundle(), online:await page.evaluate(()=>navigator.onLine), networkAvailable:await page.evaluate(async()=>{try { await fetch('uncached-network-probe', {cache:'no-store'}); return true; } catch { return false; }})};
  check('new bundle and saved guest reopen while disconnected',report.disconnected.shell==='B' && !report.disconnected.networkAvailable,report.disconnected);
  check('background offline update failure does not replace save status',!(await page.locator('#status').textContent()).includes('failed'));
  report.disconnectedEngine=await installedEngine();
  check('version B seeded engine installs while disconnected',report.disconnectedEngine.enabled && report.disconnectedEngine.regions>0,report.disconnectedEngine);

  await extraGates(original);
  check('no uncaught page errors',report.errors.length===0,report.errors);
  await Promise.all(bodyTasks);report.assetValidation=validateAssets(servedAssets,variants,url,{negativeStartup:report.negativeStartup});
  report.result='pass';
} catch(error) { report.result='fail'; report.error=error.stack;report.failureState=await page.evaluate(()=>({status:document.querySelector('#status')?.textContent,ready:window.iphone?.ready,paused:window.iphone?.paused,savedAt:window.iphone?.savedAt})).catch(e=>({error:e.message})); await screen(join(directory,'failure.png')).catch(()=>{}); throw error; }
finally {
  await Promise.allSettled(bodyTasks);if(report.result==='pass'){try{report.assetValidation=validateAssets(servedAssets,variants,url,{negativeStartup:report.negativeStartup});}catch(e){report.result='fail';report.error=e.stack;}}await reportWrite.catch(e=>{report.persistenceError=e.message;report.result='fail';});
  await fs.writeFile(join(directory,'report.json'),json(report)+'\n');
  try{await processes('before-close');}catch(e){report.processReceiptError=e.message;report.result='fail';}
  clearTimeout(outer);const closure={};
  for(const [name,close]of [['context',()=>context.close()],['browser',()=>browser.close()],['origin',async()=>{if(server.listening)return stopServer(server,sockets,port);return {alreadyClosed:true};}]] ){
   try{closure[name]={closed:true,result:await close()};}catch(e){closure[name]={closed:false,error:e.message};report.result='fail';}
  }
  report.closure=closure;report.browserClosed=closure.context.closed&&closure.browser.closed;
  report.videoFiles=[];for(const name of await fs.readdir(directory))if(name.endsWith('.webm')){const bytes=await fs.readFile(join(directory,name));report.videoFiles.push({file:name,bytes:bytes.length,sha256:sha(bytes)});}
  report.closedAt=new Date().toISOString();await fs.writeFile(join(directory,'report.json'),json(report)+'\n');
  console.log(json({result:report.result,checks:report.checks.length,error:report.error,closure}));
}
if(report.result!=='pass')process.exitCode=1;
