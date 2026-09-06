from pathlib import Path
import shutil
b=Path('.context/mbx-stride-integration');d=b/'upgrade-v2';assert not d.exists();d.mkdir()
for n in ['offline-upgrade.mjs','validate-assets.mjs','run-upgrade.py']:shutil.copyfile(b/n,d/n)
p=d/'offline-upgrade.mjs';s=p.read_text().replace("resolve('.context/mbx-stride-integration/run-1')","resolve('.context/mbx-stride-integration/upgrade-v2/run-1')").replace("const port = 8235;","const port = 8236;").replace("from '../jit-sw-cache-v4/physical-server.mjs'","from '../../jit-sw-cache-v4/physical-server.mjs'").replace("from '../jit-fallback-census/frame-packet.mjs'","from '../../jit-fallback-census/frame-packet.mjs'")
s=s.replace("'Content-Type':type,'Content-Length'", "'Content-Type':(/^(text\\/|application\\/json|image\\/svg\\+xml)/.test(type)?type+'; charset=utf-8':type),'Content-Length'")
s=s.replace("const servedAssets=[],bodyTasks=[];", "const servedAssets=[],bodyTasks=[],workerEvents=[];let requestSequence=0,pageSequence=0;const requestIds=new WeakMap(),pageIds=new WeakMap();\nfunction requestMeta(request){if(!requestIds.has(request))requestIds.set(request,++requestSequence);let pageId=null;try{pageId=pageIds.get(request.frame().page())??null;}catch{}return {requestId:requestIds.get(request),pageId};}")
s=s.replace("context.on('request',r=>browserRequests.push({phase,url:r.url(),method:r.method(),at:Date.now()}));", "context.on('page',p=>{const pageId=++pageSequence;pageIds.set(p,pageId);p.on('worker',w=>{const row={pageId,url:w.url(),created:Date.now(),createdPhase:phase};workerEvents.push(row);w.on('close',()=>{row.closed=Date.now();row.closedPhase=phase;});});});\ncontext.on('request',r=>browserRequests.push({...requestMeta(r),phase,url:r.url(),method:r.method(),at:Date.now()}));")
s=s.replace("const row={phase,url:r.url(),status:r.status(),fromServiceWorker:r.fromServiceWorker(),at:Date.now()};", "const row={...requestMeta(r.request()),phase,url:r.url(),status:r.status(),fromServiceWorker:r.fromServiceWorker(),headers:r.headers(),at:Date.now()};")
s=s.replace("item.error=e.message;", "item.error=e.message;item.errorAt=Date.now();")
s=s.replace("requestFailures.push({phase,url:r.url()", "requestFailures.push({...requestMeta(r),phase,url:r.url()")
s=s.replace("errors:[],servedAssets,variants", "errors:[],servedAssets,workerEvents,variants")
s=s.replace("  await publish('B');phase='waiting-B';", "  phase='waiting-B';await publish('B');")
needle=" await page.reload();await page.waitForFunction(()=>!document.querySelector('#retry').hidden"
s=s.replace(needle, " report.negativeStartup={phase,pageId:pageIds.get(page),started:Date.now(),physicalBarrier:barriers.at(-1)};\n"+needle)
needle="await screen(join(directory,'corrupt-offline.png'));"
replacement="""await screen(join(directory,'corrupt-offline.png'));
 report.negativeStartup.observed=await page.evaluate(async()=>{const m=await(await fetch('manifest.json')).json();return {ready:iphone.ready,retryEnabled:!document.querySelector('#retry').hidden&&!document.querySelector('#retry').disabled,kernelURL:new URL(m.kernel,location.href).href,kernelCacheRemoved:!(await(await caches.open('iphone-1-assets-1a543a-v1')).match(new URL(m.kernel,location.href))),status:document.querySelector('#status').textContent};});
 await page.waitForTimeout(50);report.negativeStartup.ended=Date.now();report.negativeStartup.workerClosures=workerEvents.filter(w=>w.pageId===report.negativeStartup.pageId&&w.createdPhase===phase&&w.closedPhase===phase&&w.closed>=report.negativeStartup.started);report.negativeStartup.kernelRequestDenied=requestFailures.some(r=>r.phase===phase&&r.url===report.negativeStartup.observed.kernelURL&&r.failure?.errorText==='net::ERR_INTERNET_DISCONNECTED');
"""
assert needle in s;s=s.replace(needle,replacement)
s=s.replace("validateAssets(servedAssets,variants,url)", "validateAssets(servedAssets,variants,url,{negativeStartup:report.negativeStartup})")
s=s.replace("await Promise.allSettled(bodyTasks);await reportWrite", "await Promise.allSettled(bodyTasks);if(report.result==='pass'){try{report.assetValidation=validateAssets(servedAssets,variants,url,{negativeStartup:report.negativeStartup});}catch(e){report.result='fail';report.error=e.stack;}}await reportWrite")
p.write_text(s)
p=d/'run-upgrade.py';s=p.read_text().replace("b=Path('.context/mbx-stride-integration')", "b=Path('.context/mbx-stride-integration/upgrade-v2')");p.write_text(s)
