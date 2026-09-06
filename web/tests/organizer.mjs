/** Real guest Notes/Contacts/Calendar editing and UI checkpoint reload.
 * BASE_URL=http://localhost:8080 ARTIFACT_DIR=.context/organizer-report node web/tests/organizer.mjs --interactive
 * Interactive JSON steps support tap, type, drag, home, saveReload, wait, shot.
 * Default mode replays the verified Calendar create/edit/delete sequence with
 * Save+reload after each change. ACTIONS_FILE replays custom UI steps.
 * --notes replays Notes create/edit/delete, comparing every RGB pixel below
 * the status bar exactly on reload. REQUIRE_JIT=1 requires generated execution.
 * No guest memory/data injection. Screenshots require semantic review.
 */
import {chromium} from 'playwright';
import {mkdir,writeFile,readFile,mkdtemp,rm} from 'node:fs/promises';
import {resolve} from 'node:path';
import {createInterface} from 'node:readline';
const artifacts=resolve(process.env.ARTIFACT_DIR || '.context/organizer-report');
const notesMode=process.argv.includes('--notes');
const exactReload=notesMode || process.env.EXACT_RELOAD==='1';
await mkdir(artifacts,{recursive:true});
const profile=await mkdtemp(resolve(artifacts,'profile-'));
const context=await chromium.launchPersistentContext(profile,{executablePath:process.env.CHROME_PATH || '/usr/bin/google-chrome',args:['--no-sandbox','--disable-dev-shm-usage'],viewport:{width:1280,height:1000},recordVideo:{dir:artifacts,size:{width:1280,height:1000}}});
const page=context.pages()[0] || await context.newPage();page.setDefaultTimeout(15000);
const began=Date.now();
const report={date:new Date().toISOString(),method:'Original guest keyboard and touch only; explicit browser Save, reload, and guest framebuffer comparison. Human screenshot review confirms contact/event content.',actions:[],checks:[],errors:[],console:[]};
page.on('console',m=>report.console.push({type:m.type(),text:m.text()}));
page.on('pageerror',e=>{report.errors.push(e.message);console.error(e.message);});
const wait=ms=>new Promise(r=>setTimeout(r,ms));
async function persist(){await writeFile(resolve(artifacts,'report.json'),JSON.stringify(report,null,2)+'\n');await writeFile(resolve(artifacts,'actions.json'),JSON.stringify(report.actions.map(({videoSeconds,...a})=>a),null,2)+'\n');}
async function ready(){await page.waitForFunction(()=>window.iphone?.ready,null,{timeout:120000});await page.waitForFunction(()=>Number(document.querySelector('#screen').dataset.frame)>0,null,{timeout:30000});}
async function tap([x,y]){const b=await page.locator('#screen').boundingBox();await page.mouse.move(b.x+x*b.width/320,b.y+y*b.height/480);await page.mouse.down();await wait(180);await page.mouse.up();}
async function pixels(){return page.evaluate(exact=>{const d=document.querySelector('#screen').getContext('2d').getImageData(0,30,320,450).data;const p=[];for(let i=0;i<d.length;i+=exact?4:32)p.push(d[i],d[i+1],d[i+2]);return p;},exactReload);}
function delta(a,b){if(a.length!==b.length)throw Error('Frame dimensions differ');let n=0;for(let i=0;i<a.length;i+=3)if(Math.max(Math.abs(a[i]-b[i]),Math.abs(a[i+1]-b[i+1]),Math.abs(a[i+2]-b[i+2]))>(exactReload?0:20))n++;return n/(a.length/3);}
async function shot(name){await page.screenshot({path:resolve(artifacts,`${name}.png`),fullPage:true});await page.locator('#screen').screenshot({path:resolve(artifacts,`${name}-guest.png`)});report.lastStatus=await page.locator('#status').textContent();report.lastDiagnostics=await page.locator('#diagnostics').textContent();await persist();console.log(`SHOT ${name}`);}
async function pause(value){if(await page.evaluate(()=>window.iphone.paused)!==value)await page.locator('#pause').click();await page.waitForFunction(v=>window.iphone.paused===v&&document.querySelector('#pause').textContent===(v?'Resume':'Pause')&&(v||getComputedStyle(document.querySelector('#screen')).pointerEvents!=='none'),value,{timeout:15000});await wait(300);}
async function notesKeyboard(){await page.waitForFunction(()=>{const d=document.querySelector('#screen').getContext('2d').getImageData(0,0,320,480).data;let keys=0;for(const x of [16,48,80,112,144,176,208,240,272,304]){const i=(282*320+x)*4;if(Math.min(d[i],d[i+1],d[i+2])>180)keys++;}const i=(170*320+150)*4;return keys>=8&&d[i]>180&&d[i+1]>160&&d[i+2]<220;},null,{timeout:20000});await wait(500);}
async function saveReload(label){await pause(true);const before=await pixels();const old=await page.evaluate(()=>window.iphone.savedAt);await page.locator('#save').click();await page.waitForFunction(v=>window.iphone.savedAt>v || /failed/i.test(document.querySelector('#status').textContent),old,{timeout:40000});if(!await page.evaluate(v=>window.iphone.savedAt>v,old))throw Error(await page.locator('#status').textContent());await shot(`${label}-saved`);await page.reload({waitUntil:'domcontentloaded'});await ready();await pause(true);const changed=delta(before,await pixels()),passed=exactReload?changed===0:changed<.005;report.checks.push({name:`${label}-reload-frame`,passed,changedPixelFraction:changed,pixelsCompared:before.length/3,exact:exactReload});await shot(`${label}-restored`);if(!passed)throw Error(`Reload frame differs: ${changed}`);await pause(false);}
async function verifyJit(label){if(process.env.REQUIRE_JIT!=='1')return;await page.waitForFunction(()=>{const j=window.iphone?.metrics?.jit;return j?.enabled&&j.retired>0&&j.calls>0;},null,{timeout:15000});report.checks.push({name:`${label}-generated-execution`,passed:true,jit:await page.evaluate(()=>window.iphone.metrics.jit)});}
const keys={};for(const [row,x0,y]of [['qwertyuiop',16,295],['asdfghjkl',32,348],['zxcvbnm',64,400]])for(let i=0;i<row.length;i++)keys[row[i]]=[x0+i*32,y];keys[' ']=[160,450];keys['<']=[300,400];
async function step(a){report.actions.push({...a,videoSeconds:(Date.now()-began)/1000});console.log(`STEP ${JSON.stringify(a)}`);if(a.note){report.observations ||= [];report.observations.push(a.note);}if(a.pause!==undefined)await pause(a.pause);if(a.export){const downloading=page.waitForEvent('download',{timeout:30000});await page.locator('#export').click();const download=await downloading;await download.saveAs(resolve(artifacts,a.export));}if(a.import){await page.locator('#import').setInputFiles(resolve(a.import));await page.waitForFunction(()=>/backup is restored|Import failed/.test(document.querySelector('#status').textContent),null,{timeout:60000});if(/Import failed/.test(await page.locator('#status').textContent()))throw Error(await page.locator('#status').textContent());}if(a.reload){await page.reload({waitUntil:'domcontentloaded'});await ready();}if(a.tap)await tap(a.tap);if(a.type)for(const ch of a.type.toLowerCase()){if(!keys[ch])throw Error(`Unsupported original-keyboard character ${ch}`);await tap(keys[ch]);await wait(1500);}if(a.home){const b=await page.locator('[data-button="1"]').boundingBox();await page.mouse.move(b.x+b.width/2,b.y+b.height/2);await page.mouse.down();await wait(180);await page.mouse.up();}if(a.drag){const [[x0,y0],[x1,y1]]=a.drag;const b=await page.locator('#screen').boundingBox();await page.mouse.move(b.x+x0*b.width/320,b.y+y0*b.height/480);await page.mouse.down();for(let i=1;i<=22;i++){await page.mouse.move(b.x+(x0+(x1-x0)*i/22)*b.width/320,b.y+(y0+(y1-y0)*i/22)*b.height/480);await wait(100);}await page.mouse.up();}if(a.wait)await wait(a.wait);if(a.keyboard)await notesKeyboard();if(a.saveReload)await saveReload(a.saveReload);if(a.shot)await shot(a.shot);await persist();}
const calendarActions=[
  {
    "tap": [
      120,
      65
    ],
    "wait": 20000,
    "shot": "01-calendar"
  },
  {
    "tap": [
      300,
      42
    ],
    "wait": 8000,
    "shot": "02-add-event"
  },
  {
    "tap": [
      160,
      120
    ],
    "wait": 10000,
    "shot": "03-event-title-editor"
  },
  {
    "type": "meet",
    "wait": 5000,
    "shot": "04-title-meet"
  },
  {
    "tap": [
      290,
      73
    ],
    "wait": 10000,
    "shot": "05-event-form-meet"
  },
  {
    "tap": [
      290,
      73
    ],
    "wait": 15000,
    "shot": "06-event-created-meet"
  },
  {
    "saveReload": "07-calendar-meet"
  },
  {
    "tap": [
      160,
      175
    ],
    "wait": 12000,
    "shot": "08-event-details-after-reload"
  },
  {
    "tap": [
      290,
      42
    ],
    "wait": 10000,
    "shot": "09-edit-event"
  },
  {
    "tap": [
      160,
      125
    ],
    "wait": 12000,
    "shot": "10-edit-event-title"
  },
  {
    "type": "up",
    "wait": 5000,
    "shot": "11-title-meetup"
  },
  {
    "tap": [
      290,
      73
    ],
    "wait": 10000,
    "shot": "12-event-form-meetup"
  },
  {
    "tap": [
      290,
      73
    ],
    "wait": 15000,
    "shot": "13-event-edited-meetup"
  },
  {
    "saveReload": "14-calendar-meetup"
  },
  {
    "tap": [
      290,
      42
    ],
    "wait": 10000,
    "shot": "15-edit-for-delete"
  },
  {
    "tap": [
      160,
      432
    ],
    "wait": 5000,
    "shot": "16-delete-event-confirmation"
  },
  {
    "tap": [
      160,
      371
    ],
    "wait": 15000,
    "shot": "17-event-deleted"
  },
  {
    "saveReload": "18-calendar-deleted"
  }
];
try{
 await page.goto(process.env.BASE_URL || 'http://localhost:8080',{waitUntil:'domcontentloaded'});await ready();await wait(2000);await verifyJit('startup');await shot('00-home');console.log('READY');
 if(process.argv.includes('--interactive')){const input=createInterface({input:process.stdin,terminal:false});for await(const line of input){if(line.trim()==='finish')break;if(!line.trim())continue;try{await step(JSON.parse(line));console.log('READY');}catch(e){report.errors.push(e.message);console.error(e.message);await shot('step-failure');}}}
 else{const actions=process.env.ACTIONS_FILE?JSON.parse(await readFile(process.env.ACTIONS_FILE,'utf8')):notesMode?JSON.parse(await readFile(new URL('./fixtures/notes-actions.json',import.meta.url),'utf8')):calendarActions;for(const a of actions)await step(a);}
 await verifyJit('completed-workflow');
 report.passed=report.errors.length===0;
}catch(e){report.errors.push(e.stack || e.message);report.passed=false;console.error(e.message);await shot('failure').catch(()=>{});}
finally{report.durationSeconds=(Date.now()-began)/1000;const video=page.video();await Promise.all(context.pages().map(p=>p.close()));await context.close();report.video=await video.path();await persist();await rm(profile,{recursive:true,force:true});console.log(`REPORT ${resolve(artifacts,'report.json')}`);}
if(!report.passed)process.exitCode=1;
