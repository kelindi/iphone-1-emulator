// Startup must show the genuine guest lock screen, including saved phones.
import {chromium} from 'playwright';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
const dir=process.env.ARTIFACT_DIR||'.context/open-lock';
await fs.mkdir(dir,{recursive:true});
const browser=await chromium.launch({executablePath:process.env.CHROME_PATH||'/usr/bin/google-chrome',args:['--no-sandbox','--disable-dev-shm-usage']});
const context=await browser.newContext({viewport:{width:1000,height:950},recordVideo:{dir,size:{width:1000,height:950}}});
const page=await context.newPage();
const began=performance.now(), report={checks:[]};
const check=name=>report.checks.push({name,seconds:(performance.now()-began)/1000});
const ready=()=>page.waitForFunction(()=>window.iphone?.ready,null,{timeout:90000});
async function locked(name) {
  // The Earth wallpaper and white slider handle occupy distinct guest pixels.
  await page.waitForFunction(()=>{
    const ctx=document.querySelector('#screen').getContext('2d');
    const earth=ctx.getImageData(50,130,220,240).data;
    const knob=ctx.getImageData(26,414,60,20).data;
    let blue=0,white=0;
    for(let i=0;i<earth.length;i+=4) if(earth[i+2]>earth[i]+30&&earth[i+2]>earth[i+1]) blue++;
    for(let i=0;i<knob.length;i+=4) if(knob[i]>180&&knob[i+1]>180&&knob[i+2]>180) white++;
    return blue>5000&&white>500;
  },null,{timeout:20000});
  await page.screenshot({path:`${dir}/${name}.png`});check(name);
}
async function slide(end=285) {
  const b=await page.locator('#screen').boundingBox();
  const point=x=>[b.x+x*b.width/320,b.y+430*b.height/480];
  await page.mouse.move(...point(45));await page.mouse.down();
  for(let i=1;i<=24;i++){await page.mouse.move(...point(45+(end-45)*i/24));await page.waitForTimeout(60);}
  await page.mouse.up();
}
async function home() {
  await page.waitForFunction(()=>{
    const p=document.querySelector('#screen').getContext('2d').getImageData(20,40,60,60).data;
    let green=0;for(let i=0;i<p.length;i+=4)if(p[i+1]>p[i]+25&&p[i+1]>p[i+2]+25)green++;
    return green>500;
  },null,{timeout:20000});
  await page.waitForTimeout(1200);
}
async function save() {
  const previous=await page.evaluate(()=>window.iphone.savedAt);
  // Save controls are intentionally hidden in the minimalist production shell.
  await page.locator('#save').evaluate(button=>button.click());
  await page.waitForFunction(previous=>window.iphone.savedAt>previous,previous,{timeout:90000});
  await page.waitForFunction(()=>!document.querySelector('#save').disabled,null,{timeout:30000});
}
try {
  await page.goto(process.env.BASE_URL||'http://localhost:8080');await ready();
  await locked('01-fresh-open');
  await slide(140);await page.waitForTimeout(1500);await locked('02-short-slide-stays-locked');
  await slide();await home();check('full slide opens Home');
  await page.screenshot({path:`${dir}/03-unlocked.png`});
  await save();await page.reload();await ready();await locked('04-saved-home-opens-locked');
  await save();await page.reload();await ready();await locked('05-saved-lock-opens-locked');
  await page.locator('[data-button="0"]').click({delay:180});
  await page.waitForFunction(()=>{
    const p=document.querySelector('#screen').getContext('2d').getImageData(0,0,320,480).data;
    return p.every((v,i)=>i%4===3||v===0);
  },null,{timeout:20000});
  await save();await page.reload();await ready();await locked('06-saved-sleep-wakes-locked');
  await slide();await home();check('restored phone still unlocks');
  assert.equal(report.checks.length,7);report.result='pass';
} catch(error) {report.result='fail';report.error=error.stack;await page.screenshot({path:`${dir}/failure.png`});throw error;}
finally {
  await fs.writeFile(`${dir}/report.json`,JSON.stringify(report,null,2)+'\n');
  const video=page.video();await context.close();await video.saveAs(`${dir}/open-lock.webm`);await browser.close();
  console.log(JSON.stringify(report,null,2));
}
