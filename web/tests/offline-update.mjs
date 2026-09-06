// Real Chrome upgrade gate. The two test bundles run the genuine guest; only
// diagnostic shell/manifest markers and a valid WASM custom section differ.
// All serving/cache/profile mutations are isolated from the live web root.
import {chromium} from 'playwright';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import {resolve, dirname, join} from 'node:path';
import {createHash} from 'node:crypto';
import {spawn} from 'node:child_process';

const directory = resolve(process.env.ARTIFACT_DIR || '.context/offline-update-report');
const root = join(directory, 'web');
const sourceRoot = resolve(process.env.SOURCE_WEB_ROOT || 'web');
const previousRoot = process.env.PREVIOUS_WEB_ROOT && resolve(process.env.PREVIOUS_WEB_ROOT);
assert.notEqual(root,sourceRoot,'Upgrade test must use an isolated web root');
assert.notEqual(root,previousRoot,'Upgrade test must preserve its previous firmware source');
const port = Number(process.env.PORT || 8091);
const url = `http://localhost:${port}/`;
const files = ['index.html','phone.css','icon.svg','app.js','storage.js','assets.js',
  'checkpoint-worker.js','emu-worker.js','manifest.json','dist/emu.js','dist/emu.wasm',
  'seed-jit.js','jit/seed.wasm','jit/regions.json','jit/abi.json'];
const sha = bytes => createHash('sha256').update(bytes).digest('hex');
await fs.mkdir(root, {recursive:true});
// Link immutable firmware within the isolated root; do not alter production files.
for (const name of await fs.readdir(join(sourceRoot, 'firmware'))) {
  const source = join(sourceRoot, 'firmware', name), target = join(root, 'firmware', name);
  if (!(await fs.stat(source)).isFile()) continue;
  await fs.mkdir(dirname(target), {recursive:true});
  // The artifact directory may contain links from an older firmware run.
  // Replacing the link does not modify the preserved source inode.
  await fs.rm(target,{force:true});
  await fs.link(source,target);
}
const source = Object.fromEntries(await Promise.all(files.map(async name => [name, await fs.readFile(join(sourceRoot,name))])));
const previousManifest = previousRoot && await fs.readFile(join(previousRoot,'manifest.json'));
const template = await fs.readFile(join(sourceRoot,'service-worker.template.js'),'utf8');
const variants = {};
function leb(n) { const bytes=[]; do { let b=n&127; n>>>=7; if(n)b|=128; bytes.push(b); } while(n); return Buffer.from(bytes); }
async function publish(version) {
  const contents = {...source};
  contents['app.js'] = Buffer.concat([source['app.js'], Buffer.from(`\ndocument.documentElement.dataset.testBundle = '${version}';\n`)]);
  const firmwareManifest = JSON.parse(version === 'A' && previousManifest ? previousManifest : source['manifest.json']);
  contents['manifest.json'] = Buffer.from(JSON.stringify({...firmwareManifest, testBundle:version}));
  if (previousRoot) {
    // Exercise a real firmware generation change while retaining the saved
    // phone. Replace directory entries, never mutate shared hardlinked bytes.
    const firmwareRoot = version === 'A' ? previousRoot : sourceRoot;
    for (const name of new Set(['kernel','devicetree','rootfs','snapshot'].flatMap(key => [firmwareManifest[key]].flat()).filter(Boolean))) {
      const target = join(root,name), staged = `${target}.next`;
      await fs.mkdir(dirname(target),{recursive:true});
      await fs.link(join(firmwareRoot,name),staged);
      await fs.rename(staged,target);
      // POSIX rename is a no-op when both names already identify the same
      // inode (unchanged kernel/tree), so remove that leftover staging name.
      await fs.rm(staged,{force:true});
    }
  }
  const name = Buffer.from(`offline-update-test-${version}`), section = Buffer.concat([leb(name.length),name]);
  contents['dist/emu.wasm'] = Buffer.concat([source['dist/emu.wasm'],Buffer.from([0]),leb(section.length),section]);
  const digest = createHash('sha256').update(template), hashes = {};
  for (const name of files) {
    digest.update(name).update(contents[name]); hashes[name]=sha(contents[name]);
    const path=join(root,name); await fs.mkdir(dirname(path),{recursive:true});
    await fs.writeFile(path, contents[name]);
  }
  const generation = digest.digest('hex').slice(0,24);
  const sw=template.replace('__VERSION__',generation).replace('__FILES__',JSON.stringify(hashes));
  await fs.writeFile(join(root,'service-worker.next'),sw);
  await fs.rename(join(root,'service-worker.next'),join(root,'service-worker.js'));
  return variants[version]={generation,hashes};
}
await publish('A');
const server=spawn(process.execPath,['tools/web-server.mjs'],{env:{...process.env,WEB_ROOT:root,PORT:String(port)},stdio:['ignore','pipe','pipe']});
await new Promise((resolve,reject)=> { server.stdout.once('data',resolve); server.once('error',reject); server.once('exit',code=>reject(new Error(`Server exit ${code}`))); });
const browser=await chromium.launch({executablePath:process.env.CHROME_PATH || '/usr/bin/google-chrome',args:['--no-sandbox','--disable-dev-shm-usage']});
const context=await browser.newContext({viewport:{width:1280,height:1050},
  ...(process.argv.includes('--record') ? {recordVideo:{dir:directory,size:{width:1280,height:1050}}}: {})});
let page=await context.newPage();
const report={startedAt:new Date().toISOString(),sourceRoot,previousRoot,checks:[],errors:[],variants};
const began=performance.now();
const check=(name,condition,detail)=> { assert.ok(condition,name); report.checks.push({name,atSeconds:(performance.now()-began)/1000,...(detail?{detail}:{})}); };
const observe=page=>page.on('pageerror',error=>report.errors.push(error.message)); observe(page);
const ready=async()=> { await page.waitForFunction(()=>window.iphone?.ready,{}, {timeout:60000}); };
const pause=async()=> { if (!await page.evaluate(()=>window.iphone.paused)) await page.locator('#pause').click(); };
async function bundle() {
  return page.evaluate(async()=> {
    const bytes=await (await fetch('dist/emu.wasm')).arrayBuffer();
    const hash=[...new Uint8Array(await crypto.subtle.digest('SHA-256',bytes))].map(n=>n.toString(16).padStart(2,'0')).join('');
    return {shell:document.documentElement.dataset.testBundle,manifest:(await (await fetch('manifest.json')).json()).testBundle,wasm:hash};
  });
}
async function installedEngine() {
  // Restored checkpoints can be paused before the worker's first metrics
  // message. Briefly resume only the live guest to observe installation, then
  // restore Pause. The durable checkpoint bytes remain untouched.
  const wasPaused=await page.evaluate(()=>window.iphone.paused);
  if(wasPaused) await page.locator('#pause').click();
  await page.waitForFunction(()=>window.iphone.metrics.jit?.enabled &&
    window.iphone.metrics.jit.regions>0,{}, {timeout:30000});
  const stats=await page.evaluate(()=>window.iphone.metrics.jit);
  if(wasPaused) await pause();
  return stats;
}
async function saved() {
  return page.evaluate(async()=> {
    const {readCheckpoint}=await import('./storage.js'); const record=await readCheckpoint();
    const hash=[...new Uint8Array(await crypto.subtle.digest('SHA-256',await record.data.arrayBuffer()))].map(n=>n.toString(16).padStart(2,'0')).join('');
    return {savedAt:record.savedAt,version:record.version,size:record.size,storedBytes:record.data.size,hash};
  });
}
try {
  await page.goto(url); await ready();
  check('new visitor is not automatically registered for offline use',await page.evaluate(async()=>!(await navigator.serviceWorker.getRegistration())));
  await page.locator('#offline').click();
  await page.waitForFunction(()=>document.getElementById('status').textContent.includes('available offline'),{}, {timeout:60000});
  await page.waitForFunction(()=>navigator.serviceWorker.controller);
  await page.reload(); await ready();
  check('version A controls a genuine running guest', (await bundle()).shell==='A');
  // Settings is a genuine distinct guest screen; save it while paused.
  const box=await page.locator('#screen').boundingBox();
  await page.mouse.click(box.x+275/320*box.width,box.y+250/480*box.height,{delay:30});
  await page.waitForTimeout(12000); await pause();
  await page.screenshot({path:join(directory,'settings-before-update.png')});
  const beforeSave=await page.evaluate(()=>window.iphone.savedAt);
  await page.locator('#save').click();
  await page.waitForFunction(old=>window.iphone.savedAt>old && !document.getElementById('save').disabled,beforeSave,{timeout:60000});
  const original=await saved(); report.savedBefore=original;
  check('real guest checkpoint is durably stored before upgrade',original.size>100*1048576);
  await publish('B');
  // Reload remains controlled by A, whose startup check discovers B.
  await page.reload(); await ready(); await pause();
  await page.locator('#update-notice').waitFor({state:'visible',timeout:60000});
  check('startup detects waiting update without another Keep offline click',await page.evaluate(async()=>Boolean((await navigator.serviceWorker.getRegistration()).waiting)));
  const oldBundle=await bundle(); report.waitingBundle=oldBundle;
  check('waiting update preserves matching old shell, manifest and WASM',oldBundle.shell==='A' && oldBundle.manifest==='A' && oldBundle.wasm===variants.A.hashes['dist/emu.wasm']);
  check('waiting update leaves saved checkpoint unchanged',JSON.stringify(await saved())===JSON.stringify(original));
  await page.screenshot({path:join(directory,'update-waiting.png')});
  const notice=await page.locator('#update-notice').textContent();
  check('notice asks to Save and close every phone tab',notice.includes('Save') && notice.includes('close all phone tabs'));
  // A second controlled tab also holds A active; closing only one cannot update.
  const second=await context.newPage(); await second.goto(url);
  await second.waitForFunction(()=>window.iphone?.ready,{}, {timeout:60000});
  check('another phone tab cannot own the saved profile',!await second.evaluate(()=>window.iphone.profileOwned));
  await page.close(); page=second; await pause();
  check('remaining phone tab retains version A while B waits',(await bundle()).shell==='A' && await page.evaluate(async()=>Boolean((await navigator.serviceWorker.getRegistration()).waiting)));
  await page.close();
  // No in-scope clients now remain. Let the browser complete normal activation.
  await new Promise(resolve=>setTimeout(resolve,2000));
  page=await context.newPage(); observe(page); await page.goto(url); await ready(); await pause();
  const newBundle=await bundle(); report.activatedBundle=newBundle;
  check('closing all tabs and reopening activates matching version B',newBundle.shell==='B' && newBundle.manifest==='B' && newBundle.wasm===variants.B.hashes['dist/emu.wasm']);
  const after=await saved(); report.savedAfter=after;
  check('upgrade retains identical checkpoint bytes and metadata',JSON.stringify(after)===JSON.stringify(original));
  check('new bundle restores genuine saved guest and profile ownership',await page.evaluate(()=>window.iphone.ready && window.iphone.profileOwned && window.iphone.savedAt>0));
  report.activatedEngine=await installedEngine();
  check('version B installs the matching seeded engine',report.activatedEngine.enabled && report.activatedEngine.regions>0,report.activatedEngine);

  check('waiting notice clears after normal activation',await page.locator('#update-notice').isHidden());
  await page.screenshot({path:join(directory,'settings-after-update.png')});
  await context.setOffline(true); await page.reload(); await ready(); await pause();
  report.disconnected = {...await bundle(), online:await page.evaluate(()=>navigator.onLine), networkAvailable:await page.evaluate(async()=>{try { await fetch('uncached-network-probe', {cache:'no-store'}); return true; } catch { return false; }})};
  check('new bundle and saved guest reopen while disconnected',report.disconnected.shell==='B' && !report.disconnected.networkAvailable,report.disconnected);
  check('background offline update failure does not replace save status',!(await page.locator('#status').textContent()).includes('failed'));
  report.disconnectedEngine=await installedEngine();
  check('version B seeded engine installs while disconnected',report.disconnectedEngine.enabled && report.disconnectedEngine.regions>0,report.disconnectedEngine);

  check('no uncaught page errors',report.errors.length===0,report.errors);
  report.result='pass';
} catch(error) { report.result='fail'; report.error=error.stack; await page.screenshot({path:join(directory,'failure.png')}).catch(()=>{}); throw error; }
finally {
  await fs.writeFile(join(directory,'report.json'),JSON.stringify(report,null,2)+'\n');
  await context.close(); await browser.close(); server.kill('SIGTERM');
  console.log(JSON.stringify(report,null,2));
}
