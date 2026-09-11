/** Actual guest Settings/Home timings. Run with the web server available.
 * Readiness is the Settings rows or a matching Home layout, observed stable
 * for 500 ms. Reported ready time is the start of that interval, not its end.
 * This is a bounded comparison, not the full 30-sample product acceptance gate.
 */
import {chromium} from 'playwright';
import {Attribution} from './cdp.mjs';
let attribution;
import {mkdir,writeFile,readFile} from 'node:fs/promises';
import {resolve} from 'node:path';
import {createHash} from 'node:crypto';
import {gzipSync} from 'node:zlib';
import {cacheServer} from '../jit-fallback-census/cache-server.mjs';
import {cpus,platform,release,totalmem} from 'node:os';
const dir=resolve('.context/trace-optimizer-profile/run-1');
await mkdir(dir,{recursive:true});
const assets=[],tasks=[];const sha=b=>createHash('sha256').update(b).digest('hex');const {server}=await cacheServer(resolve('web'),{cache:'no-cache'});await new Promise((resolve,reject)=>{server.once('error',reject);server.listen(8242,'127.0.0.1',resolve)});
const browser=await chromium.launch({executablePath:process.env.CHROME_PATH || '/usr/bin/google-chrome',args:['--no-sandbox','--disable-dev-shm-usage','--remote-debugging-port=9269']});
const context=await browser.newContext({viewport:{width:1280,height:1000},recordVideo:{dir,size:{width:1280,height:1000}}});
context.on('response',r=>{if(/\/(emu|seed)\.wasm(?:\?|$)/.test(r.url()))tasks.push((async()=>{try{const bytes=await r.body();assets.push({url:r.url(),bytes:bytes.length,sha256:sha(bytes)});}catch(e){assets.push({url:r.url(),error:e.message});}})());});
const page=await context.newPage();
const report={host:{platform:platform(),release:release(),cpu:cpus()[0]?.model,logicalCpus:cpus().length,memoryBytes:totalmem()},date:new Date().toISOString(),browser:browser.version(),url:'http://127.0.0.1:8242',pulseMs:30,actions:[],errors:[],method:'CPU attribution diagnostic with sampling overhead; never a speed acceptance result.50 ms canvas sampling; changed pixels exclude status bar; readiness must remain visually stable 500 ms. Ready time starts that stable interval. Legacy readyMs/firstVisibleMs start before mouse-down; release-relative fields use the actual browser pointerup event. First/warm are separate; at least20 warm samples are required for a per-action p95.'};
page.on('pageerror',e=>report.errors.push(e.message));
try {
 await page.goto(report.url);
 await page.waitForFunction(()=>window.iphone?.ready,null,{timeout:120000});
 await page.waitForTimeout(1000);

 report.client=await page.evaluate(()=>({userAgent:navigator.userAgent,hardwareConcurrency:navigator.hardwareConcurrency,deviceMemory:navigator.deviceMemory}));
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
 for(let round=0;round<2;round++){
  for(const kind of (round?['settings']:['settings','home'])){
   const p=kind==='settings'?{x:box.x+275/320*box.width,y:box.y+250/480*box.height}:await page.locator('[data-button="1"]').boundingBox().then(b=>({x:b.x+b.width/2,y:b.y+b.height/2}));
   await page.mouse.move(p.x,p.y);
   if(round){attribution=await Attribution.connect(9269,resolve(dir,'profiles'));await attribution.begin(`${round}-${kind}`,await page.evaluate(()=>({metrics:window.iphone.metrics,savedAt:window.iphone.savedAt})));}
   await page.evaluate(k=>window.phoneTiming.begin(k),kind);
   await page.mouse.down();await page.waitForTimeout(report.pulseMs);await page.mouse.up();
   await page.waitForFunction(()=>window.phoneTiming.get(),null,{timeout:95000});
   const result=await page.evaluate(()=>window.phoneTiming.get());
   if(round)await attribution.end(await page.evaluate(()=>({metrics:window.iphone.metrics,savedAt:window.iphone.savedAt})));
   report.actions.push({kind,round,...result});
   await page.waitForTimeout(1000);await page.locator('#screen').screenshot({path:resolve(dir,`${round}-${kind}.png`)});
   const raw=await page.evaluate(()=>{const a=document.querySelector('#screen').getContext('2d').getImageData(0,0,320,480).data;let s='';for(let i=0;i<a.length;i+=32768)s+=String.fromCharCode(...a.subarray(i,i+32768));return btoa(s);});await writeFile(resolve(dir,`${round}-${kind}.rgba.gz`),gzipSync(Buffer.from(raw,'base64')));
   console.log(JSON.stringify({kind,round,firstVisibleMs:result.firstVisibleMs,readyMs:result.readyMs,releaseToFirstVisibleMs:result.releaseToFirstVisibleMs,releaseToReadyMs:result.releaseToReadyMs,error:result.error}));
   if(result.error)throw new Error(`${kind}: ${result.error}`);
   if(!Number.isFinite(result.releaseOffsetMs))throw new Error(`${kind}: missing actual pointer release timestamp`);
   await page.waitForTimeout(1000);
  }
 }
 report.passed=report.errors.length===0;
}catch(e){report.errors.push(e.stack || e.message);report.passed=false;}
finally{
 if(attribution){try{if(attribution.active)await attribution.end({interrupted:true});}catch(e){report.errors.push(e.message);report.passed=false;}await attribution.close();}
 await Promise.allSettled(tasks);report.assets=assets;for(const [suffix,file]of [['/dist/emu.wasm','web/dist/emu.wasm'],['/jit/seed.wasm','web/jit/seed.wasm']]){const expected=sha(await readFile(file));const received=assets.filter(r=>r.url.endsWith(suffix));if(!received.length||received.some(r=>r.sha256!==expected)){report.passed=false;report.errors.push('Actual module identity '+suffix);}}
 const video=page.video();await context.close();report.video=await video.path();await browser.close();server.closeAllConnections();await new Promise(r=>server.close(r));report.closed=true;
 await writeFile(resolve(dir,'report.json'),JSON.stringify(report,(_,v)=>typeof v==='bigint'?String(v):v,2)+'\n');
}
if(!report.passed)process.exitCode=1;
