import {chromium} from 'playwright';
import {createServer} from 'node:http';
import {readFile,mkdir,writeFile} from 'node:fs/promises';
import {resolve,extname} from 'node:path';
import {createHash} from 'node:crypto';
const dir=resolve('.context/jit-sw-cache/install-diagnostic'),root=resolve('.context/jit-sw-cache/candidate/web');
await mkdir(dir,{recursive:true});
const original=await readFile(root+'/service-worker.js','utf8');
const needle='catch (error) { await caches.delete(CACHE); throw error; }';
if(original.split(needle).length!==2)throw Error('Catch seam mismatch');
const sw=original.replace(needle,`catch (error) { for (const client of await self.clients.matchAll({includeUncontrolled:true})) client.postMessage({installError:{name:error.name,message:error.message,stack:error.stack}}); await caches.delete(CACHE); throw error; }`);
await writeFile(dir+'/service-worker.js',sw);
const report={scope:'No guest/compile. Post-cleanup isolated real installer with catch-only error observation; fresh profile/origin. Cannot retroactively recover initial swallowed error.',originalSha:createHash('sha256').update(original).digest('hex'),diagnosticSha:createHash('sha256').update(sw).digest('hex'),requests:[],console:[]};
const server=createServer(async(req,res)=>{try{const p=new URL(req.url,'http://local').pathname;const body=p==='/observe.html'?Buffer.from('<!doctype html><title>Install storage diagnostic</title>'):p==='/service-worker.js'?Buffer.from(sw):await readFile(resolve(root,'.'+p));res.writeHead(200,{'Content-Type':p.endsWith('.js')?'text/javascript':p.endsWith('.wasm')?'application/wasm':p.endsWith('.html')?'text/html':'application/octet-stream','Cache-Control':'no-cache'});res.end(body);report.requests.push({at:Date.now(),path:p,bytes:body.length});}catch(e){res.writeHead(404);res.end();}});
await new Promise(r=>server.listen(8195,'127.0.0.1',r));let browser;
try{
 browser=await chromium.launch({executablePath:'/usr/bin/google-chrome',args:['--no-sandbox','--disable-dev-shm-usage']});const context=await browser.newContext();context.on('console',m=>report.console.push({type:m.type(),text:m.text()}));const page=await context.newPage();await page.goto('http://127.0.0.1:8195/observe.html');
 report.result=await page.evaluate(async()=>{
  const result={before:await navigator.storage.estimate(),errors:[],states:[]};navigator.serviceWorker.addEventListener('message',e=>result.errors.push(e.data));
  try{const reg=await navigator.serviceWorker.register('/service-worker.js');await new Promise((resolve,reject)=>{const timer=setTimeout(()=>reject(Error('Bounded install timeout 30000ms')),30000);const worker=reg.installing||reg.waiting||reg.active;function changed(){result.states.push({at:Date.now(),state:worker.state});if(worker.state==='activated'){clearTimeout(timer);resolve();}if(worker.state==='redundant'){clearTimeout(timer);reject(Error('Install became redundant'));}}worker.addEventListener('statechange',changed);changed();});result.installed=true;}catch(e){result.error={name:e.name,message:e.message};}
  result.after=await navigator.storage.estimate();result.caches=await caches.keys();result.entries=[];for(const name of result.caches){const cache=await caches.open(name);for(const request of await cache.keys()){const response=await cache.match(request);const body=await response.arrayBuffer();result.entries.push({url:request.url,bytes:body.byteLength,sha256:[...new Uint8Array(await crypto.subtle.digest('SHA-256',body))].map(v=>v.toString(16).padStart(2,'0')).join('')});}}return result;
 });
 const defs=JSON.parse(original.match(/const FILES = (.*);/)[1]);report.passed=report.result.installed&&Object.entries(defs).every(([p,sha])=>report.result.entries.some(e=>e.url.endsWith('/'+p)&&e.sha256===sha));
}catch(e){report.error=e.stack;report.passed=false;}finally{await browser?.close();await new Promise(r=>server.close(r));report.browserClosed=true;await writeFile(dir+'/report.json',JSON.stringify(report,null,2)+'\n');console.log(JSON.stringify({passed:report.passed,result:report.result,browserClosed:true}));if(!report.passed)process.exitCode=1;}
