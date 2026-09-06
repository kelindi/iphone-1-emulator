import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
const sha=b=>createHash('sha256').update(b).digest('hex');
export function assetPath(raw,origin){
 assert.equal(typeof raw,'string','Response URL must be a stored string');
 const u=new URL(raw);if(u.origin!==new URL(origin).origin)return null;
 assert(!u.username&&!u.password,'Credentialed URL');return decodeURIComponent(u.pathname).replace(/^\//,'')||'index.html';
}
export function validateAssets(rows,variants,origin,{requireComplete=true}={}){
 const accepted=[],deliberate=[];
 for(const r of rows){
  const path=assetPath(r.url,origin);if(!path)continue;
  assert(!r.error,'Failed response-body receipt: '+path);assert.equal(r.status,200,'Tracked asset response status');
  const names=r.phase==='online-A'?['A']:r.phase==='waiting-B'?['A','B']:r.phase==='corrupt-update-C'?['B','C']:['B'];
  const values=names.flatMap(n=>variants[n]?.hashes[path]?[variants[n].hashes[path]]:[]);
  if(r.phase==='corrupt-update-C'&&path==='dist/emu.wasm'&&r.sha256===sha('intentionally corrupt update body')){deliberate.push(r);continue;}
  if(r.phase==='corrupt-seed-offline'&&path==='jit/seed.wasm'&&r.sha256===sha('corrupt optional seed')){deliberate.push(r);continue;}
  assert(values.includes(r.sha256),'Unexpected served '+path+' during '+r.phase);accepted.push({...r,path});
 }
 if(requireComplete){
  for(const n of ['A','B'])for(const path of ['dist/emu.wasm','emu-worker.js','seed-jit.js','jit/seed.wasm'])
   assert(accepted.some(r=>r.path===path&&r.sha256===variants[n].hashes[path]&&(n==='A'?r.phase==='online-A':r.phase!=='online-A')),'Missing actual '+n+' '+path);
  assert(accepted.some(r=>r.path==='dist/emu.wasm'&&r.phase==='disconnected-B'&&r.fromServiceWorker&&r.sha256===variants.B.hashes['dist/emu.wasm']),'Missing actual offline candidate runtime');
 }
 return {passed:true,accepted:accepted.length,deliberateCorruptions:deliberate.length,scope:'Actual received local executable/shell bodies; allowed corruption only exact bytes in explicit negative phases'};
}
