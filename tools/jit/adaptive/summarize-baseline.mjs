import {readFile} from 'node:fs/promises';
import assert from 'node:assert/strict';
const file=process.argv[2];if(!file)throw Error('Usage: node summarize-baseline.mjs path/to/report.json');
const r=JSON.parse(await readFile(file));assert(r.passed,'Browser run failed');
assert.equal(r.preparation?.verified?.kind,'home','Home preparation not verified');
assert(r.servedAssets.every(a=>a.matches),'Served asset mismatch');
for(const name of ['index.html','emu-worker.js','dist/emu.wasm','jit/seed.wasm'])assert(r.servedAssets.some(a=>a.name===name&&a.matches&&/^[a-f0-9]{64}$/.test(a.expected||'')),'Missing frozen identity: '+name);
const summary={source:file,host:r.host,browser:r.browser,scope:'Current legacy engine after real unlock; one first pair and at least30warm samples/action. Cloud results are not Mac acceptance or an adaptive-compiler speedup.',actions:{}};
for(const kind of ['settings','home']){
 const all=r.actions.filter(a=>a.kind===kind);assert(all.every(a=>!a.error&&Number.isFinite(a.releaseToReadyMs)));
 const rounds=all.map(a=>a.round).sort((a,b)=>a-b);assert(rounds.every((round,i)=>Number.isInteger(round)&&round===i),'Rounds must be unique and contiguous from zero');
 assert.equal(all.length,r.actions.filter(a=>a.kind===(kind==='settings'?'home':'settings')).length,'Incomplete action pairs');
 const warm=all.filter(a=>a.round>0).map(a=>a.releaseToReadyMs).sort((a,b)=>a-b);assert(warm.length>=30,'At least30warm samples required');assert(warm.every(Number.isFinite));
 const mid=Math.floor(warm.length/2),median=warm.length%2?warm[mid]:(warm[mid-1]+warm[mid])/2,p95=warm[Math.ceil(warm.length*.95)-1];
 summary.actions[kind]={firstMs:all.find(a=>a.round===0)?.releaseToReadyMs,warmSamples:warm.length,medianMs:median,p95Ms:p95,warmTargetMs:1000,meetsWarmTarget:p95<=1000};
}
console.log(JSON.stringify(summary,null,2));
