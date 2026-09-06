/** Capture the genuine foreground Stopwatch for visual drift review.
 * BASE_URL=http://localhost:8080 ARTIFACT_DIR=.context/stopwatch-production node web/tests/stopwatch.mjs
 * Requires the default USB-matched phone, JIT, Chrome, and Playwright video.
 * Reads the real canvas at a scheduled 60-second interval; does not replace the
 * displayed digits with host time. captureComplete is NOT an acceptance result:
 * review the baseline/end digits and fill the report's separate visualReview.
 * --save-load clicks ordinary Save during the measured minute and requires
 * savedAt to advance. --controls also records Lap and Reset after timing.
 */
import {chromium} from 'playwright';
import {mkdir,writeFile} from 'node:fs/promises';
import {resolve} from 'node:path';
const dir=resolve(process.env.ARTIFACT_DIR || '.context/stopwatch-production');
const saveLoad=process.argv.includes('--save-load'), controls=process.argv.includes('--controls');
await mkdir(dir,{recursive:true});
const report={date:new Date().toISOString(),url:process.env.BASE_URL || 'http://localhost:8080',mode:'default JIT, software graphics, fresh disposable profile',saveLoad,controls,samples:[],actions:[],errors:[],reviewRequired:true};
const browser=await chromium.launch({executablePath:'/usr/bin/google-chrome',args:['--no-sandbox','--disable-dev-shm-usage']});
const context=await browser.newContext({viewport:{width:1280,height:1050},recordVideo:{dir,size:{width:1280,height:1050}}});
await context.route('**/emu-worker.js',async route=>{
 const response=await route.fetch();const source=await response.text();const seam='onmessage = async ({data:message}) => {';
 if(!source.includes(seam))throw Error('Missing diagnostic seam');
 await route.fulfill({response,body:source.replace(seam,seam+`
 if(message.type==='stopwatchRead'){send('stopwatchRead',{id:message.id,ticks:api?.ticks(),cycles:Number(api?.cycles()),paused,now:performance.now()});return;}
 `)});
});
await context.addInitScript(()=>{
 const Base=window.Worker;window.Worker=class extends Base {constructor(...args){super(...args);if (String(args[0]).includes('emu-worker.js')) window.stopwatchWorker=this;}};
 let id=0;window.stopwatchRead=()=>new Promise(resolve=>{const seq=++id,w=window.stopwatchWorker;const f=({data})=>{if(data.type==='stopwatchRead'&&data.id===seq){w.removeEventListener('message',f);resolve(data);}};w.addEventListener('message',f);w.postMessage({type:'stopwatchRead',id:seq});});
});
const page=await context.newPage();const video=page.video();
page.on('pageerror',error=>report.errors.push(error.message));
page.on('console',message=>{if(message.type()==='error')report.errors.push(message.text());});
const save=()=>writeFile(resolve(dir,'report.json'),JSON.stringify(report,null,2)+'\n');
const wall=()=>page.evaluate(()=>performance.now());
const wait=ms=>new Promise(resolve=>setTimeout(resolve,ms));
async function tap(name,x,y){
 const box=await page.locator('#screen').boundingBox();const before=await wall();
 await page.mouse.move(box.x+x*box.width/320,box.y+y*box.height/480);
 await page.mouse.down();await wait(50);await page.mouse.up();
 const after=await wall();const action={name,before,after};report.actions.push(action);await save();return action;
}
async function sample(name){
 const sample=await page.evaluate(()=>{const canvas=document.querySelector('#screen');return{wall:performance.now(),png:canvas.toDataURL('image/png'),jit:window.iphone.metrics.jit,visible:!document.hidden,paused:window.iphone.paused,savedAt:window.iphone.savedAt,status:document.querySelector('#status').textContent};});
 const {png,...entry}=sample;entry.name=name;entry.tickRead=await page.evaluate(()=>window.stopwatchRead());entry.afterTickRead=await wall();
 await writeFile(resolve(dir,name+'-guest.png'),Buffer.from(png.split(',')[1],'base64'));
 report.samples.push(entry);await save();return entry;
}
async function buttonColor(red){
 await page.waitForFunction(red=>{const p=document.querySelector('#screen').getContext('2d').getImageData(30,170,30,25).data;let count=0;for(let i=0;i<p.length;i+=4){if(red?p[i]>p[i+1]+30:p[i+1]>p[i]+30)count++;}return count>200;},red,{polling:20,timeout:20000});
 return wall();
}
try{
 await page.goto(report.url);await page.waitForFunction(()=>window.iphone?.ready,null,{timeout:120000});
 await page.waitForFunction(()=>{const j=window.iphone?.metrics?.jit;return j?.enabled&&j.calls>0&&j.retired>0;},null,{timeout:15000});
 await tap('Open Clock',45,250);await wait(8000);
 await tap('Select Stopwatch',200,454);await wait(3000);
 await buttonColor(false);await sample('00-zero');await page.screenshot({path:resolve(dir,'00-zero.png')});
 const start=await tap('Start',85,185);start.ack=await buttonColor(true);start.ackAfterReleaseMs=start.ack-start.after;
 await wait(300);const first=await sample('01-running-baseline');
 for(let i=1;i<=6;i++){
  const remaining=first.wall+i*10000-await wall();if(remaining>0)await wait(remaining);
  await sample(`0${i+1}-running-${i*10}s`);
  if(saveLoad&&i===2){
   await page.locator('#save').waitFor({state:'visible'});
   await page.waitForFunction(()=>!document.querySelector('#save').disabled,null,{timeout:30000});
   report.saveWorkload={before:await page.evaluate(()=>window.iphone.savedAt),requestedAt:await wall()};
   await page.locator('#save').click();report.saveWorkload.clickCompletedAt=await wall();
   await page.screenshot({path:resolve(dir,'save-in-progress.png')});await save();
  }
 }
 if(saveLoad){
  report.saveWorkload.after=await page.evaluate(()=>window.iphone.savedAt);
  report.saveWorkload.completedByEnd=report.saveWorkload.after>report.saveWorkload.before;
  if(!report.saveWorkload.completedByEnd)throw Error('Explicit Save did not advance savedAt during the measured interval');
 }
 if(controls){await tap('Lap',235,185);await wait(2000);await sample('lap');await page.screenshot({path:resolve(dir,'lap.png')});}
 const stop=await tap('Stop',85,185);stop.ack=await buttonColor(false);stop.ackAfterReleaseMs=stop.ack-stop.after;
 await wait(700);await sample('08-stopped');await page.screenshot({path:resolve(dir,'08-stopped.png')});
 await wait(2000);await sample('09-stopped-still');
 if(controls){await tap('Reset',235,185);await wait(2000);await sample('10-reset');await page.screenshot({path:resolve(dir,'10-reset.png')});await wait(1000);await sample('11-reset-still');}
 report.captureComplete=true;
}catch(error){report.errors.push(error.stack);await page.screenshot({path:resolve(dir,'failure.png')}).catch(()=>{});process.exitCode=1;}
finally{await save();await context.close();report.video=await video.path();await browser.close();await save();console.log(JSON.stringify(report,null,2));}
