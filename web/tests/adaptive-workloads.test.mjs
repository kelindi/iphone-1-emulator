import {test} from 'node:test';
import assert from 'node:assert/strict';
import {workloads,validateWorkload,nextBoundary} from '../../tools/jit/adaptive/workloads.mjs';
test('fixed work stops exactly at inputs/checkpoints even across chunk boundaries',()=>{
 const w={credits:23,events:[{at:0,type:'touch',down:1,x:5,y:5},{at:7,type:'touch',down:0,x:5,y:5}],checkpoints:[11,23]};validateWorkload(w);
 const visited=[];for(let n=0;n<w.credits;){n=nextBoundary(w,n,10);visited.push(n);}assert.deepEqual(visited,[7,11,21,23]);
});
test('workloads reject reordered inputs and missing final checkpoint',()=>{
 const w=structuredClone(workloads[0]);w.events.reverse();assert.throws(()=>validateWorkload(w),/order/);
 const x=structuredClone(workloads[1]);x.checkpoints.pop();assert.throws(()=>validateWorkload(x),/checkpoints/);
});
