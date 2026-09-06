import test from 'node:test';import assert from 'node:assert/strict';import {compareRaw} from './frames.mjs';
const a=()=>new Uint8Array(320*480*4).fill(100);
test('fixed full-content screen oracle accepts exact and two-level RGB, rejects three-level even in scrollbar',()=>{const x=a(),y=a();assert(compareRaw(x,y).passed);y[(100*320+295)*4]+=2;assert(compareRaw(x,y).passed);y[(100*320+295)*4]++;const r=compareRaw(x,y);assert(!r.passed);assert.equal(r.pixelsOver2,1);assert.deepEqual(r.bounds,[295,100,295,100]);});
test('statusrow exclusion ends exactly at rawrow30; alpha below cutoff stays exact',()=>{const x=a(),y=a();y[29*320*4]+=100;assert(compareRaw(x,y).passed);y[30*320*4+3]++;assert(!compareRaw(x,y).passed);});
test('shape changes cannot silently pass',()=>assert.throws(()=>compareRaw(a(),new Uint8Array(10)),/shape/));
