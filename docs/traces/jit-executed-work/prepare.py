from pathlib import Path
import shutil,json,hashlib
r=Path.cwd();old=r/'.context/jit-fallback-census';p=r/'.context/jit-executed-work';proof=[]
sha=lambda v:hashlib.sha256(v).hexdigest()
# Reuse accepted semantics-preserving interpreter/runtime observation projection.
script=(old/'prepare.py').read_text().replace("out=root/'.context/jit-fallback-census'","out=root/'.context/jit-executed-work'")
(p/'prepare-interpreter.py').write_text(script);exec(compile(script,str(p/'prepare-interpreter.py'),'exec'))
for name in ['census.c','census.h','test-collector.c','test-overflow.c','test-long-probes.c','test-sizing.c','runtime-baseline.c','jit-runtime.h','oracle.c']:
 shutil.copy2(old/name,p/name)
c=(p/'census.c').read_text();original=c
c=c.replace('static uint64_t reset_epoch;', 'static uint64_t reset_epoch;\nstatic uint64_t generated[512u*512u], generated_overflow;')
c=c.replace('void fc_reset(void) {','void fc_reset(void) { memset(generated,0,sizeof generated); generated_overflow=0;')
c=c.replace('void fc_start(void) {','void fc_start(void) { memset(generated,0,sizeof generated); generated_overflow=0;')
c+='\nuint64_t *fc_generated_counts(void){return generated;}\nuint64_t *fc_generated_overflow(void){return &generated_overflow;}\nuint32_t *fc_enabled_pointer(void){return &total.enabled;}\nunsigned fc_generated_capacity(void){return 512u*512u;}\n'
(p/'census.c').write_text(c)
h=(p/'census.h').read_text().replace('#endif','uint64_t *fc_generated_counts(void);\nuint64_t *fc_generated_overflow(void);\nuint32_t *fc_enabled_pointer(void);\nunsigned fc_generated_capacity(void);\n#endif');(p/'census.h').write_text(h)
s=(p/'jit-runtime.c').read_text();s+='''\nEMSCRIPTEN_KEEPALIVE uintptr_t emu_fc_generated(void){return (uintptr_t)fc_generated_counts();}
EMSCRIPTEN_KEEPALIVE uintptr_t emu_fc_generated_overflow(void){return (uintptr_t)fc_generated_overflow();}
EMSCRIPTEN_KEEPALIVE uintptr_t emu_fc_enabled_pointer(void){return (uintptr_t)fc_enabled_pointer();}
EMSCRIPTEN_KEEPALIVE unsigned emu_fc_generated_capacity(void){return fc_generated_capacity();}
''';(p/'jit-runtime.c').write_text(s)
compiler=(r/'tools/jit/compiler.cjs').read_text();s=compiler
s=s.replace('let R,F,FETCH,OFF,DIAG;', 'let R,F,FETCH,OFF,DIAG;\nlet censusConfig=null;')
helper='''
function censusCommit(e,c){
 if(!censusConfig)return '';
 if(!Number.isInteger(e.index)||e.index<0||e.index>=512)throw Error('Counter region index out of bounds');
 const offset=c.pc-e.pc,width=e.thumb?2:4;
 if(offset<0||offset>=1024||offset%width)throw Error('Counter PC not aligned within original region');
 const slot=e.index*512+offset/width,p=censusConfig.base+slot*8,ov=censusConfig.overflow;
 const count=`(i64.load ${C(p)})`;
 return `(;census-begin;)(if(i32.load ${C(censusConfig.enabled)})(then(if(i64.eq ${count}(i64.const -1))(then(i64.store ${C(ov)}(i64.add(i64.load ${C(ov)})(i64.const 1))))(else(i64.store ${C(p)}(i64.add ${count}(i64.const 1)))))))(;census-end;)`;
}
'''
s=s.replace('function compileRegion(e){',helper+'function compileRegion(e){')
needle="+c.body+set('n',add(L('n'),C(1)));";assert s.count(needle)==1;s=s.replace(needle,"+c.body+set('n',add(L('n'),C(1)))+censusCommit(e,c);")
s=s.replace(' }, compileRegion };',' }, configureCensus(config){\n if(config!==null){for(const n of [config.base,config.enabled,config.overflow])if(!Number.isInteger(n)||n<=0||n>0x7fffffff||n%4)throw Error(\'Invalid census pointer\');if(config.base%8||config.overflow%8||config.base+512*512*8>0x80000000)throw Error(\'Invalid census counter extent\');}\n censusConfig=config;\n }, compileRegion };')
assert 'configureCensus(config)' in s;(p/'compiler.cjs').write_text(s)
(p/'compiler-source-pin.json').write_text(json.dumps({'originalPath':'tools/jit/compiler.cjs','originalSha256':sha(compiler.encode()),'observerSha256':sha(s.encode()),'scope':'Add enabled-gated saturating count after original generated n increment; no other emitted body alteration.'},indent=2)+'\n')
