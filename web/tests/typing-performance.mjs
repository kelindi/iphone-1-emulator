/** Real Notes keyboard comparison. Serve isolated hold variants separately.
 * BASELINE_URL=http://localhost:8080 CANDIDATE_URL=http://localhost:8082
 * ARTIFACT_DIR=.context/typing-performance ROUNDS=3 node web/tests/typing-performance.mjs
 * Discrete 30 ms clicks; observed character commitment and key popup are separate.
 * Optional HOLDS=20 INPUT_TEXT='codexhello<o' selects a focused correctness run.
 * A 400 ms minimum action observation is a test guard, not maximum typing speed.
 * Exact text requires human review of the final screenshots; this is a pixel sampler.
 */
import {chromium} from 'playwright';
import {mkdir,writeFile} from 'node:fs/promises';
import {resolve} from 'node:path';
import {createHash} from 'node:crypto';
const dir=resolve(process.env.ARTIFACT_DIR || '.context/typing-performance');
await mkdir(dir,{recursive:true});
const holds=(process.env.HOLDS ||'100,20').split(',').map(Number);
for(const hold of holds){const variable=hold===100?'BASELINE_URL':hold===20?'CANDIDATE_URL':null;if(!variable||!process.env[variable])throw Error('Supply explicit BASELINE_URL/CANDIDATE_URL for each selected isolated hold variant; current production must not be mislabeled as the historical baseline.');}
const rounds=Number(process.env.ROUNDS ||3);if(!Number.isInteger(rounds)||rounds<1)throw Error('ROUNDS must be a positive integer');
const browser=await chromium.launch({executablePath:process.env.CHROME_PATH || '/usr/bin/google-chrome',args:['--no-sandbox','--disable-dev-shm-usage']});
const variants={100:process.env.BASELINE_URL || 'http://localhost:8080',20:process.env.CANDIDATE_URL || 'http://localhost:8082'};
const inputText=process.env.INPUT_TEXT ||'qwertyuiop';
const keyMap={};for(const [row,x0,y]of [['qwertyuiop',16,295],['asdfghjkl',32,348],['zxcvbnm',64,400]])for(let i=0;i<row.length;i++)keyMap[row[i]]=[x0+i*32,y];keyMap['<']=[300,400];
const report={date:new Date().toISOString(),browser:browser.version(),pulseMs:30,rounds,method:'25ms in-page canvas sampling, dark glyph mask excluding one/two-column caret, then retrospective matching to settled glyph pixels; popup strip measured separately. Each action waits at least400ms and200ms after commitment; not maximum typing throughput. Fresh disposable context each run; alternating order.',runs:[],errors:[]};
function json(value){return JSON.stringify(value,(_,v)=>typeof v==='bigint'?v.toString():v,2);}
async function runVariant(hold,round){
 const runDir=resolve(dir,`${round}-${hold}`);await mkdir(runDir,{recursive:true});
 const context=await browser.newContext({viewport:{width:1280,height:1000},recordVideo:{dir:runDir,size:{width:1280,height:1000}}});
 const page=await context.newPage();
 const result={hold,round,url:variants[hold],actions:[],errors:[]};report.runs.push(result);
 page.on('pageerror',e=>result.errors.push(e.message));
 try{
  const wasm=await fetch(new URL('dist/emu.wasm',result.url));if(!wasm.ok)throw Error(`WASM fingerprint HTTP${wasm.status}`);result.wasmSha256=createHash('sha256').update(Buffer.from(await wasm.arrayBuffer())).digest('hex');
  if(report.runs.some(r=>r!==result&&r.hold!==hold&&r.wasmSha256===result.wasmSha256))throw Error('Both hold labels serve identical WASM; use distinct isolated builds.');
  await page.goto(result.url);
  await page.waitForFunction(()=>window.iphone?.ready,null,{timeout:120000});
  await page.waitForTimeout(1000);
  let box=await page.locator('#screen').boundingBox();
  async function tap(x,y){await page.mouse.move(box.x+x/320*box.width,box.y+y/480*box.height);await page.mouse.down();await page.waitForTimeout(30);await page.mouse.up();}
  await tap(200,250);
  // Original Notes keyboard: white keytops at y295 and yellow note body.
  await page.waitForFunction(()=>{const d=document.querySelector('#screen').getContext('2d').getImageData(0,0,320,480).data;let keys=0;for(const x of [16,48,80,112,144,176,208,240,272,304]){const i=(282*320+x)*4;if(Math.min(d[i],d[i+1],d[i+2])>180)keys++;}const i=(170*320+150)*4;return keys>=8 && d[i]>180&&d[i+1]>160&&d[i+2]<220;},null,{timeout:90000});
  await page.waitForTimeout(1500);
  await page.locator('#screen').screenshot({path:resolve(runDir,'blank.png')});
  await page.evaluate(()=>{
   const ctx=document.querySelector('#screen').getContext('2d');
   function sample(){
    const d=ctx.getImageData(0,0,320,480).data,body=new Uint8Array(290*42),strip=new Uint8Array(320*40*3);
    for(let y=98;y<140;y++)for(let x=25;x<315;x++){const i=(y*320+x)*4;body[(y-98)*290+x-25]=Math.max(d[i],d[i+1],d[i+2])<125?1:0;}
    // The insertion caret is a tall single column, unlike the rendered glyphs.
    const columns=new Uint16Array(290);for(let x=0;x<290;x++)for(let y=0;y<42;y++)columns[x]+=body[y*290+x];
    for(let x=1;x<289;x++){if(columns[x]<16)continue;let end=x;while(end+1<289&&columns[end+1]>=16)end++;if(end-x<2&&columns[x-1]<=3&&columns[end+1]<=3)for(let col=x;col<=end;col++)for(let y=0;y<42;y++)body[y*290+col]=0;x=end;}
    for(let y=225;y<265;y++)for(let x=0;x<320;x++){const i=(y*320+x)*4,k=((y-225)*320+x)*3;strip[k]=d[i];strip[k+1]=d[i+1];strip[k+2]=d[i+2];}
    return {body,strip};
   }
   function bodyDiff(a,b){let n=0;for(let i=0;i<a.length;i++)n+=a[i]!==b[i];return n;}
   function stripDiff(a,b){let n=0;for(let i=0;i<a.length;i+=3)if(Math.max(Math.abs(a[i]-b[i]),Math.abs(a[i+1]-b[i+1]),Math.abs(a[i+2]-b[i+2]))>30)n++;return n;}
   window.typingProbe={begin(deleting=false){
    const before=sample(),start=performance.now(),beforeInk=before.body.reduce((a,b)=>a+b,0),frames=[];
    let commit=null,popup=null,lastDiff=0,maxBodyDiff=0,previous=before.body;
    this.result=null;
    this.timer=setInterval(()=>{
     const now=performance.now(),frame=sample(),delta=bodyDiff(before.body,frame.body),pop=stripDiff(before.strip,frame.strip),ink=frame.body.reduce((a,b)=>a+b,0);
     const changed=bodyDiff(previous,frame.body)>2;previous=frame.body;
     frames.push({ms:now-start,body:frame.body,witness:changed&&(deleting?beforeInk-ink:ink-beforeInk)>=12?ctx.canvas.toDataURL('image/png'):null});
     maxBodyDiff=Math.max(maxBodyDiff,delta);
     if(popup===null&&pop>60)popup=now-start;
     if(commit===null&&delta>=12&&(deleting?beforeInk-ink:ink-beforeInk)>=12)commit=now-start;
     lastDiff=delta;
     if(commit!==null&&now-start>=Math.max(400,commit+200)&&pop<=30){
      clearInterval(this.timer);
      // Retrospective reference: require the actual settled glyph pixels,
      // so a transient cursor or incomplete glyph cannot count as commitment.
      const committed=frames.find(f=>bodyDiff(f.body,frame.body)<=2);
      this.result={commitMs:committed?.ms ??commit,popupMs:popup,observedMs:now-start,changedGlyphPixels:delta,addedGlyphPixels:ink-beforeInk,maxBodyDiff,commitWitness:committed?.witness ||ctx.canvas.toDataURL('image/png'),metrics:window.iphone.metrics};
     }else if(now-start>15000){
      clearInterval(this.timer);this.result={error:'Character/popup did not settle',commitMs:commit,popupMs:popup,lastDiff,maxBodyDiff,popupPixels:pop,metrics:window.iphone.metrics};
     }
    },25);
   },get(){return this.result;}};
  });
  const text=inputText;let began;let expected='';
  for(let i=0;i<text.length;i++){
   const [x,y]=keyMap[text[i]] ||[];if(x===undefined)throw Error(`Unsupported key${text[i]}`);
   await page.mouse.move(box.x+x/320*box.width,box.y+y/480*box.height);
   if(i===0)began=await page.evaluate(()=>performance.now());
   await page.evaluate(deleting=>window.typingProbe.begin(deleting),text[i]==='<');
   await page.mouse.down();await page.waitForTimeout(30);await page.mouse.up();
   await page.waitForFunction(()=>window.typingProbe.get(),null,{timeout:20000});
   const timing=await page.evaluate(()=>window.typingProbe.get());
   if(timing.commitWitness){const name=`${i}-${text[i]}-commit.png`;await writeFile(resolve(runDir,name),Buffer.from(timing.commitWitness.split(',')[1],'base64'));timing.commitWitness=name;}
   expected=text[i]==='<'?expected.slice(0,-1):expected+(expected?text[i]:text[i].toUpperCase());
   result.actions.push({key:text[i],expectedPrefix:expected,...timing});
   console.log(json({hold,round,key:text[i],commitMs:timing.commitMs,popupMs:timing.popupMs,error:timing.error}));
   if(timing.error)throw Error(timing.error);
  }
  result.totalObservedMs=await page.evaluate(t=>performance.now()-t,began);
  result.expectedText=expected;result.textVerification='Manual review of typed.png required; the sampler does not recognize letters.';
  await page.waitForTimeout(1500);
  await page.locator('#screen').screenshot({path:resolve(runDir,'typed.png')});
  result.passed=result.errors.length===0;
 }catch(e){result.errors.push(e.stack ||e.message);result.passed=false;await page.locator('#screen').screenshot({path:resolve(runDir,'failure.png')}).catch(()=>{});}
 finally{const video=page.video();await context.close();result.video=await video.path();await writeFile(resolve(runDir,'report.json'),json(result)+'\n');await writeFile(resolve(dir,'report.json'),json(report)+'\n');}
 return result.passed;
}
try{for(let round=0;round<report.rounds;round++){const order=round%2?[...holds].reverse():holds;for(const hold of order)if(!await runVariant(hold,round))throw Error(`Typing run failed: ${hold}, round${round}`);}}
catch(e){report.errors.push(e.stack ||e.message);}
finally{await browser.close();report.passed=report.errors.length===0&&report.runs.every(r=>r.passed);await writeFile(resolve(dir,'report.json'),json(report)+'\n');}
if(!report.passed)process.exitCode=1;
