import fs from 'node:fs/promises';
import {createHash} from 'node:crypto';
import {gunzipSync,gzipSync} from 'node:zlib';
import {resolve} from 'node:path';
import {pathToFileURL,fileURLToPath} from 'node:url';
import assert from 'node:assert/strict';
const base=resolve('.context/jit-fallback-census'),kind=process.argv[2];assert(['control','observer'].includes(kind));
const url=pathToFileURL(base+'/'+kind+'/emu.js');const {default:create}=await import(url);
const m=await create({locateFile:p=>fileURLToPath(new URL(p,url))});
const hash=b=>createHash('sha256').update(b).digest('hex');
const alloc=b=>{const p=m._malloc(b.length);assert(p);m.HEAPU8.set(b,p);return p;};
const kernel=await fs.readFile('web/firmware/kernel_1A543a.macho'),tree=await fs.readFile('web/firmware/devicetree_m68ap.bin');
const kp=alloc(kernel),tp=alloc(tree);assert.equal(m._emu_init(kp,kernel.length,tp,tree.length,0,0,256),0);m._free(kp);m._free(tp);
const fixturePath='.context/graphics-speed/fixed-dismiss.snap';let fixture=await fs.readFile(fixturePath);if(fixture[0]===31&&fixture[1]===139)fixture=gunzipSync(fixture);
const p=alloc(fixture);assert.equal(m._emu_load_snapshot(p,fixture.length),0);m._free(p);
const seed=await fs.readFile('web/jit/seed.wasm'),defsBytes=await fs.readFile('web/jit/regions.json'),abi=JSON.parse(await fs.readFile('web/jit/abi.json'));
assert.equal(hash(seed),abi.moduleSha256);assert.equal(hash(defsBytes),abi.regionsSha256);assert.equal(m._emu_jit_abi(),abi.revision);assert.deepEqual(Array.from({length:16},(_,i)=>m._emu_jit_offset(i)),abi.offsets);
const {instance}=await WebAssembly.instantiate(seed,{env:{memory:m.jitMemory,stale:m._emu_jit_stale,span:m._emu_jit_span}}),defs=JSON.parse(defsBytes),mask=m._malloc(64);
for(const r of defs){const fn=m.addFunction(instance.exports['b'+r.index],'iii');m.HEAPU32.set(r.masks,mask>>>2);assert.equal(m._emu_jit_install(r.pc,+r.thumb,fn,mask),1);}
m._free(mask);assert.equal(m._emu_jit_stat(2),512);m._emu_jit_enable(1);
function census(){if(!m._emu_fc_summary)return null;const v=new DataView(m.jitMemory.buffer),p=m._emu_fc_summary(),names=['calls','ok','fetched','nofetch','nonok','overflow','mapOverflow','epoch','installs'];return {...Object.fromEntries(names.map((k,i)=>[k,Number(v.getBigUint64(p+8*i,true))])),rows:v.getUint32(p+72,true),enabled:v.getUint32(p+76,true)};}
const report={kind,fixturePath,fixtureSha256:hash(fixture),moduleSha256:hash(await fs.readFile(base+'/'+kind+'/emu.wasm')),seedSha256:hash(seed),definitionCount:defs.length,initialActive:m._emu_jit_stat(2),instructions:100000000,checkpoints:[]};
m._emu_touch(1,275,250);if(m._emu_fc_start)m._emu_fc_start();
for(let done=0;done<100000000;){assert.equal(m._emu_run(1000000),1000000);done+=1000000;if(done===5000000)m._emu_touch(0,275,250);const c=census();if(c){assert.equal(c.overflow,0);assert.equal(c.mapOverflow,0);assert.equal(c.installs,0);assert.equal(c.ok+m._emu_jit_stat(0),done);assert.equal(c.calls,c.fetched+c.nofetch);assert.equal(c.calls,c.ok+c.nonok);}
 if(done%25000000===0){const q=m._emu_save_snapshot(),n=m._emu_snapshot_size();assert(q&&n);const bytes=m.HEAPU8.subarray(q,q+n);report.checkpoints.push({done,rawSha256:hash(bytes),bytes:n,generated:m._emu_jit_stat(0),calls:m._emu_jit_stat(1),regions:m._emu_jit_stat(2),census:c});m._emu_free_snapshot();}
}
if(m._emu_fc_stop)m._emu_fc_stop();
const rgba=m._malloc(320*480*4),dims=m._malloc(8);assert.equal(m._emu_frame(rgba,dims,dims+4),1);const w=m.HEAPU32[dims>>>2],h=m.HEAPU32[(dims+4)>>>2];report.frame={width:w,height:h,sha256:hash(m.HEAPU8.subarray(rgba,rgba+w*h*4))};await fs.writeFile(base+'/'+kind+'-100m.rgba.gz',gzipSync(m.HEAPU8.subarray(rgba,rgba+w*h*4)));m._free(rgba);m._free(dims);
if(m._emu_fc_rows){const v=new DataView(m.jitMemory.buffer),start=m._emu_fc_rows(),names=['pc','opcode','flags','provenance','guard','admission','status'];const rows=[];let sum=0;for(let i=0;i<m._emu_fc_capacity();i++){const p=start+40*i;if(!v.getUint32(p+28,true))continue;const r=Object.fromEntries(names.map((k,j)=>[k,v.getUint32(p+4*j,true)]));r.count=Number(v.getBigUint64(p+32,true));sum+=r.count;rows.push(r);}const c=census();assert.equal(sum,c.calls);assert.equal(rows.length,c.rows);report.census=c;report.admission=Array(5).fill(0);report.provenance=Array(9).fill(0);for(const r of rows){report.admission[r.admission]+=r.count;report.provenance[r.provenance]+=r.count;}assert.equal(report.provenance[8],0);assert(report.provenance[6]>0,'actual production positive-prefix fallback must be observed');await fs.writeFile(base+'/observer-100m-rows.json.gz',gzipSync(JSON.stringify(rows)));}
report.passed=true;await fs.writeFile(base+'/'+kind+'-100m.json',JSON.stringify(report,null,2)+'\n');console.log(JSON.stringify(report));
