import {chromium} from 'playwright';import {readFile,writeFile,mkdir} from 'node:fs/promises';import {createHash} from 'node:crypto';import {encodeBytes,decodeFrame} from './frame-packet.mjs';
const dir='.context/jit-return-visit-v3/transport-diagnostic';await mkdir(dir,{recursive:true});
const source=await readFile('.context/jit-return-visit/references/C0-settings.rgba'),expected=createHash('sha256').update(source).digest('hex');
const report={scope:'Immutable actual RGBA protocol diagnostic; no guest/app/firmware execution, no phone speed claim',inputSha256:expected,inputBytes:source.length,cases:[],passed:false};
const browser=await chromium.launch({executablePath:'/usr/bin/google-chrome',args:['--no-sandbox','--disable-dev-shm-usage']});
try{
 const page=await browser.newPage();await page.goto('about:blank');await page.addScriptTag({content:'window.encodeBytes='+encodeBytes.toString()});
 await page.evaluate(({text,hash})=>{const s=atob(text);window.frame=new Uint8Array(s.length);for(let i=0;i<s.length;i++)window.frame[i]=s.charCodeAt(i);window.frameHash=hash;},{text:source.toString('base64'),hash:expected});
 for(const mode of ['array','base64','base64','array']){
  const start=performance.now();const got=await page.evaluate(mode=>{const begin=performance.now(),a=window.frame.slice(),b=window.frame.slice(),captureEnd=performance.now();if(mode==='array'){const out={a:Array.from(a),b:Array.from(b)},end=performance.now();return {...out,timings:{copyMs:captureEnd-begin,encodingMs:end-captureEnd,inPageMs:end-begin}};}const out={a:{base64:window.encodeBytes(a),sha256:window.frameHash},b:{base64:window.encodeBytes(b),sha256:window.frameHash}},end=performance.now();return {...out,timings:{copyMs:captureEnd-begin,encodingMs:end-captureEnd,inPageMs:end-begin}};},mode);
  const returned=performance.now();const a=mode==='array'?Buffer.from(got.a):decodeFrame(got.a),b=mode==='array'?Buffer.from(got.b):decodeFrame(got.b);const decoded=performance.now();if(!a.equals(source)||!b.equals(source))throw Error('Byte mismatch');
  report.cases.push({mode,evalWallMs:returned-start,decodeMs:decoded-returned,outsideInPageBodyMs:returned-start-got.timings.inPageMs,...got.timings,exact:true});await writeFile(dir+'/report.json',JSON.stringify(report,null,2));
 }
 report.passed=true;
}catch(e){report.error=e.stack;throw e;}finally{await browser.close();report.browserClosed=true;await writeFile(dir+'/report.json',JSON.stringify(report,null,2));}
console.log(JSON.stringify(report,null,2));
