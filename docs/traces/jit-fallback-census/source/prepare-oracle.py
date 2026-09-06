from pathlib import Path
import json,hashlib
p=Path('.context/jit-fallback-census');old=Path('docs/traces/jit-continuation/oracle.c').read_text()
a=old.index('unsigned original_try(');b=old.index('static s5l8900_t machine;',a)
s=old[:a]+old[b:]
s=s.replace('#define arm_wasm_jit_try candidate_try\n#include "jit-runtime.c"\n#undef arm_wasm_jit_try', '#ifdef FC_ORACLE\n#include "jit-runtime.c"\n#else\n#include "runtime-baseline.c"\n#endif\n#define original_try arm_wasm_jit_try\n#define candidate_try arm_wasm_jit_try')
s=s.replace('unsigned arm_wasm_jit_try(arm_cpu_t *c,unsigned limit) {return choose_original?original_try(c,limit):candidate_try(c,limit);}','')
(p/'oracle.c').write_text(s)
oldtest=Path('docs/traces/jit-continuation/test.cjs').read_text()
s=oldtest.replace("const create=require(process.env.ORACLE_MODULE||'./oracle.cjs')", "const create=require(process.env.ORACLE_MODULE||'./observer-oracle.cjs')")
s=s.replace("const originalRuntimeOnly=process.env.TEST_ORIGINAL_RUNTIME==='1';", "const originalRuntimeOnly=true; const cases=[];")
s=s.replace('const before=state();let ran;', 'const before=state();let ran; if(m._emu_fc_start)m._emu_fc_start();')
s=s.replace('const row={engine,ran,generated:', '''let observer=null;
   if(m._emu_fc_stop){m._emu_fc_stop();const v=new DataView(m.jitMemory.buffer),p=m._emu_fc_summary();const names=['calls','ok','fetched','nofetch','nonok','overflow','mapOverflow','epoch','installs'];observer=Object.fromEntries(names.map((name,i)=>[name,Number(v.getBigUint64(p+i*8,true))]));let sum=0;const base=m._emu_fc_rows();for(let i=0;i<m._emu_fc_capacity();i++)if(v.getUint32(base+i*40+28,true))sum+=Number(v.getBigUint64(base+i*40+32,true));assert.equal(sum,observer.calls);assert.equal(observer.calls,observer.ok+observer.nonok);assert.equal(observer.calls,observer.fetched+observer.nofetch);assert.equal(observer.overflow,0);assert.equal(observer.mapOverflow,0);assert.equal(observer.installs,0);if(!tryOnly)assert.equal(ran,observer.ok+m._emu_jit_stat(0));}
   const row={engine,ran,generated:''')
s=s.replace('rows.push(row);', "rows.push(row);cases.push({label,count,cap,mmu,warm,guard,engine,ran,stateSha256:crypto.createHash('sha256').update(bytes).digest('hex'),...row,observer});")
s=s.replace('const report={passed:true,', 'const report={passed:true,cases,')
(p/'test-boundaries.cjs').write_text(s)
(p/'oracle-source-provenance.json').write_text(json.dumps(dict(originalOracle='docs/traces/jit-continuation/oracle.c',originalTest='docs/traces/jit-continuation/test.cjs',oracleSourceSha256=hashlib.sha256((p/'oracle.c').read_bytes()).hexdigest(),testSourceSha256=hashlib.sha256((p/'test-boundaries.cjs').read_bytes()).hexdigest(),scope='Both separate modules use original runtime policy; observer module alone adds census and instrumented arm_step. Frozen prior actual-emitter boundary cases reused. Independent cross-module hashes compared after both finish.'),indent=2)+'\n')
