/** Genuine OS1 Calculator acceptance. No screen is recreated or replaced.
 * BASE_URL=http://localhost:8080 ARTIFACT_DIR=.context/calculator-production node web/tests/calculator.mjs --record
 * Reviewed display hashes are a framebuffer oracle independent of input events.
 * --capture collects discovery evidence only; it never marks cases as passing.
 */
import {chromium} from 'playwright';
import {mkdir,readFile,writeFile} from 'node:fs/promises';
import {resolve} from 'node:path';
const dir=resolve(process.env.ARTIFACT_DIR || '.context/calculator-production');
await mkdir(dir,{recursive:true});
const captureOnly=process.argv.includes('--capture');
const references=captureOnly?null:JSON.parse(await readFile(new URL('./include/calculator-displays.json',import.meta.url),'utf8'));
const cases=[
 {name:'clear',keys:['1','2','3','C'],expected:'0'},
 {name:'addition',keys:['C','1','2','+','3','4','='],expected:'46'},
 {name:'negative-subtraction',keys:['C','9','-','1','4','='],expected:'-5'},
 {name:'multiplication',keys:['C','7','*','8','='],expected:'56'},
 {name:'division',keys:['C','8','1','/','9','='],expected:'9'},
 {name:'fractional-division',keys:['C','1','/','4','='],expected:'0.25'},
 {name:'decimal-addition',keys:['C','1','.','5','+','2','.','2','5','='],expected:'3.75'},
 {name:'decimal-quotient',keys:['C','1','0','/','4','='],expected:'2.5'},
 {name:'negative-to-positive',keys:['C','0','-','7','=','+','1','0','='],expected:'3'},
 {name:'repeated-digits',keys:['C','1','1','1','+','2','2','2','='],expected:'333'},
 {name:'chained-calculation',keys:['C','2','+','3','=','*','4','='],expected:'20'},
 {name:'clear-and-reuse',keys:['C','9','8','7','6','5','C','6','*','7','='],expected:'42'},
];
const keys={'7':[40,224],'8':[120,224],'9':[200,224],'*':[280,224],
 '4':[40,295],'5':[120,295],'6':[200,295],'+':[280,295],
 '1':[40,366],'2':[120,366],'3':[200,366],'-':[280,366],
 '0':[40,437],'.':[120,437],'C':[200,437],'=':[280,437],'/':[280,152]};
const browser=await chromium.launch({executablePath:process.env.CHROME_PATH || '/usr/bin/google-chrome',args:['--no-sandbox','--disable-dev-shm-usage']});
const context=await browser.newContext({viewport:{width:1280,height:1050},...(process.argv.includes('--record')?{recordVideo:{dir,size:{width:1280,height:1050}}}:{})});
const page=await context.newPage();const video=page.video();const began=performance.now();
const report={date:new Date().toISOString(),url:process.env.BASE_URL || 'http://localhost:8080',mode:'production default firmware and CPU configuration, disposable profile',captureOnly,reviewRequired:captureOnly,cases:[],errors:[],limitations:['No visible sign-toggle or backspace control exists on this original keypad.','Fixed input waits establish functionality, not speed.','Memory keys and saved Calculator state are outside this bounded workflow.']};
page.on('pageerror',error=>report.errors.push(error.message));
const save=()=>writeFile(resolve(dir,'report.json'),JSON.stringify(report,null,2)+'\n');
async function tap(point){const b=await page.locator('#screen').boundingBox();await page.mouse.move(b.x+point[0]*b.width/320,b.y+point[1]*b.height/480);await page.mouse.down();await page.waitForTimeout(50);await page.mouse.up();await page.waitForTimeout(500);}
async function screen(name,expected,saveImages=true){
 const reference=!captureOnly&&expected!==undefined?references.displays[expected]:null;
 if(!captureOnly&&expected!==undefined&&!reference)throw Error(`Missing reviewed Calculator reference: ${expected}`);
 const referencePNG=reference?`data:image/png;base64,${(await readFile(new URL(`./include/${reference.image}`,import.meta.url))).toString('base64')}`:null;
 const data=await page.evaluate(async ({referencePNG,saveImages})=>{
  const c=document.querySelector('#screen'),pixels=c.getContext('2d').getImageData(0,20,320,88);
  const d=document.createElement('canvas');d.width=320;d.height=88;d.getContext('2d').putImageData(pixels,0,0);
  const hash=async bytes=>[...new Uint8Array(await crypto.subtle.digest('SHA-256',bytes))].map(n=>n.toString(16).padStart(2,'0')).join('');
  let comparison=null;
  if(referencePNG){
   const img=new Image();img.src=referencePNG;await img.decode();
   if(img.width!==320||img.height!==88)throw Error('Reviewed Calculator reference dimensions changed');
   const ref=document.createElement('canvas');ref.width=320;ref.height=88;ref.getContext('2d').drawImage(img,0,0);
   const bytes=ref.getContext('2d').getImageData(0,0,320,88).data;
   let maxRGBDelta=0,changedPixels=0,alphaExact=true;
   for(let i=0;i<bytes.length;i+=4){
    const delta=Math.max(...[0,1,2].map(ch=>Math.abs(pixels.data[i+ch]-bytes[i+ch])));
    maxRGBDelta=Math.max(maxRGBDelta,delta);alphaExact&&=pixels.data[i+3]===bytes[i+3];
    if(delta||pixels.data[i+3]!==bytes[i+3])changedPixels++;
   }
   // Independent review of all 12 displays found two antialiased glyph pixels
   // whose green channel differs by two levels under MBX; all other changed
   // channels differ by one. Keep the original references and exact alpha.
   comparison={pixels:bytes.length/4,referenceHash:await hash(bytes),maxRGBDelta,changedPixels,alphaExact,exact:changedPixels===0,passed:maxRGBDelta<=2&&alphaExact};
  }
  return{displayHash:await hash(pixels.data),comparison,...(saveImages?{png:c.toDataURL('image/png'),display:d.toDataURL('image/png')}:{}),jit:window.iphone.metrics.jit};
 },{referencePNG,saveImages});
 if(reference&&data.comparison.referenceHash!==reference.sha256)throw Error('Reviewed Calculator reference PNG does not match its recorded hash');
 if(saveImages)for(const [suffix,value] of [['guest',data.png],['display',data.display]])await writeFile(resolve(dir,`${name}-${suffix}.png`),Buffer.from(value.split(',')[1],'base64'));
 delete data.png;delete data.display;return data;
}
try{
 await page.goto(report.url);await page.waitForFunction(()=>window.iphone?.ready,null,{timeout:120000});
 await tap([120,250]);
 // A fixed delay can catch the genuine Calculator while it is still drawing.
 // Poll the independent reviewed pixels; retain the original short input and
 // a hard deadline. These waits establish functionality, not launch speed.
 if(!captureOnly){
  const beganWaiting=performance.now();let matches=0;
  report.openingSamples=[];
  do{
   const sample=await screen('00-open','0',false);
   report.openingSamples.push({elapsedMs:performance.now()-beganWaiting,...sample});
   matches=sample.comparison.passed?matches+1:0;
   if(matches===2)break;
   await page.waitForTimeout(250);
  }while(performance.now()-beganWaiting<30000);
  report.openingReady=matches===2;
 }else await page.waitForTimeout(7500);
 const opened=await screen('00-open','0');
 report.opened=opened;
 if(!captureOnly&&(!report.openingReady||!opened.comparison.passed))
  throw new Error('Calculator did not reach its reviewed initial display; inspect 00-open-guest.png');
 for(const [index,test] of cases.entries()){
  const name=String(index+1).padStart(2,'0')+'-'+test.name;
  for(const key of test.keys)await tap(keys[key]);
  await page.waitForTimeout(1300);const result={...test,...await screen(name,test.expected),videoSeconds:(performance.now()-began)/1000};
  if(!captureOnly){result.passed=result.comparison.passed;if(!result.passed)report.errors.push(`${name}: display differs from reviewed ${test.expected} reference`);}
  report.cases.push(result);await save();console.log(name,JSON.stringify({expected:result.expected,hash:result.displayHash,passed:result.passed}));
 }
 await page.waitForFunction(()=>window.iphone.metrics.jit?.enabled&&window.iphone.metrics.jit.retired>0,null,{timeout:15000});
 report.jit=await page.evaluate(()=>window.iphone.metrics.jit);report.jitPassed=report.jit.enabled&&report.jit.retired>0;
 await page.screenshot({path:resolve(dir,'complete.png')});
 report.captureComplete=report.errors.length===0;
 report.passed=!captureOnly&&report.captureComplete&&report.jitPassed&&report.cases.every(x=>x.passed);
}catch(error){report.errors.push(error.stack);report.passed=false;await page.screenshot({path:resolve(dir,'failure.png')}).catch(()=>{});}
finally{await save();await context.close();if(video)report.video=await video.path();await browser.close();await save();}
if(!captureOnly&&!report.passed||report.errors.length)process.exitCode=1;
