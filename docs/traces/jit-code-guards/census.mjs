import {readFile,writeFile} from 'node:fs/promises';
import {gunzipSync} from 'node:zlib';
import {createHash} from 'node:crypto';
import {resolve} from 'node:path';
import vm from 'node:vm';
import assert from 'node:assert/strict';
import wabtFactory from 'wabt';
const root=resolve('.context/jit-code-guards'),hash=b=>createHash('sha256').update(b).digest('hex');
const {default:createEmu}=await import(resolve('web/dist/emu.js'));
const m=await createEmu({locateFile:p=>resolve('web/dist/'+p)});
const alloc=b=>{const p=m._malloc(b.length);assert(p);m.HEAPU8.set(b,p);return p;};
const k=await readFile('web/firmware/kernel_1A543a.macho'),d=await readFile('web/firmware/devicetree_m68ap.bin'),kp=alloc(k),dp=alloc(d);
assert.equal(m._emu_init(kp,k.length,dp,d.length,0,0,256),0);m._free(kp);m._free(dp);
const fixture=await readFile('.context/graphics-speed/fixed-dismiss.snap'),sp=alloc(fixture),slots=272,diag=m._malloc(slots*8),mask=m._malloc(64);assert(diag&&mask);
const abi=JSON.parse(await readFile('web/jit/abi.json')),defs=JSON.parse(await readFile('web/jit/regions.json')),seed=await readFile('web/jit/seed.wasm');
const offsets=Array.from({length:16},(_,i)=>m._emu_jit_offset(i));assert.deepEqual(offsets,abi.offsets);assert.equal(m._emu_jit_abi(),abi.revision);assert.equal(hash(seed),abi.moduleSha256);
const input=JSON.parse(gunzipSync(await readFile('tools/jit/seed-input.json.gz'))),original=await readFile('tools/jit/compiler.cjs','utf8');
let source=original;
const helper=`
function censusAdd(address) { return '(;CENSUS_BEGIN;)(i64.store '+address+'(i64.add(i64.load '+address+')(i64.const 1)))(;CENSUS_END;)'; }
function censusCount(slot) { return censusAdd(C(${diag}+slot*8)); }
function censusPrefix(pc) { return '(;CENSUS_BEGIN;)(if(i32.lt_u '+C(pc)+reg(15)+')(then'+censusCount(3)+')(else'+censusCount(4)+'))(;CENSUS_END;)'; }
`;
// No nested marker pairs: prefix emits its own two unmarked counter expressions.
const cleanHelper=helper.replace("censusCount(3)","censusCount(3).replaceAll('(;CENSUS_BEGIN;)','').replaceAll('(;CENSUS_END;)','')").replace("censusCount(4)","censusCount(4).replaceAll('(;CENSUS_BEGIN;)','').replaceAll('(;CENSUS_END;)','')");
source=source.replace('function compileRegion(e){',cleanHelper+'\nfunction compileRegion(e){');
function replaceOnce(a,b){assert.equal(source.split(a).length,2,a);source=source.replace(a,b);}
replaceOnce("const b=blocks[bi];body+=`) `;", "const b=blocks[bi];body+=`) `;body+=censusCount(0)+censusAdd(add(C("+(diag+128)+"+(b.ops.length-1)*128),shl(op('i32.shr_u',op('i32.sub',reg(15),C(b.head)),C(e.thumb?1:2)),3)));" );
replaceOnce("for(const c of b.ops)body+=`(if(i32.ne(i32.load", "for(const c of b.ops)body+=censusCount(1)+censusPrefix(c.pc)+`(if(i32.ne(i32.load");
replaceOnce("${C(c.w)})(then(local.set $why(i32.const 5))", "${C(c.w)})(then${censusCount(2)}(local.set $why(i32.const 5))");
replaceOnce("`+put(15,C(c.pc))+c.body+set('n',add(L('n'),C(1)));", "`+censusCount(5)+put(15,C(c.pc))+c.body+set('n',add(L('n'),C(1)))+censusCount(6);");
replaceOnce(";if(DIAG)body+=", ";body+=censusAdd(add(C("+(diag+64)+"),shl(L('why'),3)));if(DIAG)body+=");
await writeFile(root+'/compiler-census.cjs',source);
const baselineBox={module:{exports:{}}},censusBox={module:{exports:{}}};vm.runInNewContext(original,baselineBox);vm.runInNewContext(source,censusBox);
const baseline=baselineBox.module.exports,census=censusBox.module.exports;baseline.configure(offsets);census.configure(offsets);
const funcs=[],bodyProof=[];const strip=s=>s.replaceAll(/\(;CENSUS_BEGIN;\)[\s\S]*?\(;CENSUS_END;\)/g,'');
for(let i=0;i<input.regions.length;i++){
 const r=input.regions[i],e={...r,ops:new Map(r.ops)},a=baseline.compileRegion(e),b=census.compileRegion(e);assert(a&&b);assert.equal(strip(b.wat),a.wat);assert.deepEqual(Array.from(b.masks,x=>x>>>0),defs[i].masks);assert.equal(r.pc,defs[i].pc);assert.equal(r.index,defs[i].index);assert.equal(r.thumb,defs[i].thumb);
 funcs.push(b.wat);bodyProof.push({index:r.index,originalWatSha256:hash(a.wat),instrumentedWatSha256:hash(b.wat),strippedWatIdentical:true});
}
const wabt=await wabtFactory(),parsed=wabt.parseWat('census.wat',`(module(import "env" "memory"(memory 8192))(import "env" "stale"(func $stale(param i32 i32)))(import "env" "span"(func $span(param i32 i32 i32 i32 i32)(result i32)))${funcs.join('')})`);parsed.validate();const binary=Buffer.from(parsed.toBinary({}).buffer);parsed.destroy();await writeFile(root+'/census-seed.wasm',binary);
const modules=[null,await WebAssembly.compile(seed),await WebAssembly.compile(binary)],ids=[];
const report={scope:'Actual raw-code guard counts in fixed100M genuine MBX Settings workload. Every existing guard, order and policy preserved. Functional concurrent run, no timing claim.',runtimeSha256:hash(await readFile('web/dist/emu.wasm')),seedSha256:hash(seed),inputSha256:hash(await readFile('tools/jit/seed-input.json.gz')),compilerSha256:hash(original),instrumentedCompilerSha256:hash(source),diagnosticSeedSha256:hash(binary),fixtureSha256:hash(fixture),counterSlots:slots,counterBytes:slots*8,abiRevision:abi.revision,bodyProof,runs:[]};
for(let mode=0;mode<3;mode++){
 m._emu_jit_reset();for(const id of ids.splice(0))m.removeFunction(id);assert.equal(m._emu_load_snapshot(sp,fixture.length),0);m.HEAPU8.fill(0,diag,diag+slots*8);
 if(mode){const instance=await WebAssembly.instantiate(modules[mode],{env:{memory:m.jitMemory,stale:m._emu_jit_stale,span:m._emu_jit_span}});for(const r of defs){const id=m.addFunction(instance.exports['b'+r.index],'iii');ids.push(id);new Uint32Array(m.jitMemory.buffer,mask,16).set(r.masks);assert.equal(m._emu_jit_install(r.pc,+r.thumb,id,mask),1);}}
 m._emu_jit_enable(mode?1:0);m._emu_touch(1,275,250);for(let done=0;done<100e6;done+=1e6){assert.equal(m._emu_run(1e6),1e6);if(done+1e6===5e6)m._emu_touch(0,275,250);}
 const p=m._emu_save_snapshot(),size=m._emu_snapshot_size();assert(p&&size);const snapshotHash=hash(m.HEAPU8.subarray(p,p+size));m._emu_free_snapshot();
 const row={mode:['interpreter','productionSeed','diagnosticSeed'][mode],snapshotHash,generated:m._emu_jit_stat(0),calls:m._emu_jit_stat(1),regions:m._emu_jit_stat(2)};
 if(mode){assert.equal(snapshotHash,report.runs[0].snapshotHash);assert(row.generated>0&&row.calls>0);}if(mode===2){for(const key of ['generated','calls','regions'])assert.equal(row[key],report.runs[1][key]);report.counters=Array.from(new BigUint64Array(m.jitMemory.buffer,diag,slots),Number);}
 report.runs.push(row);console.log(JSON.stringify(row));
}
const v=report.counters,hist=[];for(let len=1;len<=16;len++)for(let entry=0;entry<16;entry++){const count=v[16+(len-1)*16+entry];if(count){assert(entry<len);hist.push({blockLength:len,entryOffset:entry,arrivals:count});}}
report.names=['blockArrivals','rawChecksAttempted','rawMismatches','prefixChecks','entryOrLaterChecks','bodyStarts','retired','unused'];report.reasons=['early','budget','page','mode','unsupported','stale','memory','alias'];report.histogram=hist;
assert.equal(v[0],hist.reduce((n,x)=>n+x.arrivals,0));assert.equal(v[1],v[3]+v[4]);assert.equal(v[6],report.runs[2].generated);assert(v[5]>=v[6]);assert.equal(v.slice(8,16).reduce((a,b)=>a+b,0),report.runs[2].calls);assert.equal(v[2],v[13]);
report.derived={attemptedChecksPerRetired:v[1]/v[6],attemptedChecksPerBodyStart:v[1]/v[5],prefixShare:v[3]/v[1],histogramTotalGuardSlots:hist.reduce((n,x)=>n+x.arrivals*x.blockLength,0),histogramEarlierPrefixSlots:hist.reduce((n,x)=>n+x.arrivals*x.entryOffset,0),unretiredBodyStarts:v[5]-v[6]};assert(report.derived.histogramTotalGuardSlots>=v[1]);assert(report.derived.histogramEarlierPrefixSlots>=v[3]);report.passed=true;
await writeFile(root+'/census.json',JSON.stringify(report,null,2)+'\n');console.log(JSON.stringify({counts:v.slice(0,16),derived:report.derived,passed:true}));
m._emu_jit_reset();for(const id of ids)m.removeFunction(id);m._free(mask);m._free(diag);m._free(sp);
