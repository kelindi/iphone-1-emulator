/** Actual two-origin browser test. Firmware bytes are local fixtures, unchanged
 * and hash-verified by the real loader. Runtime code is not mocked. No uploads. */
import {createServer} from 'node:http';
import {readFile, mkdir, mkdtemp, writeFile, realpath, stat, cp, rm} from 'node:fs/promises';
import {createReadStream} from 'node:fs';
import {resolve, join, extname, sep, basename} from 'node:path';
import {fileURLToPath} from 'node:url';
import {chromium} from 'playwright';
import assert from 'node:assert/strict';
import {buildCdn} from '../build-cdn.mjs';
const repo = resolve(fileURLToPath(new URL('../../',import.meta.url)));
const dir = resolve(process.env.ARTIFACT_DIR || '.context/cdn-browser');
await mkdir(dir,{recursive:true});
const temp = await mkdtemp(join(dir,'run-'));
const requests=[],errors=[],report={checks:[],startedAt:new Date().toISOString()};
const types={'.html':'text/html','.js':'text/javascript','.css':'text/css','.svg':'image/svg+xml',
  '.png':'image/png','.json':'application/json','.wasm':'application/wasm','.txt':'text/plain','.xml':'application/xml'};
function server(root,role) {
  return createServer(async(req,res)=>{
    const pathname=new URL(req.url,'http://localhost').pathname;
    requests.push({role,path:pathname});
    try{
      const file=await realpath(resolve(root,`.${pathname.endsWith('/')?pathname+'index.html':pathname}`));
      if(!file.startsWith(root+sep)){res.writeHead(403);res.end();return;}
      const info=await stat(file);if(!info.isFile())throw Error('not a file');
      res.writeHead(200,{'Content-Type':types[extname(file)]||'application/octet-stream',
        'Content-Length':info.size,'Access-Control-Allow-Origin':'*',
        'Cross-Origin-Resource-Policy':'cross-origin','X-Content-Type-Options':'nosniff',
        'Cache-Control':role==='cdn'?'public, max-age=31536000, immutable':'no-cache'});
      createReadStream(file).pipe(res);
    }catch{res.writeHead(404);res.end('Not found');}
  });
}
const listen=s=>new Promise(r=>s.listen(0,'127.0.0.1',r));
const close=s=>new Promise(r=>{s.close(r);s.closeAllConnections();});
const site=server(join(temp,'bundle','site'),'site'),cdn=server(join(temp,'bundle','assets'),'cdn');
await listen(cdn);await listen(site);
const cdnBase=`http://127.0.0.1:${cdn.address().port}/`,base=`http://127.0.0.1:${site.address().port}/`;
let browser,context,offline=false;
const check=(name,result=true)=>{assert(result,name);report.checks.push(name);console.log('PASS',name);};
try{
  const release=await buildCdn({assetBase:cdnBase,out:join(temp,'bundle')});
  // Build a distinct immutable release without modifying the checked-in web
  // source. The marker lets this test prove which release actually booted.
  const secondSource=join(temp,'second-source'),secondBundle=join(temp,'second-bundle');
  await cp(join(repo,'web'),secondSource,{recursive:true});
  const secondApp=join(secondSource,'app.js');
  await writeFile(secondApp,Buffer.concat([await readFile(secondApp),Buffer.from("\ndocument.documentElement.dataset.cdnRelease = 'B';\n")]));
  const secondRelease=await buildCdn({assetBase:cdnBase,out:secondBundle,source:secondSource});
  check('second release has a distinct immutable content ID',secondRelease.version!==release.version);
  // CDN publication is additive: both immutable release directories are
  // available before the application origin begins serving release B.
  await cp(join(secondBundle,'assets'),join(temp,'bundle','assets'),{recursive:true});
  report.release=release.version;report.secondRelease=secondRelease.version;report.site=base;report.assets=release.remoteBase;
  if(process.argv.includes('--serve')) {
    console.log(JSON.stringify({site:base,assets:release.remoteBase,bundle:join(temp,'bundle')},null,2));
    console.log('Manual browser test servers are running; Ctrl-C stops them.');
    await new Promise(()=>{});
  }
  browser=await chromium.launch({...(process.env.CHROME_PATH?{executablePath:process.env.CHROME_PATH}:{}),args:['--disable-dev-shm-usage']});
  context=await browser.newContext({viewport:{width:1200,height:900}});
  await context.route('https://iphone.files.garden/**',async route=>{
    if(offline){await route.abort('internetdisconnected');return;}
    const name=basename(new URL(route.request().url()).pathname);
    await route.fulfill({body:await readFile(join(repo,'web','firmware',name)),
      contentType:'application/octet-stream',headers:{'Access-Control-Allow-Origin':'*'}});
  });
  const page=await context.newPage();
  page.on('pageerror',e=>errors.push(e.message));
  await page.goto(base);
  await page.waitForFunction(()=>window.iphone?.ready,null,{timeout:120000});
  check('genuine emulator boots with code served from a second origin');
  for(const name of ['app.js','storage.js','assets.js','emu-worker.js','dist/emu.js','dist/emu.wasm','jit/seed.wasm','jit/regions.json','jit/abi.json'])
    check(`${name} fetched from CDN`,requests.some(r=>r.role==='cdn'&&r.path.endsWith('/'+name)));
  check('runtime files absent from application-origin requests',!requests.some(r=>r.role==='site'&& /\/(app|storage|assets|seed-jit)\.js$|\/(dist|jit)\//.test(r.path)));
  await page.waitForFunction(()=>window.iphone.metrics.jit?.enabled,null,{timeout:30000});
  check('JIT acceleration enabled');
  // Actual compression worker + IndexedDB storage, with byte-for-byte round trip.
  const saved=await page.evaluate(async storageURL=>{
    const s=await import(storageURL),data=Uint8Array.from({length:1024*1024},(_,i)=>i%251),expected=data.slice();
    await s.writeOwnedCheckpoint(data.buffer);
    const transferred=data.byteLength===0;
    const record=await s.readCheckpoint(),roundtrip=new Uint8Array(await s.checkpointBytes(record));
    const roundTrips=expected.length===roundtrip.length&&roundtrip.every((v,i)=>v===expected[i]);
    // Test codec fixtures must never become a guest save on reload.
    await s.clearCheckpoints();return {transferred,roundTrips,encoding:record.encoding};
  },new URL('storage.js',release.remoteBase).href);
  check('same-origin compression worker transfers ownership and round-trips bytes through IndexedDB',
    saved.transferred&&saved.roundTrips&&saved.encoding==='gzip');
  check('compression implementation downloaded from CDN',requests.some(r=>r.role==='cdn'&&r.path.endsWith('/checkpoint-worker.js')));
  await page.locator('#offline').evaluate(button=>button.click());
  await page.waitForFunction(()=>document.querySelector('#status').textContent.includes('available offline'),null,{timeout:120000});
  await page.waitForFunction(()=>navigator.serviceWorker.controller);
  check('hash-verified split bundle installs for offline use');
  await page.screenshot({path:join(dir,'online.png')});
  check('initial offline install boots release A',await page.evaluate(()=>document.documentElement.dataset.cdnRelease!=='B'));
  // Promote only the site shell after B's immutable assets exist. Reloading an
  // A-controlled tab must install B as waiting rather than mixing releases.
  await rm(join(temp,'bundle','site'),{recursive:true,force:true});
  await cp(join(secondBundle,'site'),join(temp,'bundle','site'),{recursive:true});
  await page.reload();await page.waitForFunction(()=>window.iphone?.ready,null,{timeout:120000});
  await page.evaluate(async()=>{await (await navigator.serviceWorker.getRegistration()).update();});
  await page.waitForFunction(async()=>Boolean((await navigator.serviceWorker.getRegistration())?.waiting),null,{timeout:120000});
  check('release B waits while a release A client remains open',
    await page.evaluate(()=>document.documentElement.dataset.cdnRelease!=='B'));
  await page.close();
  // Normal activation has no skipWaiting path: it occurs only after the last A
  // client closes. A fresh page must then use B and remain independently offline.
  await new Promise(resolve=>setTimeout(resolve,1000));
  const updated=await context.newPage();
  updated.on('pageerror',e=>errors.push(e.message));
  await updated.goto(base);await updated.waitForFunction(()=>window.iphone?.ready,null,{timeout:120000});
  check('release B activates after all release A clients close',
    await updated.evaluate(()=>document.documentElement.dataset.cdnRelease==='B'));
  check('release B controls the reopened page',await updated.evaluate(()=>navigator.serviceWorker.controller?.scriptURL.includes('service-worker.js')));
  const before=requests.length;offline=true;await context.setOffline(true);
  await updated.reload();await updated.waitForFunction(()=>window.iphone?.ready,null,{timeout:120000});
  check('release B boots while both network origins are disconnected',
    await updated.evaluate(()=>document.documentElement.dataset.cdnRelease==='B'));
  check('offline release B reload makes no HTTP requests to either origin',requests.length===before);
  await updated.screenshot({path:join(dir,'offline.png')});
  check('no uncaught page errors',errors.length===0);
  report.result='pass';
}catch(error){report.result='fail';report.error=error.stack;console.error(error);process.exitCode=1;}
finally{
  report.requests=requests;report.errors=errors;
  await context?.close();await browser?.close();await close(site);await close(cdn);
  await writeFile(join(dir,'report.json'),JSON.stringify(report,null,2)+'\n');
  console.log(`Report: ${join(dir,'report.json')}`);
}
