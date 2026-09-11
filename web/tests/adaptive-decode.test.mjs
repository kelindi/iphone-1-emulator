import {test} from 'node:test';
import assert from 'node:assert/strict';
import {decodeInstruction as decode} from '../../tools/jit/adaptive/decode.mjs';
test('ARM branch sign extension, link address, conditional fallthrough and wrap',()=>{
 assert.deepEqual(decode(0x1afffffc,0x1000).successors,[0xff8,0x1004]);
 const bl=decode(0xeb000000,0xfffffffc);assert.equal(bl.target,4);assert.equal(bl.returnAddress,0);assert.equal(bl.link,true);
 assert.equal(decode(0xfa000000,0x1000).op,'fallback');
});
test('Thumb branches distinguish reserved/SWI and preserve signed offsets',()=>{
 assert.deepEqual(decode(0xd1fe,0x1000,true).successors,[0x1000,0x1002]);
 assert.equal(decode(0xe7fc,0x1000,true).target,0xffc);
 for(const raw of [0xde00,0xdf00,0xf000])assert.equal(decode(raw,0x1000,true).op,'fallback');
});
test('immediate IR preserves rotated shifter carry and safe fallback boundaries',()=>{
 const mov=decode(0xe3a00102,0x1000);assert.equal(mov.op,'alu');assert.equal(mov.operand.value,0x80000000);assert.equal(mov.operand.carry,1);
 assert.equal(decode(0xe3a000ff,0x1000).operand.carry,'preserve');
 for(const raw of [0xe328f001,0xe3a0f000,0xe7800000])assert.equal(decode(raw,0x1000).op,'fallback');
 assert.equal(decode(0x22ff,0x1000,true).rd,2);
});
test('decoder rejects truncated instruction words and unaligned addresses',()=>{
 assert.throws(()=>decode(0x10000,0x1000,true));assert.throws(()=>decode(0,0x1002));
});
test('ARM load forms preserve no-op alias exception and reject unsafe destinations',()=>{
 const normal=decode(0xe5b10004,0);assert.equal(normal.op,'load');assert.equal(normal.writeback,true);assert.equal(normal.preIndex,true);
 const alias=decode(0xe4900000,0);assert.equal(alias.op,'load');assert.equal(alias.writeback,false);
 const translated=decode(0xe4b10004,0);assert.equal(translated.unprivileged,true);assert.equal(translated.preIndex,false);
 for(const raw of [0xe5b00000,0xe4900004,0xe4b00000,0xe59ff000,0xe49f0000,0xe7910002,0xe7810000])assert.equal(decode(raw,0).op,'fallback');
});
test('ARM and Thumb immediate stores retain PC/writeback safety rules',()=>{
 assert.equal(decode(0xe581f000,0).op,'store');assert.equal(decode(0xe5800000,0).op,'store');
 const translated=decode(0xe4a10004,0);assert.equal(translated.op,'store');assert.equal(translated.unprivileged,true);assert.equal(translated.writeback,true);
 for(const raw of [0xe5c1f000,0xe5a00000,0xe4800000,0xe48f0000,0xe7810002,0xe8810001])assert.equal(decode(raw,0).op,'fallback');
 assert.equal(decode(0x6008,0,true).op,'store');assert.equal(decode(0x7008,0,true).bytes,1);assert.equal(decode(0x6008,0,true).bytes,4);
});
