import {chromium} from 'playwright';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
const dir=process.env.ARTIFACT_DIR||'.context/3d-view/acceptance';await fs.mkdir(dir,{recursive:true});
const browser=await chromium.launch({executablePath:'/usr/bin/google-chrome',args:['--no-sandbox','--disable-dev-shm-usage','--enable-unsafe-swiftshader']});
const context=await browser.newContext({viewport:{width:1200,height:1000},recordVideo:{dir,size:{width:1200,height:1000}}});
const page=await context.newPage(), started=performance.now(), errors=[], report={checks:[]};
page.on('pageerror',e=>errors.push(e.message));
const check=name=>report.checks.push({name,seconds:(performance.now()-started)/1000});
const shot=name=>page.screenshot({path:`${dir}/${name}.png`});
// Project chosen physical points independently of the view's picking code.
async function point(x,y,angled=false) {
  return page.evaluate(async({x,y,angled})=>{
    const T=await import('./vendor/three/three.module.js');
    const r=document.querySelector('#phone-3d').getBoundingClientRect(), camera=new T.PerspectiveCamera(32,r.width/r.height,.1,100);
    camera.position.z=Math.max(23,7.6/camera.aspect/Math.tan(16*Math.PI/180)/2);camera.updateMatrixWorld();
    const v=new T.Vector3(x,y,.55);if(angled)v.applyEuler(new T.Euler(-.08,-.32,0));v.project(camera);
    return [r.x+(v.x+1)*r.width/2,r.y+(1-v.y)*r.height/2];
  },{x,y,angled});
}
async function guestHome() {
  await page.waitForFunction(()=>{const p=document.querySelector('#screen').getContext('2d').getImageData(20,40,60,60).data;let n=0;for(let i=0;i<p.length;i+=4)if(p[i+1]>p[i]+25&&p[i+1]>p[i+2]+25)n++;return n>500;},null,{timeout:25000});
}
try {
  let resumeWorker, resumeModel;
  const workerGate=new Promise(resolve=>{resumeWorker=resolve;}), modelGate=new Promise(resolve=>{resumeModel=resolve;});
  await page.route('**/emu-worker.js',async route=>{await workerGate;await route.continue();});
  await page.route('**/phone-model.js',async route=>{await modelGate;await route.continue();});
  await page.goto(process.env.BASE_URL||'http://localhost:8080',{waitUntil:'domcontentloaded'});
  assert(await page.locator('#view-3d').isEnabled());
  await page.locator('#view-3d').click();
  await page.waitForFunction(()=>document.querySelector('#view-3d').textContent==='Loading…');
  check('click shows feedback while model downloads');
  resumeModel();await page.waitForFunction(()=>document.querySelector('.device-stage').dataset.view==='3d');
  assert(!await page.evaluate(()=>Boolean(window.iphone?.ready)));await shot('00-model-before-boot');check('3D opens before emulator is ready');
  resumeWorker();await page.waitForFunction(()=>window.iphone?.ready,null,{timeout:90000});
  await page.waitForTimeout(200);await shot('00-live-after-boot');check('loading screen becomes the live guest');
  await page.locator('#view-2d').click();
  assert.equal(await page.locator('#view-2d').getAttribute('aria-pressed'),'true');check('2D remains available');
  await page.locator('#view-3d').click();await page.waitForSelector('#phone-3d');await page.waitForTimeout(1200);await shot('01-angled-lock');
  assert.equal(await page.locator('#view-3d').getAttribute('aria-pressed'),'true');check('3D opens with live lock screen');
  const p=x=>point((x/320-.5)*4.9,.22+(.5-430/480)*7.35,true);
  await page.mouse.move(...await p(45));await page.mouse.down();
  for(let i=1;i<=24;i++){await page.mouse.move(...await p(45+i*10));await page.waitForTimeout(60);}await page.mouse.up();
  await guestHome();check('angled slide unlocks the real guest');await shot('02-unlocked');
  // Drag the physical chin to turn the phone over; it must not touch the guest.
  await page.mouse.move(...await point(1.5,-4.7,true));await page.mouse.down();
  await page.mouse.move(980,750,{steps:40});await page.mouse.up();await page.waitForTimeout(500);await shot('03-rotated-back');check('case drag rotates model');
  await page.locator('#view-reset').click();await page.waitForTimeout(200);
  // Launch Settings through the 3D screen and use the 3D Home button to return.
  await page.mouse.click(...await point((275/320-.5)*4.9,.22+(.5-250/480)*7.35),{delay:100});
  await page.waitForFunction(()=>{const p=document.querySelector('#screen').getContext('2d').getImageData(0,60,320,350).data;let light=0;for(let i=0;i<p.length;i+=4)if(p[i]>200&&p[i+1]>200&&p[i+2]>200)light++;return light>30000;},null,{timeout:25000});check('3D screen opens Settings');await shot('04-settings');
  await page.mouse.click(...await point(0,-4.65),{delay:180});await guestHome();check('3D Home button returns from app');
  await page.locator('#phone-3d').focus();for(let i=0;i<16;i++)await page.keyboard.press('ArrowRight');
  await page.waitForTimeout(300);await shot('05-back-keyboard');check('keyboard rotates to aluminum back');
  await page.locator('#view-reset').click();await page.mouse.move(600,500);await page.mouse.wheel(0,-100);await page.waitForTimeout(200);await shot('06-zoom');check('wheel zoom works');
  await page.locator('#view-2d').click();assert(await page.locator('.phone').isVisible());assert(await page.locator('#phone-3d').isHidden());await guestHome();check('2D switch preserves guest');
  await page.setViewportSize({width:390,height:844});await page.locator('#view-3d').click();await page.waitForTimeout(500);await shot('07-mobile');
  assert(await page.locator('#view-2d').isVisible());assert(await page.locator('#view-reset').isVisible());check('mobile viewport fits model and controls');
  await page.locator('#phone-3d').evaluate(canvas=>canvas.getContext('webgl2').getExtension('WEBGL_lose_context').loseContext());
  await page.waitForFunction(()=>document.querySelector('.device-stage').dataset.view==='2d');
  assert(await page.locator('#view-3d').isDisabled());assert(await page.evaluate(()=>window.iphone.ready));check('WebGL context loss returns safely to 2D');
  assert.deepEqual(errors,[]);check('no page errors');report.result='pass';
}catch(error){report.result='fail';report.error=error.stack;await shot('failure');throw error;}
finally{await fs.writeFile(`${dir}/report.json`,JSON.stringify(report,null,2)+'\n');const v=page.video();await context.close();await v.saveAs(`${dir}/phone-3d.webm`);await browser.close();console.log(JSON.stringify(report,null,2));}
