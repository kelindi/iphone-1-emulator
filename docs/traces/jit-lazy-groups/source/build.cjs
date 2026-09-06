const fs=require('node:fs');
const path=require('node:path');
const zlib=require('node:zlib');
const crypto=require('node:crypto');
const wabtFactory=require('../../node_modules/wabt');
const original=require('../../tools/jit/compiler.cjs');
const candidate=require('./compiler.cjs');
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
const here=__dirname,repo=path.resolve(here,'../..');
const read=p=>fs.readFileSync(path.join(repo,p));
const abi=JSON.parse(read('web/jit/abi.json'));
const input=JSON.parse(zlib.gunzipSync(read('tools/jit/seed-input.json.gz')));
const expected=read('web/jit/regions.json');
function bodies(b){
 let p=8;const u=()=>{let n=0,s=0,v;do{v=b[p++];n+=(v&127)*2**s;s+=7;}while(v&128);return n;};
 while(p<b.length){const id=b[p++],size=u(),end=p+size;if(id===10){const out=[],count=u();for(let i=0;i<count;i++){const n=u();out.push(n);p+=n;}if(p!==end)throw Error('code section bounds');return out;}p=end;}
 throw Error('missing code');
}
(async()=>{
 const wabt=await wabtFactory(),report={passed:false,groupSize:32,sourcePins:JSON.parse(fs.readFileSync(path.join(here,'source-pins.json'))),variants:{}};
 for(const [name,compiler]of [['original',original],['candidate',candidate]]){
  compiler.configure(abi.offsets);const functions=[],defs=[],details=[];
  for(const e of input.regions){const built=compiler.compileRegion({...e,ops:new Map(e.ops)});if(!built)throw Error('empty');functions.push(built.wat);defs.push({pc:e.pc,thumb:e.thumb,index:e.index,masks:built.masks.map(n=>n>>>0)});details.push({index:e.index,pc:e.pc,thumb:e.thumb,heads:built.heads,groups:built.groups||1,internalFunctions:built.internalFunctions||0});}
  const wat=`(module(import "env" "memory"(memory 8192))(import "env" "stale"(func $stale(param i32 i32)))(import "env" "span"(func $span(param i32 i32 i32 i32 i32)(result i32)))${functions.join('')})`;
  const parsed=wabt.parseWat(name+'.wat',wat);let binary;try{parsed.validate();binary=Buffer.from(parsed.toBinary({}).buffer);}finally{parsed.destroy();}
  const definitions=Buffer.from(JSON.stringify(defs)+'\n');
  if(!definitions.equals(expected))throw Error('original entry masks changed');
  if(name==='original'&&!binary.equals(read('web/jit/seed.wasm')))throw Error('original seed not reproduced');
  if(binary.length>32*1024*1024)throw Error('asset cap');
  const sizes=bodies(binary).sort((a,b)=>a-b),dir=path.join(here,name);fs.mkdirSync(dir,{recursive:true});
  fs.writeFileSync(path.join(dir,'seed.wasm'),binary);fs.writeFileSync(path.join(dir,'regions.json'),definitions);
  fs.writeFileSync(path.join(dir,'abi.json'),JSON.stringify({...abi,moduleSha256:sha(binary),regionsSha256:sha(definitions)},null,2)+'\n');
  fs.writeFileSync(path.join(dir,'regions-detail.json'),JSON.stringify(details,null,2)+'\n');
  report.variants[name]={moduleBytes:binary.length,moduleSha256:sha(binary),definitionsSha256:sha(definitions),publicFunctions:defs.length,definedFunctions:sizes.length,functionBodies:{sum:sizes.reduce((a,b)=>a+b,0),median:sizes[Math.floor(sizes.length/2)],p95:sizes[Math.floor(.95*(sizes.length-1))],max:sizes.at(-1)},splitRegions:details.filter(d=>d.groups>1).length};
 }
 report.passed=true;report.scope='Exact original seed reproduction and identical public admission masks; structural body-size measurements only, no execution or speed result';
 fs.writeFileSync(path.join(here,'build-report.json'),JSON.stringify(report,null,2)+'\n');console.log(JSON.stringify(report,null,2));
})().catch(e=>{console.error(e);process.exitCode=1;});
