import {test} from 'node:test';
import assert from 'node:assert/strict';
import {buildUnit,UNIT_LIMITS} from '../../tools/jit/adaptive/unit.mjs';
function region(base,ops,physicalBase=base){const bytes=new Uint8Array(1024).fill(255),v=new DataView(bytes.buffer);for(const [offset,word]of ops)v.setUint32(offset,word,true);return {virtualBase:base,physicalBase,version:1,bytes};}
test('connected graph crosses code windows and splits a backedge landing inside a block',()=>{
 // MOV; ADD; BNE ADD; B next-window. Two branch outcomes reconnect safely.
 const a=region(0x1000,[[0,0xe3a00000],[4,0xe2800001],[8,0x1afffffd],[12,0xea0000fb]]);
 const b=region(0x1400,[[0,0xe3a01007]]);
 const unit=buildUnit({entryPc:0x1000,regions:[a,b]});
 assert.deepEqual(unit.blocks.map(b=>b.head),[0x1000,0x1004,0x100c,0x1400]);
 assert.equal(unit.dependencies.length,2);assert.equal(unit.instructionCount,6);
 assert(unit.blocks.find(b=>b.head===0x1004).edges.every(e=>e.internal));
 a.bytes.fill(255);assert.equal(unit.blocks[0].instructions[0].raw,0xe3a00000);
 assert.throws(()=>unit.blocks[0].instructions[0].raw=0,TypeError);
});
test('unavailable branch target exits instead of inventing captured code',()=>{
 const u=buildUnit({entryPc:0x1000,regions:[region(0x1000,[[0,0xea0003fe]])]});
 assert.deepEqual(u.blocks[0].edges,[{pc:0x2000,internal:false}]);assert.equal(u.refusals[0].reason,'uncaptured');
});
test('straight-line unit obeys 512-instruction cap',()=>{
 const regions=[0x1000,0x1400,0x1800].map(base=>region(base,Array.from({length:256},(_,i)=>[4*i,0xe2800001])));
 const u=buildUnit({entryPc:0x1000,regions});assert.equal(u.instructionCount,512);assert.equal(u.blocks.length,1);assert(u.refusals.some(r=>r.reason==='instruction-limit'));
});
test('branch chain is bounded by 32 blocks',()=>{
 const r=region(0x1000,Array.from({length:100},(_,i)=>[4*i,0xeaffffff]));
 const u=buildUnit({entryPc:0x1000,regions:[r]});assert.equal(u.blocks.length,UNIT_LIMITS.blocks);assert.equal(u.refusals[0].reason,'block-limit');
});
test('physical dependencies cap at eight; inconsistent aliases are rejected',()=>{
 const regions=Array.from({length:9},(_,i)=>region(0x1000+1024*i,[[0,0xea0000fe]]));
 const u=buildUnit({entryPc:0x1000,regions});assert.equal(u.dependencies.length,8);assert.equal(u.refusals[0].reason,'granule-limit');
 const a=region(0x1000,[[0,0xe3a00001]]),b=region(0x2000,[[0,0xe3a00002]],0x1000);
 assert.throws(()=>buildUnit({entryPc:0x1000,regions:[a,b]}),/aliases/);
});
test('invalid capture alignment and duplicate virtual windows are rejected',()=>{
 const r=region(0x1000,[]);assert.throws(()=>buildUnit({entryPc:0x1002,regions:[r]}));
 assert.throws(()=>buildUnit({entryPc:0x1000,regions:[r,r]}),/Duplicate/);
});
test('consistent virtual aliases share a physical dependency but retain virtual PCs',()=>{
 const a=region(0x1000,[[0,0xea0003fe]]),b=region(0x2000,[[0,0xea0003fe]],0x1000);
 const u=buildUnit({entryPc:0x1000,regions:[a,b]});assert.equal(u.dependencies.length,1);
 assert.deepEqual(u.blocks.map(b=>b.head),[0x1000,0x2000]);
 assert.equal(u.blocks[0].instructions[0].physicalAddress,u.blocks[1].instructions[0].physicalAddress);
});
test('conditional diamond joins create block heads without duplicate instructions',()=>{
 const r=region(0x1000,[[0,0x1a000000],[4,0xe3a00001],[8,0xe3a01002]]);
 const u=buildUnit({entryPc:0x1000,regions:[r]}),heads=new Set(u.blocks.map(b=>b.head));
 assert(heads.has(0x1008));const pcs=u.blocks.flatMap(b=>b.instructions.map(n=>n.pc));assert.equal(new Set(pcs).size,pcs.length);
 for(const b of u.blocks)for(const e of b.edges)if(e.internal)assert(heads.has(e.pc),'Internal edge must reach a block head');
});
test('conditional BX retains both dynamic taken exit and static fallthrough',()=>{
 const u=buildUnit({entryPc:0x1000,regions:[region(0x1000,[[0,0x112fff13],[4,0xe3a00001]])]});
 assert.deepEqual(u.blocks[0].exits,[{kind:'dynamic',condition:1,register:3,exchange:true}]);
 assert.deepEqual(u.blocks[0].edges,[{pc:0x1004,internal:true}]);
 const tail=u.blocks.at(-1);assert.deepEqual(tail.exits,[{kind:'before-instruction',pc:0x1008,reason:'unsupported'}]);
});
test('loads remain sequential and obey unit bounds alongside arithmetic',()=>{
 const regions=[0x1000,0x1400,0x1800].map(base=>region(base,Array.from({length:256},(_,i)=>[4*i,i%2?0xe5910000:0xe2802001])));
 const unit=buildUnit({entryPc:0x1000,regions});assert.equal(unit.instructionCount,512);assert.equal(unit.blocks.length,1);
 assert.equal(unit.blocks[0].instructions.filter(n=>n.op==='load').length,256);assert(unit.refusals.some(r=>r.reason==='instruction-limit'));
});
test('mixed stores/loads/arithmetic retain the512instruction straight-line cap',()=>{
 const ops=[0xe5810000,0xe5910000,0xe2802001];
 const regions=[0x1000,0x1400,0x1800].map(base=>region(base,Array.from({length:256},(_,i)=>[4*i,ops[i%3]])));
 const u=buildUnit({entryPc:0x1000,regions});assert.equal(u.instructionCount,512);assert.equal(u.blocks.length,1);assert(u.blocks[0].instructions.some(n=>n.op==='store'));
});
