/** Bounded, real-guest app inventory. One fresh browser context per app.
 * BASE_URL=http://localhost:8080 ARTIFACT_DIR=.context/app-inventory node web/tests/inventory.mjs
 * APP_LIST=Calculator,Notes filters apps. OBSERVE_MS=30000 controls the launch
 * observation window; ACTION_MS=15000 controls the optional action window.
 * RECORD_APPS=Calculator records selected sessions (requires Playwright ffmpeg).
 * FIXTURE_SNAPSHOT imports an optional phone through the normal UI; CHECK_HOME=1
 * captures a final Home return. graphicsPass is only a coarse discovery gate.
 * Screenshots and guest logs require human review: a changed screen is NOT an
 * app functionality pass. No performance claims: this is a discovery harness.
 */
import {chromium} from 'playwright';
import {mkdir, writeFile} from 'node:fs/promises';
import {resolve} from 'node:path';
const baseURL=process.env.BASE_URL || 'http://localhost:8080';
const artifactDir=resolve(process.env.ARTIFACT_DIR || '.context/app-inventory');
const observeMs=Number(process.env.OBSERVE_MS || 30000);
const actionMs=Number(process.env.ACTION_MS || 15000);
const selected=process.env.APP_LIST?.split(',');
const recordings=new Set((process.env.RECORD_APPS || '').split(','));
const apps=[
  {name:'Text', icon:[45,65], action:{name:'Use the top-right compose/cancel control', point:[300,42]}},
  {name:'Calendar', icon:[120,65], action:{name:'Open new event editor', point:[300,42]}},
  {name:'Photos', icon:[200,65]},
  {name:'Camera', icon:[275,65], action:{name:'Press the shutter', point:[160,450]}},
  {name:'YouTube', icon:[45,155]},
  {name:'Stocks', icon:[120,155]},
  {name:'Maps', icon:[200,155], action:{name:'Focus map search', point:[160,42]}},
  {name:'Weather', icon:[275,155], action:{name:'Open city management', point:[287,424]}},
  {name:'Clock', icon:[45,250], action:{name:'Open Stopwatch', point:[200,454]}},
  {name:'Calculator', icon:[120,250], action:{name:'Evaluate 2 + 3', points:[[120,367],[275,295],[200,367],[275,437]]}},
  {name:'Notes', icon:[200,250], action:{name:'Finish the initial empty note', point:[300,42]}},
  {name:'Settings', icon:[275,250], action:{name:'Scroll the Settings list', drag:[[160,395],[160,195]], durationMs:2200}},
  {name:'Phone', icon:[45,430], action:{name:'Open Contacts', point:[160,454]}},
  {name:'Mail', icon:[120,430], action:{name:'Open manual account setup', point:[160,410]}},
  {name:'Safari', icon:[200,430], action:{name:'Close the initial bookmarks list', point:[300,42]}},
  {name:'iPod', icon:[275,430], action:{name:'Open More', point:[288,454]}},
].filter(app=>!selected || selected.includes(app.name));
// LEGACY_TOUCH=1 reproduces the pre-calibration discovery sweep, which used
// first-row lower hit regions y95. Corrected builds use actual centers y65.
if(process.env.LEGACY_TOUCH==='1') for(const app of apps) if(app.icon[1]===65){app.visualIconCenter=[...app.icon];app.icon[1]=95;}
if(process.env.ICON_Y) for(const app of apps) app.icon[1]=Number(process.env.ICON_Y);
await mkdir(artifactDir,{recursive:true});
const browser=await chromium.launch({executablePath:process.env.CHROME_PATH || '/usr/bin/google-chrome',args:['--no-sandbox','--disable-dev-shm-usage']});
const inventory={date:new Date().toISOString(),baseURL,browser:browser.version(),observeMs,actionMs,
  method:'One fresh context and supplied home snapshot per app. Fixed bounded observation, screenshots, optional UI probe, and guest diagnostics. Human review required. Not an app-acceptance or performance report.',apps:[]};
const wait=ms=>new Promise(resolve=>setTimeout(resolve,ms));
async function saveReport() {await writeFile(resolve(artifactDir,'inventory.json'),JSON.stringify(inventory,(_,v)=>typeof v==='bigint'?v.toString():v,2)+'\n');}
try {
 for(const app of apps){
  const dir=resolve(artifactDir,app.name.toLowerCase()); await mkdir(dir,{recursive:true});
  const context=await browser.newContext({viewport:{width:1280,height:1000},...(recordings.has(app.name)?{recordVideo:{dir,size:{width:1280,height:1000}}}:{})});
  const page=await context.newPage(); page.setDefaultTimeout(10000);
  const entry={name:app.name,icon:app.icon,visualIconCenter:app.visualIconCenter || app.icon,reviewRequired:true,console:[],pointerEvents:[],errors:[],stages:[],firstBlockedAction:'Pending screenshot and guest-log review'};
  inventory.apps.push(entry); await saveReport();
  page.on('console',msg=>entry.console.push({type:msg.type(),text:msg.text()}));
  page.on('pageerror',error=>{entry.errors.push(error.message); console.error(`${app.name}: ${error.message}`);});
  async function tap(point){
   const b=await page.locator('#screen').boundingBox();
   await page.mouse.move(b.x+point[0]*b.width/320,b.y+point[1]*b.height/480);
   await page.mouse.down(); await wait(180); await page.mouse.up();
  }
  async function capture(name){
   await page.screenshot({path:resolve(dir,`${name}.png`),fullPage:true});
   await page.locator('#screen').screenshot({path:resolve(dir,`${name}-guest.png`)});
   const snapshot=await page.evaluate(()=>({status:document.querySelector('#status').textContent,diagnostics:document.querySelector('#diagnostics').textContent,
    metrics:window.iphone.metrics,ready:window.iphone.ready,
    pixels:Array.from(document.querySelector('#screen').getContext('2d').getImageData(0,0,320,480).data).filter((_,i)=>i%64===0)}));
   const {pixels,...stage}=snapshot;
   stage.name=name;stage.nonBlackSampleFraction=pixels.filter(v=>v>25).length/pixels.length;entry.stages.push(stage);await saveReport();return pixels;
  }
  async function observe(duration){
   const end=Date.now()+duration;
   while(Date.now()<end){await wait(Math.min(1000,end-Date.now()));
    const state=await page.evaluate(()=>({ready:window.iphone?.ready,status:document.querySelector('#status').textContent}));
    if(!state.ready)throw new Error(`Runtime stopped: ${state.status}`);
   }
  }
  try{
   console.log(`${app.name}: restoring fresh phone`);
   await page.goto(baseURL,{waitUntil:'domcontentloaded'});
   await page.waitForFunction(()=>window.iphone?.ready,null,{timeout:120000});
   if(process.env.FIXTURE_SNAPSHOT){
    entry.fixture=resolve(process.env.FIXTURE_SNAPSHOT);
    await page.locator('#import').setInputFiles(entry.fixture);
    await page.waitForFunction(()=>/backup is restored|Import failed/.test(document.querySelector('#status').textContent),null,{timeout:120000});
    if(/Import failed/.test(await page.locator('#status').textContent()))throw Error('Fixture import failed');
   }
   await page.waitForFunction(()=>{const d=document.querySelector('#screen').getContext('2d').getImageData(40,60,1,1).data;return d[0]+d[1]+d[2]>25;},null,{timeout:10000});
   await page.evaluate(()=>{window.inventoryPointers=[];for(const type of ['pointerdown','pointerup'])document.querySelector('#screen').addEventListener(type,event=>{const b=event.currentTarget.getBoundingClientRect();window.inventoryPointers.push({type,id:event.pointerId,x:(event.clientX-b.x)*320/b.width,y:(event.clientY-b.y)*480/b.height,time:performance.now(),ready:window.iphone.ready,paused:window.iphone.paused});});});
   await observe(2000);
   const home=await capture('00-home');
   await tap(app.icon); console.log(`${app.name}: observing launch for ${observeMs/1000}s`);
   await observe(observeMs);
   const before=await capture('01-launch');
   entry.launchChangedSampleFraction=home.reduce((n,v,i)=>n+(Math.abs(v-before[i])>25?1:0),0)/home.length;
   if(app.action && process.env.SKIP_ACTIONS!=='1'){
    console.log(`${app.name}: ${app.action.name}`);
    entry.action=app.action;
    if(app.action.points){for(const point of app.action.points){await tap(point);await observe(2000);}}
    else if(app.action.point) await tap(app.action.point);
    else{
     const b=await page.locator('#screen').boundingBox();
     const [[x0,y0],[x1,y1]]=app.action.drag;
     await page.mouse.move(b.x+x0*b.width/320,b.y+y0*b.height/480);await page.mouse.down();
     for(let i=1;i<=22;i++){await page.mouse.move(b.x+(x0+(x1-x0)*i/22)*b.width/320,b.y+(y0+(y1-y0)*i/22)*b.height/480);await wait((app.action.durationMs || 2200)/22);}
     await page.mouse.up();
    }
    await observe(actionMs); const after=await capture('02-action');
    entry.action.changedSampleFraction=before.reduce((n,v,i)=>n+(Math.abs(v-after[i])>25?1:0),0)/before.length;
   }
   if(process.env.CHECK_HOME==='1'){
    await page.locator('[data-button="1"]').click();
    await observe(7000);const after=await capture('03-home');
    entry.homeDifference=home.reduce((n,v,i)=>n+(Math.abs(v-after[i])>25?1:0),0)/home.length;
   }
   entry.graphicsPass=entry.errors.length===0 && entry.launchChangedSampleFraction>.02 &&
    (entry.homeDifference===undefined || entry.homeDifference<.025) &&
    entry.stages.every(s=>s.ready&&s.nonBlackSampleFraction>.02&&(!s.metrics?.gpu || (!s.metrics.gpu[2]&&!s.metrics.gpu[5])));
   if(entry.stages.some(s=>s.metrics?.gpu&&(s.metrics.gpu[2]||s.metrics.gpu[5])) && !inventory.firstGraphicsFailure){
    inventory.firstGraphicsFailure=entry.name;
    try{
     const pending=page.waitForEvent('download',{timeout:120000});
     await page.locator('#export').click();const download=await pending;
     entry.failureCheckpoint=resolve(dir,'graphics-failure.snap');await download.saveAs(entry.failureCheckpoint);
    }catch(error){entry.checkpointError=error.message;}
   }
   entry.captureComplete=true;
  }catch(error){entry.errors.push(error.stack || error.message);entry.captureComplete=false;console.error(`${app.name}: ${error.message}`);await capture('failure').catch(()=>{});}
  finally{entry.pointerEvents=await page.evaluate(()=>window.inventoryPointers || []).catch(()=>[]);const video=page.video();await context.close();if(video)entry.video=await video.path();await writeFile(resolve(dir,'report.json'),JSON.stringify(entry,(_,v)=>typeof v==='bigint'?v.toString():v,2)+'\n');await saveReport();}
  console.log(`${app.name}: evidence saved to ${dir}`);
 }
}finally{await browser.close();await saveReport();}
