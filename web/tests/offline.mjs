// Real-browser acceptance: install a verified bundle, disconnect, reload, then
// corrupt a firmware cache entry and exercise recovery after reconnect. The
// minimalist shell exposes recovery in the phone while setup remains hidden.
import {chromium} from 'playwright';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';

const directory = process.env.ARTIFACT_DIR || '.context/offline-report';
await fs.mkdir(directory, {recursive:true});
const browser = await chromium.launch({executablePath:process.env.CHROME_PATH || '/usr/bin/google-chrome',
  args:['--no-sandbox','--disable-dev-shm-usage']});
const context = await browser.newContext({viewport:{width:1280,height:1050},
  ...(process.argv.includes('--record') ? {recordVideo:{dir:directory,size:{width:1280,height:1050}}} : {})});
const page = await context.newPage();
const report = {startedAt:new Date().toISOString(), checks:[]};
const began = performance.now();
const check = (name, condition) => {
  assert.ok(condition, name);
  report.checks.push({name,atSeconds:(performance.now()-began)/1000});
};
const ready = () => page.waitForFunction(()=>window.iphone?.ready, {}, {timeout:40000});
try {
  await page.goto(process.env.BASE_URL || 'http://localhost:8080');
  await ready();
  await page.locator('#offline').evaluate(button => button.click());
  await page.waitForFunction(()=>document.getElementById('status').textContent.includes('available offline'), {}, {timeout:60000});
  await page.waitForFunction(()=>navigator.serviceWorker.controller);
  check('offline bundle installed and controls current page', true);
  await context.setOffline(true);
  check('browser network disconnected', await page.evaluate(()=>!navigator.onLine));
  await page.reload(); await ready();
  check('genuine guest starts after disconnected reload', await page.locator('#screen').getAttribute('data-ready') === 'true');
  await page.screenshot({path:`${directory}/offline.png`});
  await page.waitForTimeout(2000);
  await page.evaluate(async () => {
    const cache = await caches.open('iphone-1-assets-1a543a-v1');
    const manifest = await (await fetch('manifest.json')).json();
    await cache.put(new URL(manifest.kernel, location.href), new Response('corrupted test entry'));
  });
  await page.reload();
  await page.waitForFunction(() => {
    const retry = document.querySelector('#retry');
    return !retry.hidden && !retry.disabled;
  }, null, {timeout:30000});
  check('corrupt offline firmware stops the guest and enables recovery', !await page.evaluate(()=>window.iphone.ready));
  check('corrupt cache entry was removed', await page.evaluate(async()=>{
    const manifest=await(await fetch('manifest.json')).json();
    return !(await (await caches.open('iphone-1-assets-1a543a-v1')).match(new URL(manifest.kernel,location.href)));
  }));
  await page.screenshot({path:`${directory}/corrupt-offline.png`});
  await context.setOffline(false);
  await page.locator('#retry').click(); await ready();
  check('retry downloads verified firmware and restores guest', true);
  await page.screenshot({path:`${directory}/recovered.png`});
  await page.waitForTimeout(2000);
  report.result = 'pass';
} catch (error) { report.result='fail'; report.error=error.stack; throw error; }
finally {
  await fs.writeFile(`${directory}/report.json`, JSON.stringify(report,null,2)+'\n');
  await context.close(); await browser.close();
  console.log(JSON.stringify(report,null,2));
}
