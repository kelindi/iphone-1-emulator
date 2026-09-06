/** Diagnostic-only genuine framebuffer/guest-clock latency trace.
 * BASE_URL=http://localhost:8080 ARTIFACT_DIR=.context/transition-profile node web/tests/transition-profile.mjs
 * Serves an instrumented worker through Playwright routing; does not change production
 * source, clock, CPI, input timing, or guest state. Optional --record adds video overhead.
 */
import {chromium} from 'playwright';
import {mkdir,writeFile,readFile} from 'node:fs/promises';
import {resolve} from 'node:path';
const dir=resolve(process.env.ARTIFACT_DIR || '.context/transition-profile');
await mkdir(dir,{recursive:true});
const duration=Number(process.env.OBSERVE_MS || 25000);
const browser=await chromium.launch({executablePath:'/usr/bin/google-chrome',args:['--no-sandbox','--disable-dev-shm-usage']});
const context=await browser.newContext({viewport:{width:1280,height:1000},...(process.argv.includes('--record')?{recordVideo:{dir,size:{width:1280,height:1000}}}:{})});
let worker=await readFile('web/emu-worker.js','utf8');
worker=worker.replace("let capturing = false, resolveCaptureTurn;","let capturing = false, resolveCaptureTurn; let diagnosticRunMs=0, diagnosticPresentMs=0, diagnosticRuns=0;");
worker=worker.replace('const ran = api.run(50000);','const diagnosticBegan=performance.now(); const ran = api.run(50000); diagnosticRunMs+=performance.now()-diagnosticBegan; diagnosticRuns++;');
worker=worker.replace('present(); lastFrame = now;','const diagnosticBegan=performance.now(); present(); diagnosticPresentMs+=performance.now()-diagnosticBegan; lastFrame = now;');
worker=worker.replace("{buffer, sequence:++frameSequence}","{buffer, sequence:++frameSequence, diagnosticTicks:api.ticks(), diagnosticCycles:Number(api.cycles())}");
worker=worker.replace('memory:module.HEAPU8.byteLength,','memory:module.HEAPU8.byteLength, diagnosticTicks:ticks, diagnosticRunMs, diagnosticPresentMs, diagnosticRuns,');
worker=worker.replace("if (!api) throw new Error('iPhone is still starting');","if (!api) throw new Error('iPhone is still starting'); if(message.type==='contacts'||message.type==='button')send('diagnosticInput',{inputType:message.type,pressed:message.pressed,count:message.contacts?.length,ticks:api.ticks(),cycles:Number(api.cycles())});");
await context.route('**/emu-worker.js',route=>route.fulfill({status:200,contentType:'application/javascript',body:worker}));
await context.addInitScript(()=>{
 window.trace={frames:[],metrics:[],inputs:[]};
 const BaseWorker=window.Worker;
 window.Worker=class extends BaseWorker{constructor(...args){super(...args);this.addEventListener('message',({data})=>{
  if(data.type==='diagnosticInput')window.trace.inputs.push({now:performance.now(),...data});
  if(data.type==='metrics')window.trace.metrics.push({now:performance.now(),...data,cycles:Number(data.cycles)});
  if(data.type==='frame'){
   const c=document.querySelector('#screen');if(!c)return;
   const d=new Uint8Array(data.buffer), rgb=[];let white=0,lit=0;
   for(let y=30;y<480;y+=4)for(let x=2;x<320;x+=4){let i=(y*320+x)*4;rgb.push(d[i],d[i+1],d[i+2]);if(Math.min(d[i],d[i+1],d[i+2])>235)white++;if(Math.max(d[i],d[i+1],d[i+2])>25)lit++;}
   window.trace.frames.push({now:performance.now(),sequence:data.sequence,ticks:data.diagnosticTicks,cycles:data.diagnosticCycles,white:white/(rgb.length/3),lit:lit/(rgb.length/3),rgb});
  }
 });}};
});
const page=await context.newPage();const errors=[];page.on('pageerror',e=>errors.push(e.message));
const wait=ms=>new Promise(r=>setTimeout(r,ms));
function difference(a,b){let n=0;for(let i=0;i<a.length;i+=3)if(Math.max(Math.abs(a[i]-b[i]),Math.abs(a[i+1]-b[i+1]),Math.abs(a[i+2]-b[i+2]))>25)n++;return n/(a.length/3);}
const report={date:new Date().toISOString(),method:'CPI1 unchanged. Actual changed framebuffer frames sampled below status bar; action begins on pointerdown. Final reference matched retrospectively, not a fixed settling wait. 0.5% sample tolerance. Instruction-clock equivalent is reported at 412 MHz alongside actual 6 MHz device ticks. Their difference is not attributed to WFI when foreground host-clock pacing is active. Instrumentation cost reported separately.',actions:[],errors};
try{
 await page.goto(process.env.BASE_URL || 'http://localhost:8080');await page.waitForFunction(()=>window.iphone?.ready,null,{timeout:120000});if(process.env.SNAPSHOT){await page.locator('#import').setInputFiles(resolve(process.env.SNAPSHOT));await page.waitForFunction(()=>/backup is restored|Import failed/.test(document.querySelector('#status').textContent),null,{timeout:60000});if(/Import failed/.test(await page.locator('#status').textContent()))throw Error(await page.locator('#status').textContent());}await wait(4000);
 const home=await page.evaluate(()=>window.trace.frames.at(-1));
 await page.screenshot({path:resolve(dir,'00-home.png')});
 const names=process.env.ACTIONS?.split(',') || ['settings-cold','home-first','settings-warm','home-second'];
 for(const name of names){
  const before=await page.evaluate(()=>({frame:window.trace.frames.at(-1),now:performance.now(),metrics:window.trace.metrics.at(-1)}));
  const box=await page.locator(name.startsWith('home')?'[data-button="1"]':'#screen').boundingBox();
  const x=name.startsWith('home')?box.x+box.width/2:box.x+275/320*box.width,y=name.startsWith('home')?box.y+box.height/2:box.y+250/480*box.height;
  await page.mouse.move(x,y);const began=await page.evaluate(()=>performance.now());await page.mouse.down();await wait(180);await page.mouse.up();await wait(duration-180);
  const after=await page.evaluate(()=>({frames:window.trace.frames,metrics:window.trace.metrics,inputs:window.trace.inputs,end:performance.now()}));
  const input=after.inputs.find(i=>i.now>=began);
  const frames=after.frames.filter(f=>f.now>=began),target=name.startsWith('home')?home:frames.at(-1);
  const values=frames.map(f=>({...f,rgb:undefined,ms:f.now-began,fromBefore:difference(f.rgb,before.frame.rgb),fromTarget:difference(f.rgb,target.rgb)}));
  const feedback=values.find(f=>f.fromBefore>.001);
  const black=values.find(f=>f.lit<.001),content=values.find(f=>f.white>.25);
  let ready=null;for(let i=0;i<values.length;i++)if(values[i].fromTarget<.005&&values.slice(i).every(f=>f.fromTarget<.005)){ready=values[i];break;}
  const metrics=after.metrics.filter(m=>m.now>=began);
  const a=metrics[0],b=metrics.at(-1);let execution=null;
  if(a&&b&&b.now>a.now){const wall=(b.now-a.now)/1000,guest=(b.diagnosticTicks-a.diagnosticTicks)/6e6,instructions=b.cycles-a.cycles;execution={wallSeconds:wall,guestSeconds:guest,instructions,mips:instructions/wall/1e6,instructionClockSeconds:instructions/412e6,guestMinusInstructionClockSeconds:guest-instructions/412e6,runFraction:(b.diagnosticRunMs-a.diagnosticRunMs)/1000/wall,presentationFraction:(b.diagnosticPresentMs-a.diagnosticPresentMs)/1000/wall};}
  report.actions.push({name,began,inputs:after.inputs.filter(i=>i.now>=began),firstFeedbackMs:feedback?.ms,firstBlackMs:black?.ms,firstContentMs:content?.ms,blackToReadyGuestSeconds:ready&&black?(ready.ticks-black.ticks)/6e6:null,readyMs:ready?.ms,readyCycles:ready?ready.cycles-(input?.cycles ?? before.frame.cycles):null,readyGuestSeconds:ready?(ready.ticks-(input?.ticks ?? before.frame.ticks))/6e6:null,targetLit:target.lit,targetWhite:target.white,execution,frames:values,metrics});
  console.log(JSON.stringify({name,firstFeedbackMs:feedback?.ms,readyMs:ready?.ms,execution}));
  await page.screenshot({path:resolve(dir,`${name}.png`)});await page.locator('#screen').screenshot({path:resolve(dir,`${name}-guest.png`)});
  await writeFile(resolve(dir,'report.json'),JSON.stringify(report,null,2)+'\n');
  if(!ready||(!name.startsWith('home')&&target.white<.25))throw Error(`${name}: intended endpoint not reached; inspect screenshot`);
 }
}catch(error){report.errors.push(error.message);process.exitCode=1;}finally{await context.close();await browser.close();await writeFile(resolve(dir,'report.json'),JSON.stringify(report,null,2)+'\n');}
