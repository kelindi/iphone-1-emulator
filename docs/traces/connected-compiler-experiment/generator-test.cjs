const assert=require('node:assert/strict'),fs=require('node:fs');
const {generate}=require('./generator.cjs');
const offsets=JSON.parse(fs.readFileSync('web/jit/abi.json')).offsets;
const [R,F,FETCH]=offsets; const compiler=require('../../tools/jit/compiler.cjs');
(async()=>{const wabt=await require('wabt')();let checks=0;const check=(v,m)=>{assert(v,m);checks++;};
async function build(regions,options={}){const g=generate(regions,offsets,options);check(g.proof.every(p=>p.originalRun===p.projectedRun),'exact original run projection');const p=wabt.parseWat('connected.wat',g.wat);p.validate();const bytes=p.toBinary({}).buffer;p.destroy();return {g,bytes};}
const regions=[{pc:0x1000,thumb:false,index:0,ops:[[0x1000,0xea0000fe]]},{pc:0x1400,thumb:false,index:1,ops:[[0x1400,0xe2800001],[0x1404,0xeafffefd]]}];
const b=await build(regions,{clusters:[[0,1]]});
async function run({budget=6,start=0x1000,flags=0x13,stale=false,failFetch=false,mutatePC=null}={}){const memory=new WebAssembly.Memory({initial:8192});const view=new DataView(memory.buffer),c=65536,fetchA=131072,fetchB=132096;for(const e of regions)for(const [pc,w]of e.ops)view.setUint32((e.pc===0x1000?fetchA:fetchB)+(pc&1023),w,true);if(stale)view.setUint32(fetchB,0,true);view.setUint32(c+R+60,start,true);view.setUint32(c+F,flags,true);view.setUint32(c+FETCH,fetchA,true);let calls=[],stales=[];const {instance}=await WebAssembly.instantiate(b.bytes,{env:{memory,span(){throw Error('unexpected span');},stale:(pc,t)=>stales.push([pc,t]),connected_fetch:(c,pc,f)=>{calls.push([pc,f]);return failFetch?0:pc>>>10===5?fetchB:fetchA;}}});const n=instance.exports.b0(c,budget);return {n,calls,stales,r0:view.getUint32(c+R,true),pc:view.getUint32(c+R+60,true),flags:view.getUint32(c+F,true)};}
let r=await run();check(r.n===6&&r.r0===2&&r.calls.length===3,'cross-region count/register continuity');
r=await run({budget:0});check(r.n===0&&r.calls.length===0,'zero budget no fetch');
r=await run({budget:1});check(r.n===1&&r.calls.length===0&&r.pc===0x1400,'exact budget before fetch');
r=await run({start:0x1800});check(r.n===0&&r.calls.length===0,'unknown initial region');
r=await run({failFetch:true});check(r.n===1&&r.calls.length===1&&r.pc===0x1400,'fetch failure preserves prefix');
r=await run({stale:true});check(r.n===1&&r.stales.length===1&&r.stales[0][0]===0x1400&&r.stales[0][1]===0,'actual destination stale attribution');
check(JSON.stringify(b.g.definitions)===JSON.stringify(regions.map(e=>{compiler.configure(offsets);const z=compiler.compileRegion(e);return {pc:e.pc,thumb:e.thumb,index:e.index,masks:z.masks.map(x=>x>>>0)};})),'unchanged definitions');
async function synthetic(es,opts={}){const x=await build(es,{clusters:[es.map(e=>e.index)]});const memory=new WebAssembly.Memory({initial:8192}),v=new DataView(memory.buffer),c=65536,base=262144,fetches=[],stales=[];for(let j=0;j<es.length;j++)for(const [pc,op]of es[j].ops)(es[j].thumb?v.setUint16.bind(v):v.setUint32.bind(v))(base+j*1024+(pc&1023),op,true);for(const [i,value]of Object.entries(opts.regs||{}))v.setUint32(c+R+4*Number(i),value,true);v.setUint32(c+R+60,opts.pc??es[0].pc,true);v.setUint32(c+F,opts.flags??(es[0].thumb?0x33:0x13),true);v.setUint32(c+FETCH,base,true);if(opts.corrupt!==undefined)v.setUint32(base+opts.corrupt*1024,0,true);const {instance}=await WebAssembly.instantiate(x.bytes,{env:{memory,span:()=>0,stale:(pc,t)=>stales.push([pc,t]),connected_fetch:(c,pc,f)=>{const j=es.findIndex(e=>e.pc===(pc&~1023) && !!e.thumb===!!(f&32));const e=es[j];const bit=(pc&1023)/(e.thumb?2:4);if(!((x.g.definitions[j].masks[bit>>>5]>>>(bit&31))&1))return 0;fetches.push([pc,f]);return base+j*1024;}}});const exports=es.map(e=>instance.exports['b'+e.index]);check(new Set(exports).size===exports.length,'distinct wrapper identity');const n=exports[0](c,opts.budget??3);return {n,fetches,stales,r:Array.from({length:16},(_,i)=>v.getUint32(c+R+i*4,true)),f:v.getUint32(c+F,true)};}
let x=await synthetic([{pc:0x1000,thumb:false,index:0,ops:[[0x1000,0xe12fff11]]},{pc:0x1400,thumb:true,index:1,ops:[[0x1400,0x3001],[0x1402,0xe7fe]]}],{regs:{1:0x1401}});
check(x.n===3&&x.r[0]===1&&(x.f&32)&&x.fetches.length===1,'ARM BX to Thumb preserves register locals and mode');
x=await synthetic([{pc:0x1000,thumb:false,index:0,ops:[[0x13fc,0xe2800001]]},{pc:0x1400,thumb:false,index:1,ops:[[0x1400,0xe2800001]]}],{pc:0x13fc,budget:2});
check(x.n===2&&x.r[0]===2&&x.fetches.length===1,'page fallthrough exact budget');
x=await synthetic([{pc:0x1000,thumb:false,index:0,ops:[[0x1000,0xea0001fe]]},{pc:0x1400,thumb:false,index:1,ops:[[0x1400,0xe2800001]]}]);
check(x.n===1&&x.r[15]===0x1800&&!x.fetches.length,'unknown target no fetch');
x=await synthetic([{pc:0x1000,thumb:false,index:0,ops:[[0x1000,0xea0000ff]]},{pc:0x1400,thumb:false,index:1,ops:[[0x1400,0xe2800001]]}]);
check(x.n===1&&x.r[15]===0x1404&&!x.fetches.length,'target absent admission stops with no successful fetch');
x=await synthetic([{pc:0x1000,thumb:false,index:0,ops:[[0x1000,0xe12fff11]]},{pc:0x1400,thumb:true,index:1,ops:[[0x1400,0x3001]]}],{regs:{1:0x1401},corrupt:1});
check(x.n===1&&x.stales.length===1&&x.stales[0][0]===0x1400&&x.stales[0][1]===1,'Thumb destination stale identity');
x=await synthetic([{pc:0x1000,thumb:false,index:0,ops:[[0x1000,0xe5900000]]},{pc:0x1400,thumb:false,index:1,ops:[[0x1400,0xe2800001]]}]);
check(x.n===0&&!x.fetches.length,'span failure with zero prefix stops');
x=await synthetic([{pc:0x1000,thumb:false,index:0,ops:[[0x1000,0xea0000fe]]},{pc:0x1400,thumb:false,index:1,ops:[[0x1400,0xed900a00]]}]);
check(x.n===1&&x.r[15]===0x1400&&x.fetches.length===1,'positive prefix VFP access refusal does not retry inherited why');
check(generate(regions,offsets).clusters.some(c=>c.length===2),'static direct branch clustering');
assert.throws(()=>generate(regions,offsets,{clusters:[[0,0]]}));checks++;
assert.throws(()=>generate(regions,offsets,{maxCluster:5}));checks++;
fs.writeFileSync('.context/connected-engine/generator-test-report.json',JSON.stringify({passed:true,checks,example:r,offsets},null,2)+'\n');console.log({passed:true,checks});})().catch(e=>{console.error(e);process.exitCode=1;});
