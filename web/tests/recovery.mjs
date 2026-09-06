/** Real-browser recovery with a genuine UI-exported snapshot fixture.
 * First run persistence.mjs, then:
 * FIXTURE_SNAPSHOT=.context/persistence-report/settings-backup.snap node web/tests/recovery.mjs
 * Fault injection aborts real IndexedDB transactions; the guest worker is real.
 */
import {chromium} from 'playwright';
import assert from 'node:assert/strict';
import {mkdir,stat,writeFile,readFile} from 'node:fs/promises';
import {createReadStream,createWriteStream} from 'node:fs';
import {pipeline} from 'node:stream/promises';
import {createGzip} from 'node:zlib';
import {resolve} from 'node:path';
import {createServer} from 'node:http';
const artifacts=resolve(process.env.ARTIFACT_DIR || '.context/recovery-report');
const fixture=resolve(process.env.FIXTURE_SNAPSHOT || '.context/persistence-report/settings-backup.snap');
const baseURL=process.env.BASE_URL || 'http://localhost:8080';
await mkdir(artifacts,{recursive:true});
const rawSize=(await stat(fixture)).size;
const gzipPath=resolve(artifacts,'fixture.snap.gz');
await pipeline(createReadStream(fixture),createGzip(),createWriteStream(gzipPath));
const browser=await chromium.launch({executablePath:process.env.CHROME_PATH || '/usr/bin/google-chrome',args:['--no-sandbox','--disable-dev-shm-usage']});
const context=await browser.newContext({viewport:{width:1280,height:1000},acceptDownloads:true,
  ...(process.argv.includes('--record') ? {recordVideo:{dir:artifacts,size:{width:1280,height:1000}}} : {})});
const fixtureServer=createServer((req,res)=>{res.writeHead(200,{'Content-Type':'application/gzip','Access-Control-Allow-Origin':'*'});createReadStream(gzipPath).pipe(res);});
await new Promise(resolve=>fixtureServer.listen(0,'127.0.0.1',resolve));
const fixtureURL=`http://127.0.0.1:${fixtureServer.address().port}/fixture.gz`;
await context.addInitScript(()=>{
  const put=IDBObjectStore.prototype.put;
  IDBObjectStore.prototype.put=function(...args){
    const req=Reflect.apply(put,this,args);
    if(window.__abortCheckpointWrites && this.name==='checkpoints' && args[1]==='current') this.transaction.abort();
    return req;
  };
  const transaction=IDBDatabase.prototype.transaction;
  IDBDatabase.prototype.transaction=function(...args){
    if(localStorage.getItem('__denyCheckpointReads')==='true' && args[0]==='checkpoints' && (!args[1] || args[1]==='readonly'))
      throw new DOMException('Storage access denied by recovery test','SecurityError');
    return Reflect.apply(transaction,this,args);
  };
});
const page=await context.newPage();
const started=performance.now();
const report={fixture,rawSize,method:'Genuine exported Settings snapshot seeded as an explicit IndexedDB test fixture; actual browser UI and guest emulator, with deliberate corrupt bytes/version and native transaction aborts.',checks:[],console:[]};
page.on('console',entry=>report.console.push({type:entry.type(),text:entry.text()}));
page.on('pageerror',error=>report.console.push({type:'pageerror',text:error.message}));
function check(name,value,details={}){assert.ok(value,name);report.checks.push({name,atSeconds:(performance.now()-started)/1000,...details});console.log(`PASS ${name}`);}
async function ready(){await page.waitForFunction(()=>window.iphone?.ready && Number(document.querySelector('#screen').dataset.frame)>0,null,{timeout:120000});await page.waitForTimeout(5000);}
async function failed(){await page.locator('#retry').waitFor({state:'visible',timeout:120000});assert.equal(await page.evaluate(()=>window.iphone.ready),false);}
async function shot(name){await page.screenshot({path:resolve(artifacts,`${name}.png`)});await page.locator('#screen').screenshot({path:resolve(artifacts,`${name}-guest.png`)});}
async function pixels(){return page.evaluate(()=>Array.from(document.querySelector('#screen').getContext('2d').getImageData(0,30,320,450).data));}
async function pause(){if(!await page.evaluate(()=>window.iphone.paused)){await page.locator('#pause').click();await page.waitForFunction(()=>window.iphone.paused && document.querySelector('#pause').textContent==='Resume');}}
async function seed(kind){
  await page.goto(`${baseURL}/icon.svg`);
  await page.evaluate(async({kind,rawSize,fixtureURL})=>{
    const {PROFILE_VERSION}=await import('./storage.js');
    const data=await(await fetch(fixtureURL)).blob();
    const previous={version:PROFILE_VERSION,savedAt:1700000000000,size:rawSize,storedSize:data.size,encoding:'gzip',data};
    let current={...previous,savedAt:1700000001000};
    if(kind==='gzip') current={...current,data:new Blob([new Uint8Array([0x1f,0x8b,0,0,7,8,9])]),storedSize:7};
    if(kind==='native') current={...current,data:new Blob(['not a real machine snapshot']),size:27,storedSize:27,encoding:'raw'};
    if(kind==='version') current={...current,version:'m68-1a543a-snapshot999'};
    const db=await new Promise((resolve,reject)=>{const r=indexedDB.open('iphone-1',1);r.onupgradeneeded=()=>r.result.createObjectStore('checkpoints');r.onsuccess=()=>resolve(r.result);r.onerror=()=>reject(r.error);});
    await new Promise((resolve,reject)=>{const tx=db.transaction('checkpoints','readwrite'),store=tx.objectStore('checkpoints');store.put(previous,'previous');store.put(current,'current');tx.oncomplete=resolve;tx.onabort=()=>reject(tx.error);});db.close();
  },{kind,rawSize,fixtureURL});
}
async function fingerprints(){return page.evaluate(async()=>{
  const {readCheckpoint}=await import('./storage.js');const result={};
  for(const generation of ['current','previous']){const r=await readCheckpoint(generation);result[generation]=r?{version:r.version,savedAt:r.savedAt,size:r.size,encoding:r.encoding,hash:Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256',await r.data.arrayBuffer()))).map(x=>x.toString(16).padStart(2,'0')).join('')}:null;}
  return result;
});}
async function sameStored(expected,name){check(name,JSON.stringify(await fingerprints())===JSON.stringify(expected));}
try{
  await seed('valid');await page.goto(baseURL);await ready();await pause();
  const expected=await pixels();await shot('00-fixture-settings');
  check('genuine fixture loads and renders before corruption',expected.some(x=>x>100));
  for(const kind of ['gzip','native']){
    await seed(kind);const before=await fingerprints();await page.goto(baseURL);await failed();
    check(`${kind} corruption blocks guest startup`,!await page.evaluate(()=>window.iphone.ready),{status:await page.locator('#status').textContent()});
    check(`${kind} failure offers Export and earlier-checkpoint recovery`,await page.locator('#export').isEnabled() && await page.locator('#recover').isVisible());
    await sameStored(before,`${kind} failed startup leaves both generations intact`);
    const [download]=await Promise.all([page.waitForEvent('download'),page.locator('#export').click()]);
    const filename=resolve(artifacts,`${kind}-bad-backup${kind==='gzip'?'.snap.gz':'.snap'}`);await download.saveAs(filename);
    const storedBytes=await page.evaluate(async()=>Array.from(new Uint8Array(await(await(await import('./storage.js')).readCheckpoint()).data.arrayBuffer())));
    check(`${kind} Export preserves corrupt backup bytes exactly`,Buffer.from(storedBytes).equals(await readFile(filename)),{suggestedFilename:download.suggestedFilename()});
    await shot(`01-${kind}-failed`);
    await page.locator('#recover').click();await ready();await pause();
    check(`${kind} recovery restores exact Settings framebuffer`,JSON.stringify(await pixels())===JSON.stringify(expected));
    await sameStored(before,`${kind} recovery does not overwrite saved generations`);
    await shot(`02-${kind}-recovered`);
  }
  await seed('version');const incompatible=await fingerprints();await page.goto(baseURL);await failed();
  check('incompatible metadata version blocks startup and enables backup',/save needs|version/i.test(await page.locator('#status').textContent()) && await page.locator('#export').isEnabled());
  await sameStored(incompatible,'incompatible version is retained without reset');
  await shot('03-incompatible-version');
  await page.locator('#recover').click();await ready();await pause();
  check('compatible previous generation recovers incompatible current',JSON.stringify(await pixels())===JSON.stringify(expected));
  const beforeAbort=await fingerprints();
  await page.evaluate(()=>{window.__abortCheckpointWrites=true;});
  await page.locator('#save').click();
  await page.waitForFunction(()=>document.querySelector('#status').textContent.startsWith('Save failed:'),null,{timeout:120000});
  check('interrupted native storage transaction fails visibly and retains guest',await page.evaluate(()=>window.iphone.ready),{status:await page.locator('#status').textContent()});
  await sameStored(beforeAbort,'aborted save rolls back both current and previous records');
  check('aborted save retains exact live Settings screen',JSON.stringify(await pixels())===JSON.stringify(expected));
  await shot('04-aborted-save');
  await page.evaluate(()=>{window.__abortCheckpointWrites=false;});
  await page.locator('#save').click();
  await page.waitForFunction(old=>window.iphone.savedAt>old && !document.querySelector('#save').disabled,beforeAbort.current.savedAt,{timeout:120000});
  check('retry after storage abort commits a new checkpoint', (await fingerprints()).current.savedAt!==beforeAbort.current.savedAt);
  const beforeReadFailure=await fingerprints();
  await page.evaluate(()=>localStorage.setItem('__denyCheckpointReads','true'));
  await page.reload();await ready();await pause();
  check('storage access failure starts an unsaved guest with writes disabled',!await page.evaluate(()=>window.iphone.profileOwned) && await page.locator('#save').isDisabled() && await page.locator('#reset').isDisabled());
  await page.evaluate(()=>localStorage.removeItem('__denyCheckpointReads'));
  await sameStored(beforeReadFailure,'storage access failure retains both saved generations');
  await shot('05-storage-unavailable');
  await page.reload();await ready();await pause();
  check('restored storage access reloads the saved Settings phone',JSON.stringify(await pixels())===JSON.stringify(expected));
  check('restored storage access restores write ownership',await page.evaluate(()=>window.iphone.profileOwned));
  await shot('06-storage-restored');report.result='pass';
}catch(error){report.result='fail';report.error=error.stack;await shot('failure').catch(()=>{});throw error;}
finally{report.durationSeconds=(performance.now()-started)/1000;await writeFile(resolve(artifacts,'report.json'),JSON.stringify(report,null,2)+'\n');await context.close().catch(()=>{});await browser.close().catch(()=>{});await new Promise(resolve=>fixtureServer.close(resolve));console.log(`Report ${artifacts}/report.json`);}
