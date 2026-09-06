import {readFile,writeFile} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import {gunzipSync,gzipSync} from 'node:zlib';
import {createRequire} from 'node:module';
import {resolve} from 'node:path';
const require=createRequire(import.meta.url),compiler=require('../../tools/jit/compiler.cjs');
const base=resolve('.context/jit-fallback-census'),abi=JSON.parse(await readFile('web/jit/abi.json'));compiler.configure(abi.offsets);
const byPC=new Map(),all={};
for(const action of ['warm-settings','warm-home']){
 const bytes=await readFile(base+'/browser-1/'+action+'-rows.json.gz');const rows=JSON.parse(gunzipSync(bytes));all[action]={rows,sha256:createHash('sha256').update(bytes).digest('hex')};
 for(const r of rows){if(!(r.flags&1))continue;const thumb=!!(r.flags&256),key=r.pc+':'+thumb;let p=byPC.get(key);if(!p)byPC.set(key,p={pc:r.pc,thumb,variants:new Set()});p.variants.add(r.opcode);}
}
const groups=new Map();for(const p of byPC.values()){const base=p.pc&~1023,key=(base>>>0)+':'+p.thumb;let g=groups.get(key);if(!g)groups.set(key,g={pc:base>>>0,thumb:p.thumb,ops:new Map(),changedPositions:0});if(p.variants.size===1)g.ops.set(p.pc,[...p.variants][0]);else g.changedPositions++;}
for(const g of groups.values()){const result=compiler.compileRegion({...g,index:0});g.supported=new Set();g.watBytes=result?Buffer.byteLength(result.wat):0;if(result){for(const pc of g.ops.keys()){const bit=(pc-g.pc)>>(g.thumb?1:2);if((result.masks[bit>>>5]>>>(bit&31))&1)g.supported.add(pc);}}}
const emitPositions=[];for(const p of byPC.values()){const g=groups.get(((p.pc&~1023)>>>0)+':'+p.thumb);emitPositions.push({pc:p.pc,thumb:p.thumb,variants:[...p.variants],emittable:p.variants.size===1&&g.supported.has(p.pc),pageChanged:g.changedPositions>0});}await writeFile(base+'/emitter-positions.json.gz',gzipSync(JSON.stringify(emitPositions)));
const reports={};
for(const [action,{rows,sha256}]of Object.entries(all)){
 const buckets=Array.from({length:5},()=>({calls:0,noFetch:0,dataAbort:0,changingOpcode:0,stableEmittable:0,stableUnsupported:0})),pages=new Map(),positions=new Map();
 for(const r of rows){const b=buckets[r.admission];b.calls+=r.count;if(!(r.flags&1)){b.noFetch+=r.count;continue;}if(r.flags&16){b.dataAbort+=r.count;continue;}const thumb=!!(r.flags&256),pc=byPC.get(r.pc+':'+thumb),g=groups.get(((r.pc&~1023)>>>0)+':'+thumb);if(pc.variants.size>1){b.changingOpcode+=r.count;continue;}const emit=g.supported.has(r.pc);b[emit?'stableEmittable':'stableUnsupported']+=r.count;
  const key=g.pc+':'+thumb;let page=pages.get(key);if(!page)pages.set(key,page={pc:g.pc,hex:'0x'+g.pc.toString(16),thumb,changedPositions:g.changedPositions,watBytes:g.watBytes,observedStablePositions:g.ops.size,emittablePositions:g.supported.size,byAdmission:Array(5).fill(0),emittableByAdmission:Array(5).fill(0),topPositions:[]});page.byAdmission[r.admission]+=r.count;if(emit)page.emittableByAdmission[r.admission]+=r.count;
  const poskey=r.pc+':'+thumb;let pos=positions.get(poskey);if(!pos)positions.set(poskey,pos={pc:r.pc,hex:'0x'+r.pc.toString(16),thumb,opcode:r.opcode,opcodeHex:'0x'+r.opcode.toString(16),emittable:emit,admission:r.admission,count:0});pos.count+=r.count;
 }
 for(const pos of positions.values()){const page=pages.get(((pos.pc&~1023)>>>0)+':'+pos.thumb);page.topPositions.push(pos);}for(const page of pages.values())page.topPositions.sort((a,b)=>b.count-a.count);for(const page of pages.values())page.topPositions=page.topPositions.slice(0,8);
 const outside=[...pages.values()].filter(x=>x.emittableByAdmission[0]>0).sort((a,b)=>b.emittableByAdmission[0]-a.emittableByAdmission[0]);
 reports[action]={sourceSha256:sha256,buckets,observedPages:pages.size,outsideEmittablePages:outside.length,outsideTop:outside.slice(0,64),outsideTopBudgets:[8,16,32,64].map(n=>({pages:n,steps:outside.slice(0,n).reduce((s,x)=>s+x.emittableByAdmission[0],0),watBytes:outside.slice(0,n).reduce((s,x)=>s+x.watBytes,0),changedPages:outside.slice(0,n).filter(x=>x.changedPositions>0).length})),missingPositions:[...positions.values()].filter(x=>x.admission===2).sort((a,b)=>b.count-a.count).slice(0,30)};
}
const result={scope:'Actual observed fetched opcodes across both windows, exact original compiler.compileRegion masks. Positions with multiple observed opcodes are excluded; data-abort and no-fetch steps separate. Emittable is only a syntactic upper bound: raw/fetch/data-cache/budget guards may still force fallback; no speed claim. WAT text length is not WASM size or compilation time.',compilerSha256:createHash('sha256').update(await readFile('tools/jit/compiler.cjs')).digest('hex'),stablePositions:[...byPC.values()].filter(x=>x.variants.size===1).length,changedPositions:[...byPC.values()].filter(x=>x.variants.size>1).length,reports};
await writeFile(base+'/ranking.json',JSON.stringify(result,null,2)+'\n');for(const [k,r]of Object.entries(reports))console.log(JSON.stringify({action:k,buckets:r.buckets,topBudgets:r.outsideTopBudgets,top:r.outsideTop.slice(0,8)}));
