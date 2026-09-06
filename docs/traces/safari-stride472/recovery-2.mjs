// One frozen failed-state Safari rendering diagnostic; no HTTP acceptance.
import {finalAcceptance,validateManualText,requireOpenInput} from './recovery-finalize.mjs';
import {chromium} from 'playwright';
import {mkdir,writeFile,readFile,stat,statfs} from 'node:fs/promises';
import {createReadStream} from 'node:fs';
import {createServer} from 'node:http';
import {createInterface} from 'node:readline';
import {resolve,extname} from 'node:path';
import {createHash} from 'node:crypto';
import {gunzipSync} from 'node:zlib';
const base=resolve('.context/safari-default-mbx'),dir=resolve(base,'recovery-run-2'),site=resolve(base,'web');
await mkdir(dir);const start=Date.now(),report={passed:false,capSeconds:360,startedUTC:new Date().toISOString(),actions:[],checks:[],errors:[],requests:[],responses:[],raw:[],serial:[],requestFailures:[],scope:'Failed Safari state: fresh glyph/rendering diagnostic only. Daemon absent; no form/HTTP acceptance, Photos, speed or production claim.'};
const json=v=>JSON.stringify(v,(_,x)=>typeof x==='bigint'?x.toString():x,2)+'\n';
const sha=b=>createHash('sha256').update(b).digest('hex');
const wait=ms=>new Promise(r=>setTimeout(r,ms));
const persist=async()=>{report.graphicsSerial={restart:(report.serial.map(x=>x.text).join('').match(/Graphics Restart/g)||[]).length,hang:(report.serial.map(x=>x.text).join('').match(/Graphics hang/g)||[]).length};await writeFile(resolve(dir,'report.json'),json(report));await writeFile(resolve(dir,'serial.log'),report.serial.map(x=>x.text).join(''));};
const mime={'.js':'text/javascript','.wasm':'application/wasm','.json':'application/json','.html':'text/html','.css':'text/css','.svg':'image/svg+xml'};
const server=createServer(async(req,res)=>{try{const u=new URL(req.url,'http://localhost:8228');const p=resolve(site,'.'+decodeURIComponent(u.pathname==='/'?'/index.html':u.pathname));if(!p.startsWith(site+'/'))throw Error('path');const s=await stat(p);if(!s.isFile())throw Error('file');res.writeHead(200,{'Content-Type':mime[extname(p)]||'application/octet-stream','Content-Length':s.size,'Cache-Control':'no-cache'});createReadStream(p).pipe(res);}catch{res.writeHead(404);res.end();}});
await new Promise((ok,no)=>{server.once('error',no);server.listen(8228,'127.0.0.1',ok);});
let browser,context,page,closing=false,video,timer;const assetTasks=[];
const input=createInterface({input:process.stdin,terminal:false});let inputEnded=false;input.once('close',()=>{inputEnded=true;});
async function state(){return page.evaluate(()=>({ready:window.iphone?.ready,paused:window.iphone?.paused,savedAt:window.iphone?.savedAt,metrics:window.iphone?.metrics,status:document.querySelector('#status')?.textContent}));}
async function finish(error){if(closing)return;closing=true;clearTimeout(timer);input.close();if(error)report.errors.push(String(error.stack||error));
 try{if(page&&!page.isClosed()){report.finalState=await state();await shot('final');video=page.video();}}catch(e){report.errors.push('final observation: '+e.message);}
 try{let deadline;try{await Promise.race([Promise.all(assetTasks),new Promise((_,reject)=>{deadline=setTimeout(()=>reject(Error('20s asset-finalization bound')),20000);})]);}finally{clearTimeout(deadline);}for(const row of report.responses){const u=new URL(row.url);if(u.origin!=='http://127.0.0.1:8228'||row.status!==200)continue;const n=u.pathname==='/'?'index.html':decodeURIComponent(u.pathname.slice(1));const pin=report.projection[n];if(!pin||row.sha256!==pin.sha256)throw Error('actual asset hash: '+n);}for(const name of ['index.html','phone.css','app.js','assets.js','storage.js','checkpoint-worker.js','emu-worker.js','seed-jit.js','manifest.json','dist/emu.js','dist/emu.wasm','jit/abi.json','jit/seed.wasm'])if(!report.responses.some(row=>row.status===200&&new URL(row.url).origin==='http://127.0.0.1:8228'&&(new URL(row.url).pathname==='/'?'index.html':decodeURIComponent(new URL(row.url).pathname.slice(1)))===name&&row.sha256===report.projection[name].sha256))throw Error('missing actual local asset: '+name);report.assetsExact=true;}catch(e){report.errors.push('asset validation: '+e.message);}
 try{if(page&&!page.isClosed()){const reserve=await statfs(dir);if(Number(reserve.bavail)*Number(reserve.bsize)<4300000000)throw Error('4.3GB final export reserve');await page.waitForFunction(()=>{const b=document.querySelector('#export');return b&&!b.disabled;},null,{timeout:15000});const event=page.waitForEvent('download',{timeout:75000});await page.locator('#export').evaluate(b=>b.click());const download=await event,target=resolve(dir,'final-render-diagnostic.snap');await download.saveAs(target);if(await download.failure())throw Error('download failed');const bytes=await readFile(target);report.finalExport={path:target,bytes:bytes.length,sha256:sha(bytes),scope:'failed-state rendering continuation only'};}}catch(e){report.errors.push('final export: '+e.message);}
 try{await context?.close();await browser?.close();report.browserClosed=true;if(video)report.video=await video.path();}catch(e){report.errors.push('browser close: '+e.message);}
 try{for(const[n,pin]of Object.entries(report.projection||{}))if(sha(await readFile(resolve(site,n)))!==pin.sha256)throw Error('source changed: '+n);report.projectionUnchanged=true;}catch(e){report.errors.push(e.message);}
 try{server.closeAllConnections();await new Promise(r=>server.close(r));report.serverClosed=true;}catch(e){report.errors.push('server close: '+e.message);}
 report.elapsedSeconds=(Date.now()-start)/1000;await persist();finalAcceptance(report);await persist();console.log('END '+json({passed:report.passed,errors:report.errors,report:resolve(dir,'report.json')}));process.exitCode=report.passed?0:1;
}
async function ready(){await page.waitForFunction(()=>window.iphone?.ready,null,{timeout:120000});await page.waitForFunction(()=>Number(document.querySelector('#screen').dataset.frame)>0);}
async function jit(name){await page.waitForFunction(()=>{const j=window.iphone?.metrics?.jit;return j?.enabled&&j.calls>0&&j.retired>0&&!window.iphone.paused;},null,{timeout:30000});report.checks.push({name,passed:true,jit:(await state()).metrics.jit});}
async function raw(name){const a=await page.evaluate(()=>{const c=document.querySelector('#screen');return{width:c.width,height:c.height,data:Array.from(c.getContext('2d').getImageData(0,0,c.width,c.height).data)};});if(a.width!==320||a.height!==480)throw Error('raw frame geometry');const bytes=Buffer.from(a.data);await writeFile(resolve(dir,name+'.rgba'),bytes);report.raw.push({name,bytes:bytes.length,sha256:sha(bytes),width:a.width,height:a.height});return bytes;}
async function shot(name){if(!/^[a-zA-Z0-9_-]+$/.test(name))throw Error('shot name');await page.screenshot({path:resolve(dir,name+'.png')});await page.locator('#screen').screenshot({path:resolve(dir,name+'-guest.png')});await raw(name);report.lastState=await state();await persist();console.log('SHOT '+name);}
async function tap(x,y){if(!Number.isFinite(x)||!Number.isFinite(y)||x<0||x>=320||y<0||y>=480)throw Error('touch bounds');const b=await page.locator('#screen').boundingBox();await page.mouse.move(b.x+x*b.width/320,b.y+y*b.height/480);await page.mouse.down();await wait(180);await page.mouse.up();}
async function act(a){await persist();if(report.graphicsSerial.restart||report.graphicsSerial.hang)throw Error('new serial graphics failure before action');if(a.tap){report.taps=(report.taps||0)+1;if(report.taps>13)throw Error('13-touch cap');if(a.tap[1]>435)throw Error('No Go/submit row touches');}if(a.home){report.homes=(report.homes||0)+1;if(report.homes>2)throw Error('two-Home cap');}if(a.wait>30000)throw Error('30s observation cap');if(a.autosaveReload)throw Error('no reload admission');
 report.actions.push({...a,videoSeconds:(Date.now()-start)/1000});
 if(a.tap)await tap(...a.tap);
 if(a.home){const b=await page.locator('[data-button="1"]').boundingBox();await page.mouse.move(b.x+b.width/2,b.y+b.height/2);await page.mouse.down();await wait(180);await page.mouse.up();}
 if(a.wait)await wait(Math.min(a.wait,60000));
 if(a.observe){validateManualText(a);if(!/^[a-zA-Z0-9_-]+\.png$/.test(a.frame??''))throw Error('reviewed frame required');const bytes=await readFile(resolve(dir,a.frame));report.checks.push({name:a.observe,intendedText:a.intendedText??null,actualText:a.actualText??null,passed:true,scope:'Manual review of retained actual original framebuffer',frame:a.frame,frameSHA256:sha(bytes)});}
 if(a.exportState){
  if(!/^[a-zA-Z0-9_-]+$/.test(a.exportState))throw Error('export label');
  const fs=await statfs(dir);if(Number(fs.bavail)*Number(fs.bsize)<4300000000)throw Error('export reserve');
  await page.waitForFunction(()=>{const b=document.querySelector('#export');return b&&!b.disabled;},null,{timeout:60000});
  const event=page.waitForEvent('download',{timeout:90000});
  await page.locator('#export').evaluate(button=>{if(button.disabled)throw Error('Export disabled');button.click();});
  const download=await event,target=resolve(dir,a.exportState+'.snap');await download.saveAs(target);
  if(await download.failure())throw Error('Export failed');
  const bytes=await readFile(target);report.exports??=[];report.exports.push({label:a.exportState,path:target,bytes:bytes.length,sha256:sha(bytes),scope:'Existing hidden diagnostic Export; not persistence acceptance'});
 }
 if(a.compareHome){const first=await readFile(resolve(dir,'home-first.rgba')),last=await readFile(resolve(dir,'home-second.rgba'));let max=0,n=0;for(let y=30;y<480;y++)for(let x=0;x<320;x++)for(let c=0;c<3;c++){const i=(y*320+x)*4+c;const d=Math.abs(first[i]-last[i]);max=Math.max(max,d);n+=d>2;}report.homeComparison={crop:[0,30,320,450],baselineSHA256:sha(first),comparisonSHA256:sha(last),pixels:144000,maximumChannelDelta:max,channelsAbove2:n};if(n)throw Error('Home repeat differs');report.checks.push({name:'exact-home-repeat',passed:true,...report.homeComparison});}
 if(a.shot)await shot(a.shot);await persist();console.log('READY');
}
try{
 timer=setTimeout(()=>finish(Error('360-second single-workflow cap')),360000);
 const fs=await statfs(dir);report.initialAvailableBytes=Number(fs.bavail)*Number(fs.bsize);if(report.initialAvailableBytes<5000000000)throw Error('5GB initial reserve');
 const fixturePath=resolve('.context/guest-http-default/browser-run-1/final-home.snap.gz');const packed=await readFile(fixturePath),unpacked=gunzipSync(packed,{maxOutputLength:300000000});if(unpacked.length!==260767808||sha(unpacked)!=='c407b3734919c76dc0eae153b1bb70366969c4548e3fa2232716a1bc3395e693')throw Error('failed-state fixture mismatch');report.fixture={path:fixturePath,rawBytes:unpacked.length,rawSHA256:sha(unpacked),gzipSHA256:sha(packed),scope:'failed form, not Home; daemon absent'};
 report.projection=JSON.parse(await readFile(resolve(base,'site-pins.json'),'utf8'));for(const[n,p]of Object.entries(report.projection)){const b=await readFile(resolve(site,n));if(b.length!==p.bytes||sha(b)!==p.sha256)throw Error('prelaunch source mismatch: '+n);}
 requireOpenInput(inputEnded);if(!process.stdin.isTTY)throw Error('TTY command input is required before browser launch');browser=await chromium.launch({executablePath:'/usr/bin/google-chrome',args:['--no-sandbox','--disable-dev-shm-usage']});context=await browser.newContext({viewport:{width:1280,height:1000},recordVideo:{dir,size:{width:1280,height:1000}}});
 await context.exposeBinding('__retainGuestSerial',(_,text)=>{if(typeof text==='string')report.serial.push({seconds:(Date.now()-start)/1000,text});});
 await context.addInitScript(()=>{const Native=window.Worker;window.Worker=class extends Native{constructor(...args){super(...args);this.addEventListener('message',event=>{if(event.data?.serial)window.__retainGuestSerial(event.data.serial);if(event.data?.type==='log')window.__retainGuestSerial(event.data.message+'\n');});}};});
 context.on('requestfailed',r=>report.requestFailures.push({url:r.url(),error:r.failure(),seconds:(Date.now()-start)/1000}));
 context.on('request',r=>report.requests.push({url:r.url(),method:r.method()}));context.on('response',r=>{const row={url:r.url(),status:r.status(),fromSW:r.fromServiceWorker()};report.responses.push(row);if(row.url.startsWith('http://127.0.0.1:8228/')&&row.status===200)assetTasks.push(r.body().then(b=>row.sha256=sha(b)).catch(e=>row.error=e.message));});
 page=await context.newPage();page.setDefaultTimeout(15000);page.on('pageerror',e=>report.errors.push(e.message));
 await page.goto('http://127.0.0.1:8228',{waitUntil:'domcontentloaded'});await ready();await jit('fresh-generated-execution');await shot('00-default-home');
 await page.waitForFunction(()=>!document.querySelector('#import').disabled,null,{timeout:60000});await page.locator('#import').setInputFiles(fixturePath);await page.waitForFunction(()=>/backup is restored|Import failed/.test(document.querySelector('#status').textContent),null,{timeout:90000});if(/Import failed/.test((await state()).status))throw Error('Import failed');await jit('failed-fixture-generated-execution');await wait(10000);await shot('01-failed-state-restored');console.log('READY');
 requireOpenInput(inputEnded);for await(const line of input){if(closing)break;if(!line.trim())continue;if(line.trim()==='finish'){for(const name of ['home-first','safari-open','safari-reopened','address-keyboard','typed-glyphs','home-second','exact-home-repeat'] )if(!report.checks.some(c=>c.name===name&&c.passed))throw Error('unfinished gate: '+name);report.finished=true;break;}await act(JSON.parse(line));if(report.graphicsSerial.restart||report.graphicsSerial.hang)throw Error('new guest graphics failure; stop');const f=await statfs(dir);if(Number(f.bavail)*Number(f.bsize)<4300000000)throw Error('4.3GB live reserve');}
 if(!closing){await jit('final-generated-execution');await finish();}
}catch(e){await finish(e);}
