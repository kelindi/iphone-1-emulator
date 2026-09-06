from pathlib import Path
import shutil
p=Path('.context/jit-mixed-coverage');c=p/'candidate';t=c/'tests';t.mkdir(exist_ok=True)
shutil.copy('web/emu-worker.js',c/'emu-worker.js')
s=Path('web/tests/jit-lifecycle.test.mjs').read_text()
s+='''
const fullDefs = n => Array.from({length:n},(_,i)=>region(i*1024,i));
test('576 distinct regions install and reset without table leaks',async()=>{const x=setup({defs:fullDefs(576)}),c=x.create();await c.ready;assert.equal(c.enabled,true);assert.equal(x.active.size,576);await c.reset();assert.equal(x.active.size,576);c.disable(Error('test teardown'));assert.equal(x.active.size,0);assert.equal(x.allocated.size,0);});
test('577 definitions reject before any table function allocation',async()=>{const x=await rejected({defs:fullDefs(577)});assert.equal(x.events.filter(e=>e[0]==='add').length,0);});
test('576th failed admission reclaims all earlier and just-created slots',async()=>{const x=await rejected({defs:fullDefs(576),installFail:576});assert.equal(x.events.filter(e=>e[0]==='add').length,576);assert.equal(x.events.filter(e=>e[0]==='remove').length,576);});
test('576 queued invalidations are reclaimed exactly once, then reset can install again',async()=>{const x=setup({defs:fullDefs(576)}),c=x.create();await c.ready;let pending=[...x.active.keys()];x.api._emu_jit_retired=p=>{x.api.HEAPU32.set(pending,p>>>2);const n=pending.length;pending=[];return n;};c.pump();assert.equal(x.active.size,0);c.pump();assert.equal(x.active.size,0);await c.reset();assert.equal(x.active.size,576);c.disable(Error('test teardown'));assert.equal(x.allocated.size,0);});
'''
(t/'jit-lifecycle.test.mjs').write_text(s)
s=Path('web/tests/jit-loader.test.mjs').read_text().replace('[33554432,512,8000]','[16777216,576,8000]').replace('32*1024*1024+1','16*1024*1024+1');(t/'jit-loader.test.mjs').write_text(s)
s=Path('.context/jit-fallback-census/oracle.c').read_text();a=s.index('#ifdef FC_ORACLE');b=s.index('#define original_try');s='#include "jit-runtime.c"\n'+s[b:];(c/'oracle.c').write_text(s)
shutil.copy('.context/jit-fallback-census/test-boundaries.cjs',p/'test-boundaries.cjs')
