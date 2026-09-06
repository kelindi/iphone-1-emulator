// Exercise physical GPIO controls and the genuine lock-screen slider.
import {chromium} from 'playwright';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
const dir=process.env.ARTIFACT_DIR||'.context/controls-report';await fs.mkdir(dir,{recursive:true});
const browser=await chromium.launch({executablePath:process.env.CHROME_PATH||'/usr/bin/google-chrome',args:['--no-sandbox','--disable-dev-shm-usage']});
const context=await browser.newContext({viewport:{width:1280,height:1050},recordVideo:{dir,size:{width:1280,height:1050}}});
const page=await context.newPage();const began=performance.now();
const report={checks:[],startedAt:new Date().toISOString()};
const check=(name,condition)=>{assert.ok(condition,name);report.checks.push({name,atSeconds:(performance.now()-began)/1000});};
const shot=name=>page.screenshot({path:`${dir}/${name}.png`});
async function jitEvidence(stage) {
  if (process.env.REQUIRE_JIT === '1') {
    await page.waitForFunction(() => {
      const jit = window.iphone?.metrics?.jit;
      return jit?.enabled && jit.regions > 0 && jit.calls > 0 && jit.retired > 0;
    }, null, {timeout:15000});
    check(`compiled seed executes ${stage}`, true);
  }
  (report.jit ||= {})[stage] = await page.evaluate(() => window.iphone?.metrics?.jit);
}
try {
  await page.goto(process.env.BASE_URL||'http://localhost:8080');
  await page.waitForFunction(()=>window.iphone?.ready,{}, {timeout:40000});
  if (process.env.FIXTURE_SNAPSHOT) {
    report.fixture = process.env.FIXTURE_SNAPSHOT;
    await page.locator('#import').setInputFiles(report.fixture);
    await page.waitForFunction(() => /backup is restored|Import failed/.test(document.querySelector('#status').textContent), null, {timeout:120000});
    assert.doesNotMatch(await page.locator('#status').textContent(), /Import failed/);
  }
  await page.waitForTimeout(2000);
  await jitEvidence('before controls');
  await page.locator('[data-button="2"]').click({delay:180});
  await page.waitForTimeout(1200);await shot('01-volume-up');
  await page.locator('[data-button="3"]').click({delay:180});
  await page.waitForTimeout(1000);await shot('02-volume-down');
  await page.locator('[data-button="4"]').click();
  check('silent switch latched',await page.locator('[data-button="4"]').getAttribute('aria-pressed')==='true');
  await page.waitForTimeout(1200);await shot('03-silent');
  await page.locator('[data-button="0"]').click({delay:180});
  await page.waitForFunction(()=>{const p=document.querySelector('#screen').getContext('2d').getImageData(0,0,320,480).data;return p.every((v,i)=>i%4===3||v===0);},{},{timeout:15000});
  check('sleep disables the real LCD',true);await shot('04-sleep');
  await page.waitForTimeout(2000);
  await page.locator('[data-button="0"]').click({delay:180});
  await page.waitForFunction(()=>{const p=document.querySelector('#screen').getContext('2d').getImageData(0,0,320,480).data;let lit=0;for(let i=0;i<p.length;i+=40)if(p[i]+p[i+1]+p[i+2]>60)lit++;return lit>500;},{},{timeout:15000,polling:30});
  check('wake restores live display pixels',true);await shot('05-lock-screen');
  const box=await page.locator('#screen').boundingBox();
  const point=(x,y)=>[box.x+x*box.width/320,box.y+y*box.height/480];
  await page.mouse.move(...point(45,430));await page.mouse.down();
  for(let i=1;i<=24;i++){await page.mouse.move(...point(45+i*10,430));await page.waitForTimeout(60);}
  await page.mouse.up();
  await page.waitForFunction(()=>{const p=document.querySelector('#screen').getContext('2d').getImageData(20,40,60,60).data;let green=0;for(let i=0;i<p.length;i+=4)if(p[i+1]>p[i]+25&&p[i+1]>p[i+2]+25)green++;return green>500;},{},{timeout:20000});
  check('real slide-to-unlock returns to Home',true);await shot('06-unlocked');
  await page.waitForTimeout(2000);await jitEvidence('after controls');report.result='pass';
}catch(error){report.result='fail';report.error=error.stack;await shot('failure');throw error;}
finally{report.diagnostics=await page.locator('#diagnostics').textContent().catch(()=>null);await fs.writeFile(`${dir}/report.json`,JSON.stringify(report,null,2)+'\n');await context.close();await browser.close();console.log(JSON.stringify(report,null,2));}
