import fs from'node:fs/promises';import assert from'node:assert/strict';import{createHash}from'node:crypto';import{validateAssets,expectedStartupUnavailable,responseMime}from'./validate-assets.mjs';
const original=JSON.parse(await fs.readFile('.context/mbx-stride-integration/run-1/report.json'));const variants=original.variants,origin='http://localhost:8235/';const sha=b=>createHash('sha256').update(b).digest('hex'),cases=[];function test(name,f){f();cases.push(name);}
// Real retained received bytes; headers below model the separately proposed UTF8 server, not the old run.
const valid=original.servedAssets.filter(r=>!r.error&&Object.values(variants).some(v=>v.hashes[r.path]===r.sha256)).map(r=>({...r,headers:{'content-type':responseMime(r.path.endsWith('.wasm')?'application/wasm':'text/plain')}}));
test('completed exact receipts with proposed truthful text headers',()=>assert(validateAssets(valid,variants,origin).passed));
const transformed=original.servedAssets.find(r=>r.path==='phone.css'&&r.sha256!==variants.A.hashes['phone.css']);
test('actual CP1252 transformed CSS remains rejected',()=>assert.throws(()=>validateAssets([...valid,{...transformed,headers:{'content-type':'text/css; charset=utf-8'}}],variants,origin)),/Unexpected served/);
test('missing explicit UTF8 rejected',()=>assert.throws(()=>validateAssets(valid.map(r=>({...r,headers:{'content-type':'text/css'}})),variants,origin)),/UTF8/);
test('truthful text and unchanged binary MIME',()=>{assert.equal(responseMime('text/css'),'text/css; charset=utf-8');assert.equal(responseMime('application/wasm'),'application/wasm');assert.equal(responseMime('application/json'),'application/json; charset=utf-8');});
const actual=original.servedAssets.find(r=>r.error),row={...actual,pageId:3,at:20,errorAt:40};
const proof={phase:'corrupt-kernel-offline',pageId:3,started:10,ended:50,corruption:{kernelURL:'https://iphone.files.garden/kernel_1A543a.macho',sha256:sha('corrupted test entry')},observed:{ready:false,retryEnabled:true,kernelCacheRemoved:true,kernelURL:'https://iphone.files.garden/kernel_1A543a.macho'},kernelRequestDenied:true,physicalBarrier:{listening:false,refusal:{code:'ECONNREFUSED'}},workerClosures:[{pageId:3,url:origin+'emu-worker.js',created:12,closed:30,createdPhase:'corrupt-kernel-offline',closedPhase:'corrupt-kernel-offline'}]};
test('real retained unavailable error is not proof without semantic evidence',()=>assert(!expectedStartupUnavailable(row,null)));
test('mock full kernel-abort lifecycle classifies unavailable separately',()=>{const r=validateAssets([...valid,row],variants,origin,{negativeStartup:proof});assert.equal(r.unavailableNegative.length,1);assert.equal(r.unavailableNegative[0].byteProof,false);});
for(const[name,mutate]of [
 ['positive phase',r=>r.phase='activate-B'],['wrong page',r=>r.pageId=4],['before negative',r=>r.at=9],['after negative',r=>r.at=51],['unknown error',r=>r.error='arbitrary transport failure'],['body error before worker closed',r=>r.errorAt=29]
])test('reject '+name,()=>{const x={...row};mutate(x);assert(!expectedStartupUnavailable(x,proof));});
for(const[name,mutate]of [
 ['guest still ready',p=>p.observed.ready=true],['kernel not removed',p=>p.observed.kernelCacheRemoved=false],['no kernel denial',p=>p.kernelRequestDenied=false],['no physical barrier',p=>p.physicalBarrier.listening=true],['wrong corruption',p=>p.corruption.sha256='0'.repeat(64)],['no worker closure',p=>p.workerClosures=[]],['unrelated worker',p=>p.workerClosures[0].url=origin+'checkpoint-worker.js'],['old generation worker',p=>p.workerClosures[0].createdPhase='activate-B']
])test('reject '+name,()=>{const p=structuredClone(proof);mutate(p);assert(!expectedStartupUnavailable(row,p));});
test('unavailable rows cannot replace required positive byte receipts',()=>assert.throws(()=>validateAssets([row],variants,origin,{negativeStartup:proof})));
test('wrong completed bytes during negative still fail',()=>assert.throws(()=>validateAssets([...valid,{...row,error:undefined,sha256:'0'.repeat(64),headers:{'content-type':'application/json; charset=utf-8'}}],variants,origin,{negativeStartup:proof})));
test('original failed rows remain rejected',()=>assert.throws(()=>validateAssets(original.servedAssets,variants,origin)));
console.log(JSON.stringify({passed:true,cases,scope:'Actual retained good and failed response rows, synthetic proposed UTF8 headers and explicitly mocked negative lifecycle; no old-run acceptance and no browser run'}));
