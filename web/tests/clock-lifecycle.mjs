/** Real foreground/background clock regression on a disposable Chrome profile.
 * Requires Chrome and either DISPLAY or Xvfb. Uses Chrome's default context
 * with Playwright's focus override disabled, so tabs can really become hidden.
 * BASE_URL=http://localhost:8080 node web/tests/clock-lifecycle.mjs
 */
import {chromium} from 'playwright';
import {spawn} from 'node:child_process';
import {mkdir, mkdtemp, readFile, writeFile, rm, rename} from 'node:fs/promises';
import {resolve} from 'node:path';

const dir = resolve(process.env.ARTIFACT_DIR || '.context/clock-lifecycle-report');
await mkdir(dir, {recursive:true});
const profile = await mkdtemp(resolve(dir, 'chrome-profile-'));
const report = {date:new Date().toISOString(), url:process.env.BASE_URL || 'http://localhost:8080', checks:[], errors:[]};
const wait = ms => new Promise(resolve => setTimeout(resolve, ms));
let chrome, xvfb, browser, context, page;
async function check(name, passed, evidence = {}) {
  report.checks.push({name, passed, ...evidence});
  await writeFile(resolve(dir, 'report.json'), JSON.stringify(report, null, 2) + '\n');
  if (!passed) throw new Error(name);
}
async function launch() {
  let display = process.env.DISPLAY;
  if (!display) {
    xvfb = spawn(process.env.XVFB_PATH || 'Xvfb', ['-displayfd','1','-screen','0','1440x1100x24'], {stdio:['ignore','pipe','pipe']});
    display = await new Promise((resolve, reject) => {
      let output = '';
      const timer = setTimeout(() => reject(new Error('Xvfb did not start')), 10000);
      xvfb.once('error', error => { clearTimeout(timer); reject(error); });
      xvfb.stdout.on('data', chunk => {
        output += chunk;
        if (/^\d+\n/.test(output)) { clearTimeout(timer); resolve(':' + output.trim()); }
      });
    });
  }
  chrome = spawn(process.env.CHROME_PATH || '/usr/bin/google-chrome', [
    '--no-sandbox','--disable-dev-shm-usage','--no-first-run','--no-default-browser-check',
    '--remote-debugging-port=0',`--user-data-dir=${profile}`,'--window-size=1280,1000','about:blank'
  ], {env:{...process.env, DISPLAY:display}, stdio:'ignore'});
  let launchError;
  chrome.once('error', error => { launchError = error; });
  let port;
  for (let attempt = 0; attempt < 100; attempt++) {
    if (launchError) throw launchError;
    if (chrome.exitCode !== null) throw new Error('Chrome exited before starting');
    try { port = (await readFile(resolve(profile, 'DevToolsActivePort'), 'utf8')).split('\n')[0]; break; }
    catch { await wait(100); }
  }
  if (!port) throw new Error('Chrome did not expose its debugging port');
  // Both options are supported by the pinned Playwright CDP client. isLocal
  // permits real >50 MB file-input backups without streaming a remote upload.
  browser = await chromium.connectOverCDP(`http://localhost:${port}`, {noDefaults:true, isLocal:true});
  context = browser.contexts()[0];
  // Diagnostics are request-routed only in this test; production has no probe.
  await context.route('**/emu-worker.js', async route => {
    const response = await route.fetch();
    const source = await response.text();
    const seam = 'onmessage = async ({data:message}) => {';
    if (!source.includes(seam)) throw new Error('Worker diagnostic seam changed');
    const body = source.replace(seam, seam + `
  if (message.type === 'clockTestRead') {
    send('clockTestRead', {id:message.id, ticks:api?.ticks(), cycles:Number(api?.cycles()), paused, now:performance.now()});
    return;
  }`);
    await route.fulfill({response, body});
  });
  await context.addInitScript(() => {
    const Base = window.Worker;
    window.Worker = class extends Base {
      constructor(...args) { super(...args); if (String(args[0]).includes('emu-worker.js')) window.clockTestWorker = this; }
    };
    let sequence = 0;
    window.clockTestRead = () => new Promise(resolve => {
      const id = ++sequence, worker = window.clockTestWorker;
      const listener = ({data}) => {
        if (data.type !== 'clockTestRead' || data.id !== id) return;
        worker.removeEventListener('message', listener); resolve(data);
      };
      worker.addEventListener('message', listener);
      worker.postMessage({type:'clockTestRead', id});
    });
  });
  page = context.pages()[0];
  page.on('pageerror', error => report.errors.push(error.message));
}
const read = () => page.evaluate(() => window.clockTestRead());
async function togglePause(label) {
  const began = Date.now();
  await page.locator('#pause').click();
  await page.waitForFunction(label => document.querySelector('#pause').textContent === label, label);
  return Date.now() - began;
}
async function rate(name, milliseconds = 1000) {
  const before = await read(); await wait(milliseconds); const after = await read();
  const guest = (after.ticks - before.ticks) / 6e6, wall = (after.now - before.now) / 1000;
  await check(name, !after.paused && guest / wall > .9 && guest / wall < 1.08, {guest, wall, ratio:guest / wall});
}
async function jitEvidence(stage, newerThan = 0) {
  if (process.env.REQUIRE_JIT === '1') {
    await page.waitForFunction(generation => {
      const jit = window.iphone?.metrics?.jit;
      return jit?.enabled && jit.regions > 0 && jit.calls > 0 && jit.retired > 0 && jit.generation > generation;
    }, newerThan, {timeout:15000});
    await check(`compiled seed executes ${stage}`, true);
  }
  const jit = await page.evaluate(() => window.iphone?.metrics?.jit);
  (report.jit ||= {})[stage] = jit;
  return jit;
}
try {
  await launch();
  await page.goto(report.url);
  await page.bringToFront();
  await page.waitForFunction(() => window.iphone?.ready, null, {timeout:120000});
  if (process.env.FIXTURE_SNAPSHOT) {
    await page.locator('#import').setInputFiles(resolve(process.env.FIXTURE_SNAPSHOT));
    await page.waitForFunction(() => /backup is restored|Import failed/.test(document.querySelector('#status').textContent), null, {timeout:90000});
    if (/Import failed/.test(await page.locator('#status').textContent())) throw new Error('Fixture import failed');
  }
  const initialJit = await jitEvidence('before lifecycle');
  await rate('foreground clock follows wall', 2000);
  const pauseMs = await togglePause('Resume');
  let before = await read(); await wait(1000); let after = await read();
  await check('Pause responds within 1s and stops time', pauseMs < 1000 && before.ticks === after.ticks, {pauseMs, before, after});
  const resumeMs = await togglePause('Pause');
  await check('Resume responds within 1s', resumeMs < 1000, {resumeMs});
  await rate('Resume resets the old clock anchor');

  const other = await context.newPage(); await other.goto('about:blank'); await other.bringToFront();
  await page.waitForFunction(() => document.hidden);
  before = await read(); await wait(1000); after = await read();
  await check('real hidden tab pauses and holds guest time', before.paused && after.paused && before.ticks === after.ticks, {before, after});
  const cdp = await context.newCDPSession(page);
  await cdp.send('Page.setWebLifecycleState', {state:'frozen'});
  await wait(2000);
  await cdp.send('Page.setWebLifecycleState', {state:'active'});
  after = await read();
  await check('real frozen tab stays at the same guest time', after.paused && before.ticks === after.ticks, {before, after});
  await page.bringToFront(); await page.waitForFunction(() => !document.hidden); await wait(200);
  await rate('becoming visible resumes without catch-up');

  await togglePause('Resume'); before = await read();
  await other.bringToFront(); await page.waitForFunction(() => document.hidden);
  await cdp.send('Page.setWebLifecycleState', {state:'frozen'}); await wait(1000);
  await cdp.send('Page.setWebLifecycleState', {state:'active'});
  await page.bringToFront(); await page.waitForFunction(() => !document.hidden); after = await read();
  await check('manual Pause survives hiding and freezing', after.paused && before.ticks === after.ticks, {before, after});
  await other.close();

  // Use the real Export/Import UI. The exact reference clock comes from this
  // paused phone, so the test does not depend on one particular boot snapshot.
  const checkpoint = await read();
  // noDefaults also leaves download policy untouched. Enable a disposable
  // native download directory explicitly and observe Chrome's completion.
  const downloads = await browser.newBrowserCDPSession();
  await downloads.send('Browser.setDownloadBehavior', {behavior:'allowAndName', downloadPath:dir, eventsEnabled:true});
  const downloadPromise = new Promise((resolve, reject) => {
    let guid;
    const timer = setTimeout(() => reject(new Error('Export download timed out')), 90000);
    downloads.on('Browser.downloadWillBegin', event => { guid = event.guid; });
    downloads.on('Browser.downloadProgress', event => {
      if (event.guid !== guid) return;
      if (event.state === 'completed') { clearTimeout(timer); resolve(guid); }
      if (event.state === 'canceled') { clearTimeout(timer); reject(new Error('Export download canceled')); }
    });
  });
  await page.locator('#export').click();
  const guid = await downloadPromise;
  const backup = resolve(dir, 'paused-phone.iphone'); await rename(resolve(dir, guid), backup);
  await downloads.detach();
  await togglePause('Pause'); await wait(500); await togglePause('Resume');
  await page.locator('#import').setInputFiles(backup);
  await page.waitForFunction(() => /backup is restored|Import failed/.test(document.querySelector('#status').textContent), null, {timeout:90000});
  if (/Import failed/.test(await page.locator('#status').textContent())) throw new Error('Import failed');
  before = await read(); await wait(500); after = await read();
  await check('paused import restores exact clock and retired instructions', before.paused && after.paused && before.ticks === checkpoint.ticks && after.ticks === checkpoint.ticks && before.cycles === checkpoint.cycles && after.cycles === checkpoint.cycles, {checkpoint, before, after});
  await togglePause('Pause'); await rate('restored phone resumes at wall rate');
  await jitEvidence('after restore', initialJit?.generation || 0);
  await page.screenshot({path:resolve(dir, 'final.png')});
} catch (error) {
  report.errors.push(error.stack || error.message); process.exitCode = 1;
} finally {
  report.passed = report.errors.length === 0;
  await writeFile(resolve(dir, 'report.json'), JSON.stringify(report, null, 2) + '\n');
  if (browser) {
    try { await (await browser.newBrowserCDPSession()).send('Browser.close'); } catch {}
    await browser.close().catch(() => {});
  }
  if (chrome && chrome.exitCode === null) chrome.kill('SIGTERM');
  if (xvfb && xvfb.exitCode === null) xvfb.kill('SIGTERM');
  // This directory was created solely by this invocation; keep exported phone
  // evidence, but remove its throwaway browser cache/profile after shutdown.
  await wait(300);
  await rm(profile, {recursive:true, force:true, maxRetries:5, retryDelay:200});
  console.log(JSON.stringify(report, null, 2));
}
