/** Real checkpoint work must not disable Pause/Resume for its full duration.
 * BASE_URL=http://localhost:8080 node web/tests/save-pause.mjs
 * Requires Chrome; records the actual guest and Save/Pause controls.
 */
import {chromium} from 'playwright';
import {mkdir, writeFile} from 'node:fs/promises';
import {resolve} from 'node:path';
const directory = resolve(process.env.ARTIFACT_DIR || '.context/save-pause-report');
await mkdir(directory, {recursive:true});
const browser = await chromium.launch({executablePath:process.env.CHROME_PATH || '/usr/bin/google-chrome', args:['--no-sandbox','--disable-dev-shm-usage']});
const context = await browser.newContext({viewport:{width:1280,height:1000}, recordVideo:{dir:directory,size:{width:1280,height:1000}}});
const report = {date:new Date().toISOString(), url:process.env.BASE_URL || 'http://localhost:8080', checks:[], errors:[]};
async function check(name, passed, evidence = {}) {
  report.checks.push({name,passed,...evidence});
  if (!passed) throw new Error(name);
}
await context.route('**/emu-worker.js', async route => {
  const response = await route.fetch(), source = await response.text();
  const seam = 'onmessage = async ({data:message}) => {';
  if (!source.includes(seam)) throw new Error('Worker diagnostic seam changed');
  await route.fulfill({response,body:source.replace(seam,seam+`
  if (message.type === 'savePauseRead') {
    send('savePauseRead', {id:message.id,ticks:api?.ticks(),cycles:Number(api?.cycles()),paused,now:performance.now()});
    return;
  }`)});
});
await context.addInitScript(() => {
  const Base = window.Worker;
  window.Worker = class extends Base {
    constructor(...args) { super(...args); if (String(args[0]).includes('emu-worker.js')) window.savePauseWorker = this; }
  };
  let sequence = 0;
  window.savePauseRead = () => new Promise(resolve => {
    const id = ++sequence, worker = window.savePauseWorker;
    const listener = ({data}) => {
      if (data.type !== 'savePauseRead' || data.id !== id) return;
      worker.removeEventListener('message',listener); resolve(data);
    };
    worker.addEventListener('message',listener); worker.postMessage({type:'savePauseRead',id});
  });
});
const page = await context.newPage();
page.on('pageerror',error => report.errors.push(error.message));
const read = () => page.evaluate(() => window.savePauseRead());
const wait = ms => new Promise(resolve => setTimeout(resolve,ms));
async function toggle(label) {
  const start = performance.now();
  await page.locator('#pause').click();
  await page.waitForFunction(label => document.querySelector('#pause').textContent === label,label);
  const state = await read();
  await check(`acknowledge ${label} request`, state.paused === (label === 'Resume'), {milliseconds:performance.now()-start});
  return state;
}
try {
  await page.goto(report.url);
  await page.waitForFunction(() => window.iphone?.ready,null,{timeout:120000});
  await page.locator('#save').click();
  await check('Save is in progress and Pause stays enabled', await page.locator('#save').isDisabled() && await page.locator('#pause').isEnabled());
  const paused = await toggle('Resume');
  await wait(500);
  const held = await read();
  await check('paused guest does not advance during checkpoint work',held.ticks === paused.ticks && held.cycles === paused.cycles,{ticks:held.ticks,cycles:held.cycles});
  await check('checkpoint remains in progress during pause',await page.locator('#save').isDisabled());
  await toggle('Pause');
  const before = await read(); await wait(500); const after = await read();
  await check('resumed guest advances while checkpoint finishes',after.ticks > before.ticks && after.cycles > before.cycles);
  await check('Save still runs while Resume takes effect',await page.locator('#save').isDisabled());
  const finalPause = await toggle('Resume');
  await page.waitForFunction(() => !document.querySelector('#save').disabled,null,{timeout:60000});
  await check('Save finishes successfully while paused',await page.evaluate(() => window.iphone.savedAt > 0 && window.iphone.paused));
  await check('Save completion preserves paused status',/^Paused\./.test(await page.locator('#status').textContent()));
  const completed = await read();
  await check('Save completion does not resume guest time',completed.ticks === finalPause.ticks && completed.cycles === finalPause.cycles);
  await page.screenshot({path:resolve(directory,'saved-and-paused.png')});
  await toggle('Pause');
  const resumeStart = await read(); await wait(500); const resumed = await read();
  await check('normal Resume still works after Save',resumed.ticks > resumeStart.ticks && resumed.cycles > resumeStart.cycles);
  await page.reload();
  await page.waitForFunction(() => window.iphone?.ready,null,{timeout:120000});
  await check('saved snapshot reloads as the real phone',await page.evaluate(() => window.iphone.savedAt > 0 && !/failed/i.test(document.querySelector('#status').textContent)));
  report.passed = report.errors.length === 0;
} catch(error) { report.errors.push(error.stack || error.message); report.passed = false; }
finally {
  const video = page.video(); await context.close(); report.video = await video.path();
  await browser.close(); await writeFile(resolve(directory,'report.json'),JSON.stringify(report,null,2)+'\n');
}
console.log(JSON.stringify(report,null,2));
if (!report.passed) process.exitCode = 1;
