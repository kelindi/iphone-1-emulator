import {chromium} from 'playwright';
import assert from 'node:assert/strict';
import {mkdir,writeFile} from 'node:fs/promises';
const dir=process.env.ARTIFACT_DIR || '.context/recovery-ui-report'; await mkdir(dir,{recursive:true});
const report={checks:[],errors:[],scope:'Real original guest startup/retry after controlled HTTP503; runtime fatal is injected at Worker message boundary, not an actual core failure.'};
const began=performance.now(); const check=(name,v)=>{assert.ok(v,name);report.checks.push({name,seconds:(performance.now()-began)/1000});};
const browser=await chromium.launch({executablePath:'/usr/bin/google-chrome',args:['--no-sandbox','--disable-dev-shm-usage']});
const context=await browser.newContext({viewport:{width:1000,height:900},recordVideo:{dir,size:{width:1000,height:900}}});
await context.addInitScript(()=>{const Original=Worker;window.testWorkers=[];window.Worker=class extends Original{constructor(...args){super(...args);window.testWorkers.push(this);}};});
const page=await context.newPage();page.setDefaultTimeout(10000);page.on('pageerror',e=>report.errors.push(e.message));
let rejectManifest=true;await context.route('**/manifest.json',route=>rejectManifest?route.fulfill({status:503,body:'Controlled loading failure'}):route.continue());
const ready=()=>page.waitForFunction(()=>window.iphone?.ready,null,{timeout:60000});
try{
 await page.goto(process.env.BASE_URL || 'http://localhost:8080');
 await page.getByRole('button',{name:'Try again',exact:true}).waitFor({state:'visible',timeout:30000});
 check('HTTP503 failure visible inside phone',await page.locator('#boot-label').textContent()==='Could not load phone manifest (503)');
 check('phone stops and disables Home',!await page.evaluate(()=>window.iphone.ready)&&await page.getByRole('button',{name:'Home',exact:true}).isDisabled());
 check('missing save does not offer recovery',!await page.locator('#recover').isVisible());
 await page.screenshot({path:dir+'/loading-error.png'});await page.waitForTimeout(1200);
 rejectManifest=false;await page.getByRole('button',{name:'Try again',exact:true}).click();
 check('retry immediately restores loading state',await page.locator('#boot-loader').getAttribute('data-state')==='loading');
 await ready(); await page.waitForTimeout(1500);
 check('real guest ready with error overlay hidden',!await page.locator('#boot-loader').isVisible());
 check('minimalist controls remain hidden',!await page.locator('.companion').isVisible());
 check('real guest framebuffer drawn',await page.evaluate(()=>Number(document.querySelector('#screen').dataset.frame)>0));
 await page.screenshot({path:dir+'/recovered-home.png'});
 await page.evaluate(()=>window.testWorkers.find(w=>String(w.constructor).includes('extends')).dispatchEvent(new MessageEvent('message',{data:{type:'fatal',message:'Controlled runtime failure'}})));
 await page.getByRole('button',{name:'Try again',exact:true}).waitFor({state:'visible'});
 check('runtime failure reveals overlay after ready',await page.locator('#boot-label').textContent()==='Controlled runtime failure');
 check('runtime error stops input',!await page.evaluate(()=>window.iphone.ready)&&await page.getByRole('button',{name:'Home',exact:true}).isDisabled());
 await page.screenshot({path:dir+'/runtime-error.png'});await page.waitForTimeout(1200);
 await page.getByRole('button',{name:'Try again',exact:true}).focus();await page.keyboard.press('Enter');await ready();
 check('keyboard retry restarts genuine guest',await page.evaluate(()=>window.iphone.ready));
 check('keyboard retry clears error overlay',!await page.locator('#boot-loader').isVisible());
 await page.waitForTimeout(1200);check('no page errors',report.errors.length===0);report.result='pass';
}catch(e){report.result='fail';report.error=e.stack;throw e;}
finally{report.elapsedSeconds=(performance.now()-began)/1000;await writeFile(dir+'/report.json',JSON.stringify(report,null,2)+'\n');await context.close();await browser.close();}
