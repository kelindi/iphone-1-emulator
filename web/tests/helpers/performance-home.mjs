// Evaluate this self-contained function in either Node tests or the page.
export function classifyPerformanceScreen(pixels) {
  pixels ??= document.querySelector('#screen').getContext('2d').getImageData(0,0,320,480).data;
  if (pixels.length !== 320*480*4) throw Error('Expected a 320x480 RGBA guest screen');
  const count=(x,y,w,h,predicate)=>{let n=0;for(let row=y;row<y+h;row++)for(let col=x;col<x+w;col++){const i=(row*320+col)*4;if(predicate(pixels[i],pixels[i+1],pixels[i+2]))n++;}return n;};
  const green=count(20,40,60,60,(r,g,b)=>g>r+25&&g>b+25);
  const mailBlue=count(90,405,60,60,(r,g,b)=>b>r+25&&b>g);
  const earthBlue=count(50,130,220,240,(r,g,b)=>b>r+30&&b>g);
  const sliderWhite=count(26,414,60,20,(r,g,b)=>r>180&&g>180&&b>180);
  const lit=count(0,30,320,450,(r,g,b)=>Math.max(r,g,b)>25);
  const kind=green>500&&mailBlue>300?'home':earthBlue>5000&&sliderWhite>500?'locked':lit===0?'asleep':'unknown';
  return {kind,green,mailBlue,earthBlue,sliderWhite,lit};
}

export async function ensurePerformanceHome(page,{screenshot}={}) {
  const began=Date.now(),events=[];
  const waitFor=async kinds=>{const deadline=Date.now()+20000;let state;do{state=await page.evaluate(classifyPerformanceScreen);if(kinds.includes(state.kind))return state;await page.waitForTimeout(100);}while(Date.now()<deadline);throw Error('Benchmark requires verified Home or lock screen; observed '+JSON.stringify(state));};
  let state=await waitFor(['home','locked','asleep']);events.push({event:'initial',state});
  if(state.kind==='asleep'){
    await page.locator('[data-button="1"]').click({delay:180});
    state=await waitFor(['home','locked']);events.push({event:'wake',state});
  }
  if(state.kind==='locked'){
    const box=await page.locator('#screen').boundingBox();
    const point=x=>({x:box.x+x*box.width/320,y:box.y+430*box.height/480});
    await page.mouse.move(point(45).x,point(45).y);await page.mouse.down();
    try{for(let i=1;i<=24;i++){const p=point(45+240*i/24);await page.mouse.move(p.x,p.y);await page.waitForTimeout(60);}}
    finally{await page.mouse.up();}
    events.push({event:'slide',state:await waitFor(['home'])});
  }
  await page.waitForTimeout(1200);
  state=await page.evaluate(classifyPerformanceScreen);
  if(state.kind!=='home')throw Error('Home did not remain ready: '+JSON.stringify(state));
  if(screenshot)await page.locator('#screen').screenshot({path:screenshot});
  return {events,verified:state,preparationMs:Date.now()-began};
}
