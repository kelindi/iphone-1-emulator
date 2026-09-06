/** Genuine guest checkpoints and input cleanup through the public browser UI.
 * BASE_URL=http://localhost:8080 ARTIFACT_DIR=.context/persistence-report node web/tests/persistence.mjs
 * Requires a built web/dist and local server. Uses an isolated browser context;
 * never changes the user's profile. Add --incognito to cover private-mode
 * storage limits; the default uses a disposable ordinary Chrome profile. Pixel checks exclude the guest status bar.
 */
import {chromium} from 'playwright';
import {mkdir, writeFile, stat, open, mkdtemp, rm} from 'node:fs/promises';
import {resolve} from 'node:path';
import {performance} from 'node:perf_hooks';

const baseURL = process.env.BASE_URL || 'http://localhost:8080';
const artifacts = resolve(process.env.ARTIFACT_DIR || '.context/persistence-report');
const timeout = Number(process.env.ACTION_TIMEOUT_MS || 180000);
await mkdir(artifacts, {recursive:true});
const incognito = process.argv.includes('--incognito');
const launch = {executablePath:process.env.CHROME_PATH || '/usr/bin/google-chrome',
  args:['--no-sandbox','--disable-dev-shm-usage']};
const contextOptions = {viewport:{width:1280,height:1000}, acceptDownloads:true,
  ...(process.argv.includes('--record') ? {recordVideo:{dir:artifacts,size:{width:1280,height:1000}}} : {})};
const profile = incognito ? null : await mkdtemp(resolve(artifacts,'chrome-profile-'));
let browser, context;
if (incognito) {
  browser = await chromium.launch(launch);
  context = await browser.newContext(contextOptions);
} else {
  context = await chromium.launchPersistentContext(profile,{...launch,...contextOptions});
  browser = context.browser();
}
await context.addInitScript(() => {
  const openDatabase = IDBFactory.prototype.open;
  IDBFactory.prototype.open = function(...args) {
    const request = Reflect.apply(openDatabase, this, args);
    request.addEventListener('success', () => {
      for (const type of ['error','abort']) request.result.addEventListener(type, event => {
        const error = event.target?.error;
        console.error(`IDB ${type}: ${error?.name || 'unknown'}: ${error?.message || '(empty message)'}`);
      });
    });
    return request;
  };
});
const page = await context.newPage();
const began = performance.now();
const report = {date:new Date().toISOString(), browser:browser.version(), baseURL, incognito,
  method:'Real guest Settings screen and physical ringer latch saved through UI, IndexedDB metadata inspected read-only, raw exported snapshot inspected, reload pixels compared below status bar.',
  checks:[], console:[], failures:[]};
page.on('console', entry => report.console.push({type:entry.type(),text:entry.text()}));
page.on('pageerror', error => report.failures.push(error.message));
const wait = ms => new Promise(resolve => setTimeout(resolve,ms));
function check(name, passed, detail = {}) {
  report.checks.push({name,passed,...detail});
  console.log(`${passed ? 'PASS' : 'FAIL'} ${name}${detail.status ? `: ${detail.status}` : ''}`);
  if (!passed) throw new Error(name);
}
async function status() { return page.locator('#status').textContent(); }
async function shot(name) {
  await page.screenshot({path:resolve(artifacts,`${name}.png`),fullPage:true});
  await page.locator('#screen').screenshot({path:resolve(artifacts,`${name}-guest.png`)});
}
async function ready(target = page) {
  await target.waitForFunction(() => window.iphone?.ready, null, {timeout});
  await target.waitForFunction(() => Number(document.querySelector('#screen').dataset.frame) > 0, null, {timeout:30000});
}
async function pixels() {
  return page.evaluate(() => {
    const data = document.querySelector('#screen').getContext('2d').getImageData(0,30,320,450).data;
    const values=[];
    for (let i=0;i<data.length;i+=32) values.push(data[i],data[i+1],data[i+2]);
    return values;
  });
}
function difference(a,b) {
  let count=0;
  for(let i=0;i<a.length;i+=3)
    if(Math.max(Math.abs(a[i]-b[i]),Math.abs(a[i+1]-b[i+1]),Math.abs(a[i+2]-b[i+2]))>20) count++;
  return count/(a.length/3);
}
async function tap(x,y) {
  const rect=await page.locator('#screen').boundingBox();
  await page.mouse.move(rect.x+x*rect.width/320,rect.y+y*rect.height/480);
  await page.mouse.down(); await wait(180); await page.mouse.up();
}
async function settledChange(name,action,threshold=.1) {
  const before=await pixels(); const start=performance.now();
  let last=before, stableAt=start;
  await action();
  while(performance.now()-start<timeout) {
    await wait(500);
    const current=await pixels();
    if(difference(last,current)>.005) stableAt=performance.now();
    const lit=current.filter(value=>value>30).length/current.length;
    if(difference(before,current)>=threshold && lit>.1 && performance.now()-stableAt>2500 && performance.now()-start>6000) {
      check(name,true,{elapsedMs:performance.now()-start,changedFraction:difference(before,current)});
      await shot(name); return;
    }
    last=current;
  }
  await shot(`${name}-failed`);
  check(name,false,{status:await status()});
}
async function checkpoint() {
  return page.evaluate(async () => {
    const {readCheckpoint}=await import('./storage.js');
    const current=await readCheckpoint();
    const previous=await readCheckpoint('previous');
    return {current:current && {savedAt:current.savedAt,size:current.size,blobSize:current.data.size,version:current.version,encoding:current.encoding || 'raw',storedSize:current.storedSize,compressionMs:current.compressionMs},
      previous:previous && {savedAt:previous.savedAt,size:previous.size}};
  });
}
async function pause() {
  if(!await page.evaluate(()=>window.iphone.paused)) {
    await page.locator('#pause').click();
    await page.waitForFunction(()=>window.iphone.paused,null,{timeout:10000});
    await wait(300);
  }
}
async function resume() {
  if(await page.evaluate(()=>window.iphone.paused)) {
    await page.locator('#pause').click();
    await page.waitForFunction(()=>!window.iphone.paused,null,{timeout:10000});
  }
}

try {
  await page.goto(baseURL,{waitUntil:'domcontentloaded',timeout});
  await ready();
  check('isolated-tab-owns-profile',await page.evaluate(()=>window.iphone.profileOwned));
  await shot('00-initial');
  const hasAlert=await page.evaluate(()=>{
    const p=document.querySelector('#screen').getContext('2d').getImageData(160,315,1,1).data;
    return p[0]+p[1]+p[2]>90;
  });
  if(hasAlert) await settledChange('01-dismiss-alert',()=>tap(160,315),.035);
  await settledChange('02-open-settings',()=>tap(275,250));
  // A settings subpage is distinguishable from the supplied Home snapshot and
  // avoids depending on as-yet-unverified guest keyboard/Notes functionality.
  await page.locator('[data-button="4"]').click();
  await wait(4000);
  check('ringer-on-before-save',(await page.locator('[data-button="4"]').getAttribute('aria-pressed'))==='true');
  await pause();
  const beforeSave=await pixels();
  const previousSavedAt=await page.evaluate(()=>window.iphone.savedAt);
  await page.locator('#save').click();
  await page.waitForFunction(old=>window.iphone.savedAt>old,previousSavedAt,{timeout:30000});
  const committed=await checkpoint();
  report.committed=committed;
  check('manual-save-commits-real-checkpoint',Boolean(committed.current && committed.current.size>100*1048576 && committed.current.size>=committed.current.blobSize));
  check('save-preserves-live-frame',difference(beforeSave,await pixels())===0);
  await shot('03-saved-settings');
  report.storageEstimate = await page.evaluate(() => navigator.storage.estimate());
  report.repeatedSaves = [];
  for (let generation = 2; generation <= 4; generation++) {
    const priorSave = await page.evaluate(() => window.iphone.savedAt);
    const saveBegan = performance.now();
    await page.locator('#save').click();
    await page.waitForFunction(old => window.iphone.savedAt > old || document.querySelector('#status').textContent.startsWith('Save failed:'), priorSave, {timeout:30000});
    const stored = await checkpoint();
    report.repeatedSaves.push({generation,elapsedMs:performance.now()-saveBegan,...stored});
    check(`save-${generation}-retains-previous-generation`, await page.evaluate(old => window.iphone.savedAt > old,priorSave) && Boolean(stored.previous), {status:await status()});
  }

  const downloadPromise=page.waitForEvent('download',{timeout:30000});
  await page.locator('#export').click();
  const download=await downloadPromise;
  const backupPath=resolve(artifacts,'settings-backup.snap');
  await download.saveAs(backupPath);
  const file=await open(backupPath,'r');
  const header=Buffer.alloc(40); await file.read(header,0,header.length,0); await file.close();
  const backup=await stat(backupPath);
  report.storageAfterRepeatedSaves = await page.evaluate(() => navigator.storage.estimate());
  report.export={path:backupPath,bytes:backup.size,version:header.readUInt32LE(16)};
  check('export-is-large-raw-snapshot',backup.size>100*1048576 && header.subarray(0,15).toString()==='S5LBox SNAPSHOT' && header.readUInt32LE(16)===39);

  await page.locator('#import').setInputFiles({name:'invalid.snap',mimeType:'application/octet-stream',buffer:Buffer.from('invalid snapshot for regression test')});
  await page.waitForFunction(()=>document.querySelector('#status').textContent.startsWith('Import failed:'),null,{timeout:30000});
  check('invalid-import-keeps-live-guest',await page.evaluate(()=>window.iphone.ready) && difference(beforeSave,await pixels())===0,{status:await status()});
  check('invalid-import-reaches-snapshot-validator', /snapshot|truncated|checksum/i.test(await status()) && !/quota/i.test(await status()), {status:await status()});
  check('invalid-import-retains-checkpoint',Boolean((await checkpoint()).current?.size>100*1048576));
  await shot('04-invalid-import');

  const second=await context.newPage();
  await second.goto(baseURL,{waitUntil:'domcontentloaded',timeout});
  await ready(second);
  check('second-tab-cannot-save-or-reset',!await second.evaluate(()=>window.iphone.profileOwned) &&
    await second.locator('#save').isDisabled() && await second.locator('#reset').isDisabled() && await second.locator('#import').isDisabled());
  await second.screenshot({path:resolve(artifacts,'05-second-tab.png'),fullPage:true});
  await second.close(); await page.bringToFront();

  await page.reload({waitUntil:'domcontentloaded',timeout});
  await ready(); await pause();
  const restored=await pixels();
  report.restoredFrameDifference=difference(beforeSave,restored);
  check('reload-restores-settings-screen',report.restoredFrameDifference<.005,{changedFraction:report.restoredFrameDifference});
  check('reload-restores-ringer-latch',(await page.locator('[data-button="4"]').getAttribute('aria-pressed'))==='true');
  check('reload-recovers-profile-ownership',await page.evaluate(()=>window.iphone.profileOwned));
  await shot('06-restored-settings');

  await resume();
  await page.locator('[data-button="4"]').click();
  check('ringer-changed-before-valid-import',(await page.locator('[data-button="4"]').getAttribute('aria-pressed'))==='false');
  await wait(4000); await pause();
  await page.locator('#import').setInputFiles(backupPath);
  await page.waitForFunction(()=>document.querySelector('#status').textContent==='Your backup is restored.',null,{timeout:40000});
  check('valid-import-restores-ringer-control',(await page.locator('[data-button="4"]').getAttribute('aria-pressed'))==='true');
  check('valid-import-restores-guest-screen',difference(beforeSave,await pixels())<.005);
  check('valid-import-preserves-paused-state',await page.evaluate(()=>window.iphone.paused));
  await shot('06b-imported-settings');
  await resume();
  const homeBefore=await pixels();
  await page.keyboard.down('Escape');
  await wait(200);
  check('keyboard-shows-pressed-home',await page.locator('[data-button="1"]').evaluate(element=>element.classList.contains('pressed')));
  const other=await context.newPage();
  await other.goto('about:blank'); await other.bringToFront();
  await wait(400);
  // Headless Chrome does not always synthesize OS window blur when switching
  // pages. Record that and dispatch the browser blur event as a fallback.
  const nativeBlurCleared=await page.locator('[data-button="1"]').evaluate(element=>!element.classList.contains('pressed'));
  report.nativeTabSwitchBlur=nativeBlurCleared;
  if(!nativeBlurCleared) await page.evaluate(()=>window.dispatchEvent(new Event('blur')));
  check('blur-clears-pressed-key-ui',await page.locator('[data-button="1"]').evaluate(element=>!element.classList.contains('pressed')));
  await other.close(); await page.bringToFront(); await page.keyboard.up('Escape');
  await wait(15000);
  // A successful save proves C input-idle, including pending guest releases;
  // the public UI refuses to save while a touch or momentary button is held.
  const prior=await page.evaluate(()=>window.iphone.savedAt);
  await page.locator('#save').click();
  await page.waitForFunction(old=>window.iphone.savedAt>old || document.querySelector('#status').textContent.startsWith('Save failed:'),prior,{timeout:30000});
  check('blur-releases-guest-input-and-can-save',await page.evaluate(old=>window.iphone.savedAt>old,prior),{homeFrameChanged:difference(homeBefore,await pixels())});
  await shot('07-after-keyboard-blur');
  if (process.argv.includes('--notes')) {
    report.noteVideoStartSeconds=(performance.now()-began)/1000;
    await settledChange('08-open-notes',()=>tap(200,250));
    const blankNote=await pixels();
    // The stock on-screen keyboard, never host text injection. Auto-capital-
    // ization makes H uppercase and the following keys spell "Hello".
    for (const [x,y] of [[192,348],[80,295],[288,348],[288,348],[272,295]]) {
      await tap(x,y); await wait(1500);
    }
    await wait(6000);
    check('guest-keyboard-changes-note',difference(blankNote,await pixels())>.002);
    await shot('09-typed-note');
    await tap(289,43); // Done in the actual Notes navigation bar.
    await wait(8000);
    await pause();
    const savedNote=await pixels();
    const priorNoteSave=await page.evaluate(()=>window.iphone.savedAt);
    await page.locator('#save').click();
    await page.waitForFunction(old=>window.iphone.savedAt>old,priorNoteSave,{timeout:30000});
    await shot('10-saved-note');
    await page.reload({waitUntil:'domcontentloaded',timeout});
    await ready(); await pause();
    const noteDifference=difference(savedNote,await pixels());
    check('reload-restores-written-note',noteDifference<.005,{changedFraction:noteDifference});
    await shot('11-restored-note');
    report.noteVideoEndSeconds=(performance.now()-began)/1000;
  }
  report.passed=report.failures.length===0;
} catch(error) {
  report.passed=false; report.failures.push(error.stack || error.message);
  console.error(error.message); await shot('failure').catch(()=>{});
} finally {
  report.durationSeconds=(performance.now()-began)/1000;
  const video=page.video();
  await context.close();
  if (video) report.video=await video.path();
  await browser.close();
  await writeFile(resolve(artifacts,'report.json'),JSON.stringify(report,null,2)+'\n');
  if (profile) await rm(profile,{recursive:true,force:true});
  console.log(`Report: ${resolve(artifacts,'report.json')}`);
}
if(!report.passed) process.exitCode=1;
