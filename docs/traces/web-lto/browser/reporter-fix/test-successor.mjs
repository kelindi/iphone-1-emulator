import assert from 'node:assert/strict';import {readFile,writeFile} from 'node:fs/promises';import{validateAssets}from'./validate-assets.mjs';
const b='.context/web-lto/browser',old=await readFile(b+'/run-browser.mjs','utf8'),next=await readFile(b+'/run-browser-successor.mjs','utf8'),prefix="import {validateAssets} from './reporter-fix/validate-assets.mjs';\n";
assert.equal(next.slice(prefix.length,next.indexOf('\nfinally{')),old.slice(0,old.indexOf('\nfinally{')),'All guest/input/timing/trace collection before finally is byte-identical');
const body=next.slice(next.indexOf('\nfinally{')+9,next.indexOf('\n}\nif(!report.passed)'));
const AsyncFunction=Object.getPrototypeOf(async function(){}).constructor;
const fn=new AsyncFunction('tracing','stopTrace','assetTasks','report','site','origin','validateAssets','processReceipt','client','context','server','page','writeFile','resolve','dir','json','console',body);
const original=JSON.parse(await readFile(b+'/measured/1-original/report-partial.json')),site=JSON.parse(await readFile(b+'/site-pins.json')).sites.original;const rows=[];
for(const kind of ['success','malformed-url','receipt-error','context-error','server-error','video-error']){
 const report={...original,assets:structuredClone(original.assets),errors:[],passed:true};if(kind==='malformed-url')report.assets[0].url='http://[';
 const calls=[];let saved;
 await fn(false,async()=>{},[],report,site,original.origin,validateAssets,async()=>{calls.push('receipt');if(kind==='receipt-error')throw Error('injected receipt');},{close(){calls.push('client');}},{async close(){calls.push('context');if(kind==='context-error')throw Error('injected context');}},{closeAllConnections(){calls.push('connections');},close(cb){calls.push('server');cb(kind==='server-error'?Error('injected server'):undefined);}},{video(){return{async path(){calls.push('video');if(kind==='video-error')throw Error('injected video');return'actual.webm';}};}},async(path,data)=>{calls.push('write');saved=JSON.parse(data);},(...p)=>p.join('/'),'artifacts',JSON.stringify,{log(){}});
 assert.deepEqual(calls,['receipt','client','context','connections','server','video','write']);assert.equal(saved.passed,kind==='success');assert.equal(saved.errors.length,kind==='success'?0:kind==='malformed-url'?2:1);rows.push({kind,passed:true,reportPassed:saved.passed});
}
await writeFile(b+'/reporter-fix/successor-tests.json',JSON.stringify({passed:true,checks:7,guestCollectorPrefixByteIdentical:true,rows},null,2)+'\n');console.log('PASS 7 source projection/finalizer scenarios');
