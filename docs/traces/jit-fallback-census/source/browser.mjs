import {chromium} from 'playwright';
import {RawCDP} from '../../docs/traces/browser-first-use/cdp.mjs';
import {cacheServer} from './cache-server.mjs';
import {encodeBytes,decodeFrame} from './frame-packet.mjs';
import {compareRaw} from './frames.mjs';
import {readFile,writeFile,mkdir,readdir,readlink} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import {gzipSync} from 'node:zlib';
import {resolve} from 'node:path';
import assert from 'node:assert/strict';
const base=resolve('.context/jit-fallback-census'),dir=base+'/browser-1',pins=JSON.parse(await readFile(base+'/browser-pins.json'));
await mkdir(dir,{recursive:true});
const sha=x=>createHash('sha256').update(x).digest('hex'),json=x=>JSON.stringify(x,(_,v)=>typeof v==='bigint'?v.toString():v,2);
const report={started:new Date().toISOString(),scope:'One instrumented warm Settings/Home step census; not a speed measurement',pins,actions:[],frames:[],errors:[],assets:[],processReceipts:[]};
assert.equal(JSON.parse(json({a:1n})).a,'1');
const persist=()=>writeFile(dir+'/report.json',json(report));
let server,context,page,client;const tasks=[];
async function processes(label){const info=(await client.call('SystemInfo.getProcessInfo')).processInfo,all=[];for(const name of await readdir('/proc'))if(/^\d+$/.test(name)){try{const raw=await readFile('/proc/'+name+'/stat','utf8'),f=raw.slice(raw.lastIndexOf(')')+2).split(' ');all.push({pid:+name,ppid:+f[1],startTicks:f[19],exe:await readlink('/proc/'+name+'/exe')});}catch{}}const scope=new Set(info.map(x=>x.id));let changed=true;while(changed){changed=false;for(const x of all)if(scope.has(x.ppid)&&!scope.has(x.pid)){scope.add(x.pid);changed=true;}}report.processReceipts.push({label,utc:new Date().toISOString(),info,processes:all.filter(x=>scope.has(x.pid))});await writeFile(dir+'/process-receipt.json',json(report.processReceipts));}
async function counters(operation='read'){return page.evaluate(op=>window.fcRequest(op),operation);}
async function frame(name,kind){await page.waitForTimeout(1000);const packet=await page.evaluate(async()=>{const c=document.querySelector('#screen'),ctx=c.getContext('2d'),a=ctx.getImageData(0,0,320,480).data;await new Promise(r=>setTimeout(r,50));const b=ctx.getImageData(0,0,320,480).data;const pack=async bytes=>({base64:window.packFrame(bytes),sha256:[...new Uint8Array(await crypto.subtle.digest('SHA-256',bytes))].map(v=>v.toString(16).padStart(2,'0')).join('')});return {a:await pack(a),b:await pack(b),png:c.toDataURL()};});const a=decodeFrame(packet.a),b=decodeFrame(packet.b),reference=await readFile(base+'/references/C0-'+kind+'.rgba'),entry={name,kind,stable:compareRaw(a,b),reference:compareRaw(reference,b),sha256:sha(b)};await writeFile(dir+'/'+name+'.rgba.gz',gzipSync(b));await writeFile(dir+'/'+name+'-before.rgba.gz',gzipSync(a));await writeFile(dir+'/'+name+'.png',Buffer.from(packet.png.split(',')[1],'base64'));await page.screenshot({path:dir+'/'+name+'-page.png'});report.frames.push(entry);await persist();assert(entry.stable.passed&&entry.reference.passed,'strict settled '+name);}
function parseRows(packet){const raw=Buffer.from(packet.table,'base64');assert.equal(raw.length,pins.capacity*40);const rows=[];const totals=Array(5).fill(0),provenance=Array(9).fill(0);let sum=0;for(let p=0;p<raw.length;p+=40){if(!raw.readUInt32LE(p+28))continue;const keys=['pc','opcode','flags','provenance','guard','admission','status'],r=Object.fromEntries(keys.map((k,i)=>[k,raw.readUInt32LE(p+i*4)]));r.count=Number(raw.readBigUInt64LE(p+32));rows.push(r);sum+=r.count;totals[r.admission]+=r.count;provenance[r.provenance]+=r.count;}assert.equal(sum,packet.census.calls);assert.equal(rows.length,packet.census.rows);assert.equal(provenance[8],0);return {rows,totals,provenance};}
try {
 for(const [path,hash]of Object.entries(pins.site))assert.equal(sha(await readFile(base+'/site/'+path)),hash,'site pin '+path);assert.equal(sha(await readFile(pins.fixture)),pins.fixtureSha256);for(const [name,hash]of Object.entries(pins.references))assert.equal(sha(await readFile(base+'/references/'+name)),hash,'reference pin '+name);
 const created=await cacheServer(base+'/site',{cache:'no-cache'});server=created.server;await new Promise(r=>server.listen(pins.port,'127.0.0.1',r));
 context=await chromium.launchPersistentContext(base+'/browser-profile-1',{executablePath:'/usr/bin/google-chrome',args:['--no-sandbox','--disable-dev-shm-usage','--remote-debugging-port='+pins.debugPort],viewport:{width:1280,height:1000},recordVideo:{dir:dir+'/video',size:{width:1280,height:1000}},serviceWorkers:'block'});
 await context.addInitScript({content:'window.packFrame='+encodeBytes.toString()});
 await context.addInitScript(()=>{const Original=window.Worker;window.Worker=class extends Original{constructor(url,options){super(url,options);if(String(url).includes('emu-worker.js'))window.fcWorker=this;}};let id=900000;window.fcRequest=op=>new Promise((resolve,reject)=>{const w=window.fcWorker,n=++id;const timer=setTimeout(()=>{w.removeEventListener('message',listener);reject(Error('census reply timeout'));},10000);function listener(e){if(e.data.id!==n)return;clearTimeout(timer);w.removeEventListener('message',listener);if(e.data.error)return reject(Error(e.data.error));const {buffer,...rest}=e.data;if(buffer)rest.table=window.packFrame(new Uint8Array(buffer));resolve(rest);}w.addEventListener('message',listener);w.postMessage({type:'fc',operation:op,id:n});});});
 page=context.pages()[0]||await context.newPage();page.on('pageerror',e=>report.errors.push(e.stack));context.on('response',r=>{if(/\/(emu|seed)\.wasm(?:\?|$)/.test(r.url()))tasks.push((async()=>{try{const bytes=await r.body();report.assets.push({url:r.url(),sha256:sha(bytes),length:bytes.length});}catch(e){report.errors.push('asset body '+e.message);}})());});
 const version=await(await fetch('http://127.0.0.1:'+pins.debugPort+'/json/version')).json();client=new RawCDP(version.webSocketDebuggerUrl);await client.ready;report.version=version;await processes('before-navigation');
 await page.goto(pins.origin);await page.waitForFunction(()=>window.iphone?.ready,null,{timeout:90000});report.startup=await counters();
 // Explicit diagnostic normal-import setup; hidden controls stay hidden.
 if(!await page.evaluate(()=>window.iphone.paused))await page.locator('#pause').evaluate(b=>b.click());await page.waitForFunction(()=>window.iphone.paused);
 await page.locator('#import').setInputFiles(resolve(pins.fixture));await page.waitForFunction(()=>/backup is restored|Import failed/.test(document.querySelector('#status').textContent),null,{timeout:90000});assert(!/Import failed/.test(await page.locator('#status').textContent()));await page.waitForFunction(()=>!document.querySelector('#pause').disabled);
 for(let n=0;n<100;n++){report.import=await counters();if(report.import.jit?.enabled&&report.import.jit.regions===pins.expectedPausedImportActive)break;await page.waitForTimeout(100);}assert.equal(report.import.jit.regions,pins.expectedPausedImportActive);assert(report.import.jit.enabled);await persist();
 await page.locator('#pause').evaluate(b=>b.click());await page.waitForFunction(()=>!window.iphone.paused);await page.waitForTimeout(1000);
 await page.evaluate(()=>{
  const ctx=document.querySelector('#screen').getContext('2d');function pixels(){const raw=ctx.getImageData(0,0,320,480).data,out=[];let white=0;for(let y=30;y<480;y+=4)for(let x=2;x<320;x+=4){const i=(y*320+x)*4;out.push(raw[i],raw[i+1],raw[i+2]);if(Math.min(raw[i],raw[i+1],raw[i+2])>235)white++;}return{out,white:white/(out.length/3)};}function diff(a,b){let n=0;for(let i=0;i<a.out.length;i+=3)if(Math.max(Math.abs(a.out[i]-b.out[i]),Math.abs(a.out[i+1]-b.out[i+1]),Math.abs(a.out[i+2]-b.out[i+2]))>25)n++;return n/(a.out.length/3);}const home=pixels();
  window.censusReady={begin(kind){const before=pixels(),start=performance.now();let previous=before,stable=start,ready=null;this.result=null;this.timer=setInterval(()=>{const now=performance.now(),frame=pixels(),changed=diff(before,frame),isReady=kind==='home'?diff(home,frame)<.04:frame.white>.25;if(diff(previous,frame)>.008||!isReady||changed<.1){stable=now;ready=null;}else if(ready===null)ready=stable;previous=frame;if(ready!==null&&now-ready>=500){clearInterval(this.timer);this.result={observedMs:now-start,readinessMs:ready-start};}else if(now-start>15000){clearInterval(this.timer);this.result={error:'15s readiness bound',changed,white:frame.white};}},50);}};
 });
 await frame('initial-home','home');
 for(const [round,kind]of [[0,'settings'],[0,'home'],[1,'settings'],[1,'home']]){
  const label=(round?'warm':'prepare')+'-'+kind,box=await page.locator('#screen').boundingBox(),point=kind==='settings'?{x:box.x+275/320*box.width,y:box.y+250/480*box.height}:await page.locator('[data-button="1"]').boundingBox().then(b=>({x:b.x+b.width/2,y:b.y+b.height/2}));await page.mouse.move(point.x,point.y);
  const before=await counters(round?'start':'read');await page.evaluate(k=>window.censusReady.begin(k),kind);await page.mouse.down();await page.waitForTimeout(30);await page.mouse.up();await page.waitForFunction(()=>window.censusReady.result,null,{timeout:16000});const readiness=await page.evaluate(()=>window.censusReady.result),after=await counters(round?'stop':'read');
  const {table,...afterMetadata}=after,action={label,before,after:afterMetadata,readiness};report.actions.push(action);if(table)await writeFile(dir+'/'+label+'-table.bin.gz',gzipSync(Buffer.from(table,'base64')));await persist();assert(!readiness.error,readiness.error);
  if(round){const parsed=parseRows(after);action.admission=parsed.totals;action.provenance=parsed.provenance;const c=after.census;assert.equal(c.overflow,0);assert.equal(c.mapOverflow,0);assert.equal(c.installs,0);assert.equal(c.epoch,before.census.epoch);assert.equal(c.calls,c.ok+c.nonok);assert.equal(c.calls,c.fetched+c.nofetch);assert.equal(c.ok+after.jit.retired-before.jit.retired,after.retired-before.retired);assert(after.jit.retired>before.jit.retired);await writeFile(dir+'/'+label+'-rows.json.gz',gzipSync(JSON.stringify(parsed.rows)));}
  await frame(label,kind);await persist();
 }
 report.final=await counters();assert(report.final.jit.retired>0);report.passed=report.errors.length===0;
}catch(e){report.errors.push(e.stack||e.message);report.passed=false;}
finally {
 await Promise.allSettled(tasks);for(const file of ['dist/emu.wasm','jit/seed.wasm']){const actual=report.assets.filter(a=>a.url===pins.origin+'/'+file);if(!actual.length||actual.some(a=>a.sha256!==pins.site[file])){report.errors.push('actual module identity '+file);report.passed=false;}}
 if(client)try{await processes('before-close');}catch(e){report.errors.push(e.message);report.passed=false;}
 if(client)client.close();if(context)await context.close();if(page?.video())report.video=await page.video().path();
 if(server){server.closeAllConnections();await new Promise(r=>server.close(r));}
 report.closed=new Date().toISOString();report.browserClosed=true;report.passed=report.passed&&report.errors.length===0;await persist();console.log(json({passed:report.passed,actions:report.actions.length,frames:report.frames.length,errors:report.errors}));
}
if(!report.passed)process.exitCode=1;
