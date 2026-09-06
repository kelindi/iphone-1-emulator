// Stored response rows contain URL strings, unlike Playwright Response.url().
export const requiredAssets=['index.html','app.js','phone.css','dist/emu.js','dist/emu.wasm','seed-jit.js','emu-worker.js','jit/seed.wasm','jit/regions.json','jit/abi.json','storage.js','assets.js','manifest.json'];
export function validateAssets(assets,site,origin){
 const errors=[],seen=new Set();
 if(!Array.isArray(assets))return {passed:false,errors:['Asset rows must be an array'],seen:[]};
 for(const row of assets){
  if(!row||typeof row.url!=='string'){errors.push('Stored asset URL must be a string');continue;}
  let url;try{url=new URL(row.url);}catch{errors.push('Malformed stored asset URL');continue;}
  if(url.origin!==origin){errors.push('Unexpected asset origin: '+url.origin);continue;}
  const name=url.pathname==='/'?'index.html':url.pathname.slice(1);seen.add(name);
  if(!Object.hasOwn(site,name)||typeof row.sha256!=='string'||!(/^[a-f0-9]{64}$/).test(row.sha256)||row.sha256!==site[name]||row.error)errors.push('Actual served hash mismatch: '+name);
 }
 for(const name of requiredAssets)if(!seen.has(name))errors.push('Missing required asset: '+name);
 return {passed:errors.length===0,errors,seen:[...seen]};
}
