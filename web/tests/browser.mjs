/** Real guest navigation evidence, not merely WASM initialization.
 * BASE_URL=http://localhost:8080 ARTIFACT_DIR=.context/browser-report node web/tests/browser.mjs
 * Add --baseline for the original main-thread frontend. Requires playwright + Chrome.
 * Saves a JSON report, console log, full-page/guest screenshots, and an unedited WebM.
 * Frame-change/settling checks are smoke assertions; inspect screenshots to confirm
 * app identity. These timings include settling and are NOT product latency gates.
 */
import { chromium } from 'playwright';
import { mkdir, writeFile } from 'node:fs/promises';
import { resolve } from 'node:path';
import { performance } from 'node:perf_hooks';

const baseline = process.argv.includes('--baseline');
const baseURL = process.env.BASE_URL || 'http://localhost:8080';
const artifactDir = resolve(process.env.ARTIFACT_DIR || '.context/browser-report');
const timeout = Number(process.env.ACTION_TIMEOUT_MS || 120000);
const startupTimeout = Number(process.env.STARTUP_TIMEOUT_MS || 240000);
const pointFrom = (name, fallback) => process.env[name]?.split(',').map(Number) || fallback;
const settingsPoint = pointFrom('SETTINGS_POINT', [275, 250]);
const safariPoint = pointFrom('SAFARI_POINT', [200, 430]);
await mkdir(artifactDir, { recursive: true });
const browser = await chromium.launch({
  executablePath: process.env.CHROME_PATH || '/usr/bin/google-chrome',
  args: ['--no-sandbox', '--disable-dev-shm-usage'],
});
const viewport = baseline ? { width: 1000, height: 1240 } : { width: 1280, height: 1000 };
const context = await browser.newContext({ viewport, recordVideo: { dir: artifactDir, size: viewport } });
const page = await context.newPage();
page.setDefaultTimeout(10000);
const start = performance.now();
const report = {
  baseURL, baseline, viewport, browser: browser.version(), date: new Date().toISOString(),
  timingMethod: 'Wall time from action dispatch through changed, nonblack, stable guest framebuffer; includes 6s minimum observation and 2s settling. Not input latency.',
  actions: [], console: [], failures: [],
};
page.on('console', message => report.console.push({ seconds: (performance.now() - start) / 1000, type: message.type(), text: message.text() }));
page.on('pageerror', error => report.failures.push(`Page error: ${error.message}`));
page.on('response', response => {
  if (response.status() >= 400 && !response.url().endsWith("/favicon.ico")) report.failures.push(`HTTP ${response.status()}: ${response.url()}`);
});
const wait = ms => new Promise(resolve => setTimeout(resolve, ms));

// Sample guest pixels below the status bar, which legitimately changes over time.
async function pixels() {
  return page.evaluate(() => {
    const data = document.querySelector('#screen').getContext('2d').getImageData(0, 0, 320, 480).data;
    const values = [];
    let lit = 0, white = 0;
    for (let y = 30; y < 480; y += 6) for (let x = 3; x < 320; x += 6) {
      const i = (y * 320 + x) * 4;
      values.push(data[i], data[i + 1], data[i + 2]);
      if (Math.max(data[i], data[i + 1], data[i + 2]) > 25) lit++;
      if (Math.min(data[i], data[i + 1], data[i + 2]) > 235) white++;
    }
    return { values, lit: lit / (values.length / 3), white: white / (values.length / 3) };
  });
}
function difference(a, b) {
  let changed = 0;
  for (let i = 0; i < a.values.length; i += 3) {
    if (Math.max(Math.abs(a.values[i] - b.values[i]), Math.abs(a.values[i + 1] - b.values[i + 1]), Math.abs(a.values[i + 2] - b.values[i + 2])) > 25) changed++;
  }
  return changed / (a.values.length / 3);
}
async function evidence(name) {
  await page.screenshot({ path: resolve(artifactDir, `${name}.png`), fullPage: true });
  await page.locator('#screen').screenshot({ path: resolve(artifactDir, `${name}-guest.png`) });
  return { status: await page.locator('#status').textContent(), diagnostics: await page.locator('#diagnostics').textContent().catch(() => null) };
}
async function settle(before, began, requiredDifference = .1, ready = () => true) {
  let previous = await pixels();
  let stableSince = performance.now();
  let maxDifference = 0;
  let changedAt = null;
  while (performance.now() - began < timeout) {
    await wait(400);
    const current = await pixels();
    const status = await page.locator('#status').textContent();
    if (/failed|could not|panic|unreachable/i.test(status)) throw new Error(`Runtime failure: ${status}`);
    const delta = difference(before, current);
    maxDifference = Math.max(maxDifference, delta);
    if (delta >= requiredDifference && changedAt === null) changedAt = performance.now() - began;
    if (difference(previous, current) > .008 || current.lit < .08) stableSince = performance.now();
    if (delta >= requiredDifference && current.lit >= .08 && ready(current) && performance.now() - stableSince >= 2000 && performance.now() - began >= 6000) {
      return { firstSignificantChangeMs: changedAt, settledMs: performance.now() - began, changedPixelFraction: delta, litPixelFraction: current.lit, whitePixelFraction: current.white };
    }
    previous = current;
  }
  throw new Error(`Guest did not settle after ${timeout}ms (largest changed-pixel fraction ${maxDifference.toFixed(3)})`);
}
async function tap([x, y]) {
  const box = await page.locator('#screen').boundingBox();
  await page.mouse.move(box.x + (x / 320) * box.width, box.y + (y / 480) * box.height);
  await page.mouse.down();
  await wait(180);
  await page.mouse.up();
}
async function home() {
  if (baseline) {
    await page.keyboard.down('Escape');
    await wait(180);
    await page.keyboard.up('Escape');
  } else {
    const box = await page.locator('[data-button="1"]').boundingBox();
    await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
    await page.mouse.down();
    await wait(180);
    await page.mouse.up();
  }
}
async function action(name, run, threshold = .1, ready = () => true) {
  const before = await pixels();
  const began = performance.now();
  const entry = { name, videoStartSeconds: (began - start) / 1000 };
  report.actions.push(entry);
  console.log(`Starting ${name} at ${entry.videoStartSeconds.toFixed(1)}s`);
  try {
    await run();
    Object.assign(entry, await settle(before, began, threshold, ready), await evidence(name), { passed: true });
    console.log(`${name}: settled ${(entry.settledMs / 1000).toFixed(1)}s; ${entry.status?.split('\n')[0]}`);
  } catch (error) {
    Object.assign(entry, { passed: false, error: error.message }, await evidence(`${name}-failed`));
    throw error;
  }
}

try {
  await page.goto(baseURL, { waitUntil: 'domcontentloaded', timeout: startupTimeout });
  await page.waitForFunction(isBaseline => {
    const text = document.querySelector('#status')?.textContent || '';
    if (/failed|error/i.test(text)) throw new Error(text);
    return isBaseline ? /^Running/.test(text) : document.querySelector('#pause')?.disabled === false;
  }, baseline, { timeout: startupTimeout });
  report.startupMs = performance.now() - start;
  await page.waitForFunction(() => {
    const data = document.querySelector('#screen').getContext('2d').getImageData(0, 0, 320, 480).data;
    let lit = 0;
    for (let i = 0; i < data.length; i += 40) if (data[i] + data[i + 1] + data[i + 2] > 60) lit++;
    return lit > 500;
  }, null, { timeout: 30000 });
  report.startup = await evidence('00-startup');
  console.log(`Runtime ready in ${(report.startupMs / 1000).toFixed(1)}s`);
  if (!baseline) {
    report.minimalShell = await page.evaluate(() => {
      const companion = document.querySelector('.companion');
      const page = document.documentElement;
      return {
        controlsHidden: companion.hidden,
        viewportFits: page.scrollWidth === innerWidth && page.scrollHeight === innerHeight,
      };
    });
    if (!report.minimalShell.controlsHidden || !report.minimalShell.viewportFits)
      throw new Error(`Minimal shell is visible or overflows: ${JSON.stringify(report.minimalShell)}`);
    report.shellButtons = [];
    for (const expected of ['Resume', 'Pause']) {
      const buttonStart = performance.now();
      await page.locator('#pause').evaluate(button => button.click());
      await page.waitForFunction(label => document.querySelector('#pause').textContent.trim() === label, expected, {timeout: 1000});
      const responseMs = performance.now() - buttonStart;
      report.shellButtons.push({ labelAfterClick: expected, responseMs });
      if (responseMs > 1000) throw new Error(`Pause/resume button response exceeded 1s: ${responseMs}ms`);
    }
  }
  // Some supplied snapshots already have Repair Needed dismissed. Only tap
  // the modal button when its center is lit; Home's empty fourth row is black.
  const hasAlert = await page.evaluate(() => {
    const p = document.querySelector('#screen').getContext('2d').getImageData(160, 315, 1, 1).data;
    return p[0] + p[1] + p[2] > 90;
  });
  if (hasAlert) await action('01-dismiss-alert', () => tap([160, 315]), .035);
  else report.actions.push({ name: '01-dismiss-alert', skipped: true, reason: 'Snapshot has no alert at the expected button location' });
  const homeFrame = await pixels();
  // A paused app-close zoom can look stable for seconds on this slow guest.
  // Require the actual Home layout, not merely a changed nonblack image.
  const isHome = frame => difference(homeFrame, frame) < .04;
  // A title on an empty striped background is not a loaded Settings list.
  await action('02-settings', () => tap(settingsPoint), .1, frame => frame.white > .25);
  await action('03-home-from-settings', home, .1, isHome);
  await action('04-safari', () => tap(safariPoint));
  await action('05-home-from-safari', home, .1, isHome);
  report.passed = report.failures.length === 0;
} catch (error) {
  report.passed = false;
  report.failures.push(error.stack || error.message);
  await evidence('failure').catch(() => {});
  console.error(error.message);
} finally {
  report.durationSeconds = (performance.now() - start) / 1000;
  const video = page.video();
  await context.close();
  report.video = await video.path();
  await browser.close();
  await writeFile(resolve(artifactDir, 'report.json'), JSON.stringify(report, null, 2) + '\n');
  await writeFile(resolve(artifactDir, 'console.log'), report.console.map(entry => `[${entry.seconds.toFixed(3)}] ${entry.type}: ${entry.text}`).join('\n') + '\n');
  console.log(`Report: ${resolve(artifactDir, 'report.json')}`);
}
if (!report.passed) process.exitCode = 1;
