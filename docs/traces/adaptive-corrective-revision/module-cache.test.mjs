import test from 'node:test';
import assert from 'node:assert/strict';
import {createModuleCache} from '../module-cache.mjs';
const module=new WebAssembly.Module(Uint8Array.from([0,97,115,109,1,0,0,0]));
test('exact keys, no accidental equivalence and LRU entry eviction',()=>{
 const c=createModuleCache({entries:2,bytes:100});
 c.put('a',module,8);c.put('b',module,8);assert.equal(c.get('a').module,module);
 assert.equal(c.get('a '),undefined);c.put('c',module,8);
 assert.equal(c.get('b'),undefined);assert.equal(c.get('a').module,module);
 assert.deepEqual(c.stats,{hits:2,misses:2,evictions:1,bytes:20,entries:2});
});
test('byte limit accounts WAT and binary; oversize artifacts are not retained',()=>{
 const c=createModuleCache({entries:8,bytes:20});
 c.put('a',module,8);c.put('bb',module,8);assert.equal(c.stats.bytes,12);
 assert.equal(c.get('a'),undefined);assert.equal(c.put('oversize',module,8),false);
 assert.equal(c.stats.bytes,12);assert.equal(c.stats.entries,1);
 assert.equal(c.get('bb').module,module);
});
test('replacement accounting, clear and owner isolation',()=>{
 const a=createModuleCache(),b=createModuleCache();a.put('x',module,8);a.put('x',module,12);
 assert.equal(a.stats.bytes,14);assert.equal(a.stats.entries,1);assert.equal(b.get('x'),undefined);
 a.clear();assert.equal(a.stats.bytes,0);assert.equal(a.stats.entries,0);assert.equal(a.get('x'),undefined);
});
test('reject invalid limits and nonmodule artifacts',()=>{
 assert.throws(()=>createModuleCache({entries:0}));const c=createModuleCache();
 assert.throws(()=>c.put('x',{},8));assert.throws(()=>c.put('x',module,-1));assert.throws(()=>c.get(3));
});
