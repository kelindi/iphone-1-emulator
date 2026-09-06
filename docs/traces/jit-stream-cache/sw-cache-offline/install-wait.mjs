// Diagnostic setup only: never shipped with the phone or service worker.
export function awaitInstalled(registration, timeoutMs=30000) {
  return new Promise((resolve,reject)=>{
    const worker=registration.installing||registration.waiting||registration.active;
    if(!worker){reject(Error('Registration has no worker'));return;}
    const states=[];
    const finish=(error)=>{clearTimeout(timer);worker.removeEventListener('statechange',changed);error?reject(Object.assign(error,{states})):resolve({states,scriptURL:worker.scriptURL});};
    const changed=()=>{states.push({at:Date.now(),state:worker.state});if(worker.state==='activated')finish();else if(worker.state==='redundant')finish(Error('Offline installation became redundant'));};
    const timer=setTimeout(()=>finish(Error('Offline installation exceeded '+timeoutMs+'ms')),timeoutMs);
    worker.addEventListener('statechange',changed);changed();
  });
}
