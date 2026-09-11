import test from 'node:test';
import assert from 'node:assert/strict';
import {validateSwipe,swipeScreen} from './qualify-browser.mjs';

test('swipe defaults and exact boundary coordinates are accepted without input aliasing',()=>{
 const value={from:[0,0],to:[319.9,479.9]};const parsed=validateSwipe(value);
 assert.deepEqual(parsed,{...value,steps:20,duration:600});value.from[0]=300;assert.equal(parsed.from[0],0);
 assert.equal(validateSwipe({from:[0,0],to:[1,1],steps:120,duration:3000}).steps,120);
});
test('invalid endpoints, durations and motion density fail before any page interaction',async()=>{
 const valid={from:[10,20],to:[300,450]};
 const bad=[null,[],{},...[[NaN,0],[Infinity,0],[-1,0],[320,0],[0,480],[0,-1],[1],[1,2,3],['1',2]].map(from=>({...valid,from})),
  ...[0,1,121,2.5,NaN,Infinity].map(steps=>({...valid,steps})),
  ...[0,99,3001,100.5,NaN,Infinity].map(duration=>({...valid,duration})),
  {...valid,steps:120,duration:100},{...valid,to:[0,480]}];
 for(const value of bad){let touched=false;await assert.rejects(swipeScreen({locator(){touched=true;throw Error('Unexpected page access');}},value));assert.equal(touched,false);}
});
test('motion uses screen-scaled mouse down/moves/up and bounded delay, without guest evaluation',async()=>{
 const events=[];const page={locator:selector=>{assert.equal(selector,'#screen');return {boundingBox:async()=>({x:10,y:20,width:640,height:960})};},
  mouse:{move:async(x,y)=>events.push(['move',x,y]),down:async()=>events.push(['down']),up:async()=>events.push(['up'])}};
 const result=await swipeScreen(page,{from:[20,100],to:[300,100],steps:2,duration:100},async ms=>events.push(['wait',ms]));
 assert.deepEqual(events,[['move',50,220],['down'],['wait',50],['move',330,220],['wait',50],['move',610,220],['up']]);
 assert.equal(result.steps,2);assert.equal(result.duration,100);
});
test('move failure still attempts release and propagates the failure',async()=>{
 let moves=0,releases=0;const page={locator:()=>({boundingBox:async()=>({x:0,y:0,width:320,height:480})}),
  mouse:{move:async()=>{if(++moves===2)throw Error('motion failed');},down:async()=>{},up:async()=>{releases++;}}};
 await assert.rejects(swipeScreen(page,{from:[0,0],to:[100,100],steps:2,duration:100},async()=>{}),/motion failed/);
 assert.equal(releases,1);
});
test('missing or invalid screen bounds fail without mouse events',async()=>{
 for(const box of [null,{x:0,y:0,width:0,height:480},{x:NaN,y:0,width:320,height:480}]){
  await assert.rejects(swipeScreen({locator:()=>({boundingBox:async()=>box}),mouse:{}},{from:[0,0],to:[1,1]}),/bounding box/);
 }
});
