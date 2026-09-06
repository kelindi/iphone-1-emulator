import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
const sha=b=>createHash('sha256').update(b).digest('hex');
export function assetPath(raw,origin){
 assert.equal(typeof raw,'string','Response URL must be a stored string');
 const u=new URL(raw);if(u.origin!==new URL(origin).origin)return null;
 assert(!u.username&&!u.password,'Credentialed URL');return decodeURIComponent(u.pathname).replace(/^\//,'')||'index.html';
}
export function validateAssets(rows,variants,origin,{requireComplete=true,negativeStartup=null}={}){
 const accepted=[],deliberate=[],unavailableNegative=[];
 for(const r of rows){
  const path=assetPath(r.url,origin);if(!path)continue;
  if(r.error){assert(expectedStartupUnavailable(r,negativeStartup),'Unexpected unavailable response body: '+path);unavailableNegative.push({...r,byteProof:false,classification:'Expected failed-kernel startup worker destruction; no body hash available'});continue;}assert.equal(r.status,200,'Tracked asset response status');
  if(/\.(css|js|json|html|svg)$/.test(path)){const mime=Object.entries(r.headers||{}).find(([k])=>k.toLowerCase()==='content-type')?.[1];assert(typeof mime==='string'&&/charset\s*=\s*utf-8/i.test(mime),'Missing truthful UTF8 text header: '+path);}
  const names=r.phase==='online-A'?['A']:r.phase==='waiting-B'?['A','B']:r.phase==='corrupt-update-C'?['B','C']:['B'];
  const values=names.flatMap(n=>variants[n]?.hashes[path]?[variants[n].hashes[path]]:[]);
  if(r.phase==='corrupt-update-C'&&path==='dist/emu.wasm'&&r.sha256===sha('intentionally corrupt update body')){deliberate.push(r);continue;}
  if(r.phase==='corrupt-seed-offline'&&path==='jit/seed.wasm'&&r.sha256===sha('corrupt optional seed')){deliberate.push(r);continue;}
  assert(values.includes(r.sha256),'Unexpected served '+path+' during '+r.phase);accepted.push({...r,path});
 }
 if(requireComplete){
  for(const n of ['A','B'])for(const path of Object.keys(variants[n].hashes))
   assert(accepted.some(r=>r.path===path&&r.sha256===variants[n].hashes[path]&&(n==='A'?r.phase==='online-A':r.phase!=='online-A')),'Missing actual '+n+' '+path);
  assert(accepted.some(r=>r.path==='dist/emu.wasm'&&r.phase==='disconnected-B'&&r.fromServiceWorker&&r.sha256===variants.B.hashes['dist/emu.wasm']),'Missing actual offline candidate runtime');
 }
 return {passed:true,accepted:accepted.length,deliberateCorruptions:deliberate.length,unavailableNegative,scope:'Actual received local executable/shell bodies; allowed corruption only exact bytes in explicit negative phases'};
}

export function expectedStartupUnavailable(row,proof){
 if(!proof||proof.phase!=='corrupt-kernel-offline'||row.phase!==proof.phase||row.pageId!==proof.pageId||row.pageId==null)return false;
 if(!proof.observed||proof.observed.ready!==false||!proof.observed.retryEnabled||!proof.observed.kernelCacheRemoved||!proof.kernelRequestDenied)return false;
 if(proof.corruption?.sha256!==sha('corrupted test entry')||proof.corruption.kernelURL!==proof.observed.kernelURL)return false;
 if(proof.physicalBarrier?.listening!==false||proof.physicalBarrier.refusal?.code!=='ECONNREFUSED')return false;
 if(row.at<proof.started||row.at>proof.ended||!Number.isFinite(row.errorAt))return false;
 if(!/^(response\.body: )?(Target page, context or browser has been closed|Response body is unavailable)$/.test(row.error))return false;
 return proof.workerClosures.some(w=>w.pageId===proof.pageId&&w.createdPhase===proof.phase&&w.closedPhase===proof.phase&&/\/emu-worker\.js(?:\?|$)/.test(w.url)&&w.created<=row.at&&w.closed>=row.at&&w.closed<=row.errorAt&&w.closed<=proof.ended);
}

export const responseMime=type=>/^(text\/|application\/json|image\/svg\+xml)/.test(type)?type+'; charset=utf-8':type;
