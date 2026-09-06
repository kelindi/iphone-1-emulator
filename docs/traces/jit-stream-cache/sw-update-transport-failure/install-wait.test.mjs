import {test} from 'node:test';
import assert from 'node:assert/strict';
import {awaitInstalled} from './install-wait.mjs';
class Worker extends EventTarget{constructor(state='installing'){super();this.state=state;this.scriptURL='/service-worker.js';}change(state){this.state=state;this.dispatchEvent(new Event('statechange'));}}
test('activation waits through installed state and returns actual worker',async()=>{const worker=new Worker();const pending=awaitInstalled({installing:worker},1000);worker.change('installed');worker.change('activating');worker.change('activated');assert.deepEqual((await pending).states.map(x=>x.state),['installing','installed','activating','activated']);});
test('failed installer rejects immediately with retained states',async()=>{const worker=new Worker();const pending=awaitInstalled({installing:worker},1000);worker.change('redundant');await assert.rejects(pending,e=>e.message.includes('redundant')&&e.states.at(-1).state==='redundant');});
test('stalled installer rejects at separate bound',async()=>{await assert.rejects(awaitInstalled({installing:new Worker()},5),/exceeded 5ms/);});
test('already active works and missing worker rejects',async()=>{assert.equal((await awaitInstalled({active:new Worker('activated')})).scriptURL,'/service-worker.js');await assert.rejects(awaitInstalled({}),/no worker/);});
