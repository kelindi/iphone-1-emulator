// Inputs are scheduled at exact _emu_run budget boundaries, not wall-clock time.
// These are scheduler credits; exceptions can consume credit without fetching.
const tap=(at,x,y)=>[{at,type:'touch',down:1,x,y},{at:at+5000000,type:'touch',down:0,x,y}];
export const workloads=[
 {name:'settings',events:[...tap(0,275,250)]},
 {name:'contacts',events:[...tap(0,43,425),...tap(200000000,160,454)]},
 {name:'notes',events:[...tap(0,200,250)]}
].map(w=>({...w,credits:600000000,checkpoints:[100000000,300000000,600000000],events:[...w.events,{at:400000000,type:'button',id:1,down:1},{at:405000000,type:'button',id:1,down:0}]}));

export function validateWorkload(w){
 if(!Number.isSafeInteger(w.credits)||w.credits<1)throw Error('Invalid total credit budget');
 let last=-1;
 for(const e of w.events){
  if(!Number.isSafeInteger(e.at)||e.at<last||e.at<0||e.at>=w.credits)throw Error('Invalid input order/boundary');last=e.at;
  if(![0,1].includes(e.down))throw Error('Invalid input state');
  if(e.type==='touch'){if(!Number.isInteger(e.x)||!Number.isInteger(e.y)||e.x<0||e.x>=320||e.y<0||e.y>=480)throw Error('Invalid touch');}
  else if(e.type!=='button'||e.id!==1)throw Error('Invalid button');
 }
 if(!w.checkpoints.length||w.checkpoints.at(-1)!==w.credits||w.checkpoints.some((v,i)=>!Number.isSafeInteger(v)||v<=0||v>w.credits||(i&&v<=w.checkpoints[i-1])))throw Error('Invalid checkpoints');
 return w;
}
export function nextBoundary(w,done,chunk=1000000){
 return Math.min(w.credits,done+chunk,...w.events.filter(e=>e.at>done).map(e=>e.at),...w.checkpoints.filter(n=>n>done));
}
