/** Actual guest Settings/Home timings. Run with the web server available.
 * Readiness is the Settings rows or a matching Home layout, observed stable
 * for 500 ms. Reported ready time is the start of that interval, not its end.
 * This is a bounded comparison, not the full 30-sample product acceptance gate.
 */
import {chromium} from 'playwright';
import {mkdir,writeFile,readFile} from 'node:fs/promises';
import {resolve} from 'node:path';
import {createHash} from 'node:crypto';
import {gzipSync} from 'node:zlib';
import {execFileSync} from 'node:child_process';
import {ensurePerformanceHome,classifyPerformanceScreen} from './helpers/performance-home.mjs';
import {finishPerformanceRun,loadPerformanceFixture} from './helpers/performance-lifecycle.mjs';
import {cpus,platform,release,totalmem} from 'node:os';
let macModel=null;if(platform()==='darwin'){try{macModel=execFileSync('/usr/sbin/sysctl',['-n','hw.model'],{encoding:'utf8'}).trim();}catch{macModel='unavailable';}}
const dir=resolve(process.env.ARTIFACT_DIR || '.context/performance-report');
let browser,context,page,baseline;
const assetTasks=[];
let collectAssets=true;
const report={host:{macModel,platform:platform(),release:release(),cpu:cpus()[0]?.model,logicalCpus:cpus().length,memoryBytes:totalmem()},date:new Date().toISOString(),browser:null,url:process.env.BASE_URL || 'http://localhost:8080',pulseMs:30,actions:[],errors:[],method:'50 ms canvas sampling; changed pixels exclude status bar; readiness must remain visually stable 500 ms. Ready time starts that stable interval. Legacy readyMs/firstVisibleMs start before mouse-down; release-relative fields use the actual browser pointerup event. First/warm are separate; at least30 warm samples are required for a per-action p95.'};
try {
 await mkdir(dir,{recursive:true});
 baseline=process.env.BASELINE_MANIFEST?JSON.parse(await readFile(resolve(process.env.BASELINE_MANIFEST))):null;
 const expected=new Map((baseline?.entries||[]).filter(r=>r.relative.startsWith('site/')).map(r=>[r.relative.slice(5),r.sha256]));
 browser=await chromium.launch({executablePath:process.env.CHROME_PATH || '/usr/bin/google-chrome',timeout:30000,args:['--no-sandbox','--disable-dev-shm-usage']});
 report.browser=browser.version();
 context=await browser.newContext({viewport:{width:1280,height:1000},recordVideo:{dir,size:{width:1280,height:1000}}});
 page=await context.newPage();
 page.on('pageerror',e=>report.errors.push(e.message));
 page.on('response',response=>{
  if(!collectAssets)return;
  try {
   const url=new URL(response.url()),base=new URL(report.url);
   if(url.origin!==base.origin)return;
   const name=decodeURIComponent(url.pathname).replace(/^\//,'')||'index.html';
   if(!expected.has(name)&&!['index.html','emu-worker.js','dist/emu.wasm','jit/seed.wasm'].includes(name))return;
   const promise=(async()=>{const bytes=await response.body(),sha256=createHash('sha256').update(bytes).digest('hex');return {name,url:response.url(),sha256,expected:expected.get(name),matches:!expected.has(name)||sha256===expected.get(name)};})().catch(error=>({name,error:error.message,matches:false}));
   assetTasks.push({name,promise});
  } catch(error) { report.errors.push('Response identity: '+(error.stack||error)); }
 });
 await page.goto(report.url);
 await page.waitForFunction(()=>window.iphone?.ready,null,{timeout:120000});
 if (process.env.FIXTURE_SNAPSHOT) {
  const fixture=await loadPerformanceFixture(process.env.FIXTURE_SNAPSHOT);
  report.fixture=fixture.metadata.path;
  report.fixtureIdentity=fixture.metadata;
  await page.locator('#import').setInputFiles(fixture.upload);
  await page.waitForFunction(() => /backup is restored|Import failed/.test(document.querySelector('#status').textContent), null, {timeout:90000});
  if (/Import failed/.test(await page.locator('#status').textContent())) throw new Error('Fixture import failed');
 }
 await page.waitForTimeout(1000);
 report.preparation=await ensurePerformanceHome(page,{screenshot:resolve(dir,'prepared-home.png')});
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
 for(let round=0;round<Number(process.env.ROUNDS || 4);round++){
  for(const kind of ['settings','home']){
   if(kind==='settings'&&(await page.evaluate(classifyPerformanceScreen)).kind!=='home')throw Error('Settings measurement requires verified Home');
   const p=kind==='settings'?{x:box.x+275/320*box.width,y:box.y+250/480*box.height}:await page.locator('[data-button="1"]').boundingBox().then(b=>({x:b.x+b.width/2,y:b.y+b.height/2}));
   await page.mouse.move(p.x,p.y);
   await page.evaluate(k=>window.phoneTiming.begin(k),kind);
   await page.mouse.down();await page.waitForTimeout(report.pulseMs);await page.mouse.up();
   await page.waitForFunction(()=>window.phoneTiming.get(),null,{timeout:95000});
   const result=await page.evaluate(()=>window.phoneTiming.get());
   report.actions.push({kind,round,...result});
   await page.locator('#screen').screenshot({path:resolve(dir,`${round}-${kind}.png`)});
   const raw=await page.evaluate(()=>{const a=document.querySelector('#screen').getContext('2d').getImageData(0,0,320,480).data;let s='';for(let i=0;i<a.length;i+=32768)s+=String.fromCharCode(...a.subarray(i,i+32768));return btoa(s);});
   await writeFile(resolve(dir,`${round}-${kind}.rgba.gz`),gzipSync(Buffer.from(raw,'base64')));
   console.log(JSON.stringify({kind,round,firstVisibleMs:result.firstVisibleMs,readyMs:result.readyMs,releaseToFirstVisibleMs:result.releaseToFirstVisibleMs,releaseToReadyMs:result.releaseToReadyMs,error:result.error}));
   if(result.error)throw new Error(`${kind}: ${result.error}`);
   if(!Number.isFinite(result.releaseOffsetMs))throw new Error(`${kind}: missing actual pointer release timestamp`);
   await page.waitForTimeout(1000);
  }
 }
 const percentile95=values=>[...values].sort((a,b)=>a-b)[Math.ceil(values.length*.95)-1];
 report.summary=Object.fromEntries(['settings','home'].map(kind=>{
  const actions=report.actions.filter(a=>a.kind===kind&&!a.error);
  const warm=actions.filter(a=>a.round>0&&Number.isFinite(a.releaseToReadyMs));
  return [kind,{first:actions[0]||null,warmSamples:warm.length,
   releaseToReadyP95Ms:warm.length>=30?percentile95(warm.map(a=>a.releaseToReadyMs)):null,
   navigationTargetMs:1000,meetsSampledNavigationTarget:warm.length>=30?percentile95(warm.map(a=>a.releaseToReadyMs))<=1000:null,
   note:warm.length>=30?'Sampled navigation gate on this machine; broader interactions and reference laptops remain separate.':'Insufficient warm samples for the30-run navigation gate.'}];
 }));
 report.passed=report.errors.length===0;
}catch(e){report.errors.push(e.stack || e.message);report.passed=false;}
finally {
 collectAssets=false;
 await finishPerformanceRun({report,assetTasks,context,browser,page,
  required:baseline?['index.html','emu-worker.js','dist/emu.wasm','jit/seed.wasm']:[],
  writeReport:async result=>{
   await mkdir(dir,{recursive:true});
   await writeFile(resolve(dir,'report.json'),JSON.stringify(result,(_,value)=>typeof value==='bigint'?value.toString():value,2)+'\n');
  }});
}
if(!report.passed)process.exitCode=1;
