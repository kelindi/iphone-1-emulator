/* Harness-only observation. Original Worker and putImageData calls retain order,
 * arguments, return values and exceptions. No guest/frame scheduling changes. */
(() => {
  const nativePut = CanvasRenderingContext2D.prototype.putImageData;
  const NativeWorker = window.Worker;
  let active = null, pending = null, lastCanvas = null, emuWorker = null;
  let targets = null;
  const errors = [];
  const fail = message => { if (errors.length < 32) errors.push(String(message)); };
  const crop = (bytes, x, y, w, h) => {
    if (bytes.length !== 320 * 480 * 4) throw Error('Source framebuffer dimensions');
    const result = new Uint8Array(w * h * 4);
    for (let row = 0; row < h; row++) result.set(bytes.subarray(((y+row)*320+x)*4,((y+row)*320+x+w)*4),row*w*4);
    return result;
  };
  const same = (a,b) => a.length===b.length && a.every((v,i)=>v===b[i]);
  const changed = (a,b) => {
    if (a.length!==6400 || b.length!==6400) throw Error('Button crop dimensions');
    let n=0;for(let i=0;i<a.length;i+=4)if(Math.max(Math.abs(a[i]-b[i]),Math.abs(a[i+1]-b[i+1]),Math.abs(a[i+2]-b[i+2]))>12)n++;return n;
  };
  const comparison = (bytes,value) => {
    const ref=targets?.[value];if(!ref || bytes.length!==112640)throw Error('Display/reference dimensions');
    let maxRGBDelta=0,alphaExact=true;
    for(let i=0;i<bytes.length;i+=4){for(let k=0;k<3;k++)maxRGBDelta=Math.max(maxRGBDelta,Math.abs(bytes[i+k]-ref[i+k]));alphaExact&&=bytes[i+3]===ref[i+3];}
    return {passed:maxRGBDelta<=2&&alphaExact,maxRGBDelta,alphaExact};
  };
  const screen = () => document.querySelector('#screen');
  const read = (ctx,x,y,w,h) => new Uint8Array(ctx.getImageData(x,y,w,h).data);
  const evidence = (bytes,frame,at,source=null) => ({bytes,frame,at,...(source?{sourceBytes:source}:{} )});
  const note = (a,row) => {if(a.frames.length>=1024)throw Error('Per-press frame receipt bound');a.frames.push(row);};
  function received(worker,event) {
    if(event.data?.type!=='frame')return;
    const receivedAt=performance.now();
    try {
      if(worker!==emuWorker)throw Error('Wrong emulator worker');
      const {buffer,sequence}=event.data;
      if(!(buffer instanceof ArrayBuffer)||buffer.byteLength!==614400||!Number.isSafeInteger(sequence)||sequence<1)throw Error('Invalid actual frame source');
      if(pending)throw Error('Prior frame not consumed by original canvas handler');
      if(lastCanvas&&sequence<=lastCanvas.sequence)throw Error('Non-increasing actual frame sequence');
      const row={sequence,receivedAt,sourceBytes:buffer.byteLength};
      if(active&&active.down!==null){const bytes=new Uint8Array(buffer);row.button=crop(bytes,active.x-20,active.y-20,40,40);row.display=crop(bytes,0,20,320,88);}
      row.sourceCopiedAt=performance.now();pending=row;
    }catch(e){fail(e.message);}
  }
  window.Worker=new Proxy(NativeWorker,{construct(Target,args,newTarget){
    const worker=Reflect.construct(Target,args,newTarget);
    if(new URL(String(args[0]),location.href).pathname.endsWith('/emu-worker.js')){
      if(emuWorker)fail('Multiple emulator workers');emuWorker=worker;
      // Constructor listener is registered before production's onmessage.
      worker.addEventListener('message',event=>received(worker,event));
    }
    return worker;
  }});
  CanvasRenderingContext2D.prototype.putImageData=function(...args){
    const relevant=this.canvas===screen(),beforeNativeAt=performance.now();
    const result=Reflect.apply(nativePut,this,args),canvasDoneAt=performance.now();
    if(relevant)try{
      const source=pending;pending=null;
      if(!source)throw Error('Canvas write without actual frame receipt');
      if(args.length!==3||args[1]!==0||args[2]!==0||args[0]?.data?.length!==614400)throw Error('Unexpected production frame write shape');
      const row={sequence:source.sequence,receivedAt:source.receivedAt,sourceCopiedAt:source.sourceCopiedAt,beforeNativeAt,canvasDoneAt};
      lastCanvas=row;
      const a=active;
      if(a&&a.down!==null){
        const button=read(this,a.x-20,a.y-20,40,40),display=read(this,0,20,320,88);
        if(!source.button||!same(button,source.button)||!same(display,source.display))throw Error('Source/canvas ROI mismatch');
        row.sourceCanvasExact=true;row.changed=changed(button,a.baseline.bytes);row.comparison=comparison(display,a.value);
        if(!a.firstButton&&row.changed>=16)a.firstButton={...row,crop:evidence(button,row.sequence,canvasDoneAt,source.button)};
        if(!a.firstResult&&row.comparison.passed)a.firstResult={...row,crop:evidence(display,row.sequence,canvasDoneAt,source.display)};
        a.latestDisplay={bytes:display,sourceBytes:source.display,sequence:row.sequence};
        row.observerDoneAt=performance.now();note(a,row);
      }
    }catch(e){fail(e.message);}
    return result;
  };
  const api=window.deliveryObserver={errors,result:null,
    configure(values){targets=Object.fromEntries(Object.entries(values).map(([k,v])=>[k,new Uint8Array(v)]));for(const v of Object.values(targets))if(v.length!==112640)throw Error('Reference shape');},
    match(value){return comparison(read(screen().getContext('2d'),0,20,320,88),value);},
    arm(key,value,x,y){
      if(active)throw Error('Input still active');if(errors.length)throw Error(errors.join('; '));
      if(!api.match(value==='9'?'0':'9').passed)throw Error('Wrong prior display');
      const canvas=screen();if(!lastCanvas||String(lastCanvas.sequence)!==canvas.dataset.frame)throw Error('Baseline frame association');
      const now=performance.now();api.result=null;
      active={key,value,x,y,down:null,up:null,armedAt:now,baseline:evidence(read(canvas.getContext('2d'),x-20,y-20,40,40),lastCanvas.sequence,now),frames:[],raf:[],lastRafFrame:canvas.dataset.frame,firstButton:null,firstResult:null,rafFirstButton:null,rafFirstResult:null,latestDisplay:null};
    },
    async takeResult(){
      if(!api.result)throw Error('No completed result');
      async function pack(value){
        if(value instanceof Uint8Array){let text='';for(let i=0;i<value.length;i+=32768)text+=String.fromCharCode(...value.subarray(i,i+32768));const digest=await crypto.subtle.digest('SHA-256',value);return {base64:btoa(text),sha256:[...new Uint8Array(digest)].map(x=>x.toString(16).padStart(2,'0')).join(''),byteLength:value.length};}
        if(typeof value==='bigint')return String(value);
        if(Array.isArray(value))return Promise.all(value.map(pack));
        if(value&&typeof value==='object')return Object.fromEntries(await Promise.all(Object.entries(value).map(async([k,v])=>[k,await pack(v)])));
        return value;
      }
      return pack(api.result);
    }
  };
  document.addEventListener('pointerdown',e=>{if(e.target===screen()&&active&&active.down===null)active.down=performance.now();},{capture:true});
  document.addEventListener('pointerup',e=>{if(e.target===screen()&&active&&active.up===null)active.up=performance.now();},{capture:true});
  function sample(){
    const a=active;
    if(a&&a.down!==null)try{
      const now=performance.now(),canvas=screen(),ctx=canvas.getContext('2d'),frame=canvas.dataset.frame;
      if(frame!==a.lastRafFrame){
        if(!lastCanvas||String(lastCanvas.sequence)!==frame)throw Error('RAF frame association');
        a.lastRafFrame=frame;const button=read(ctx,a.x-20,a.y-20,40,40),display=read(ctx,0,20,320,88),row={at:now,sequence:Number(frame),canvasDoneAt:lastCanvas.canvasDoneAt,changed:changed(button,a.baseline.bytes),comparison:comparison(display,a.value)};
        if(!a.rafFirstButton&&row.changed>=16)a.rafFirstButton={...row,crop:evidence(button,row.sequence,now)};
        if(!a.rafFirstResult&&row.comparison.passed)a.rafFirstResult={...row,crop:evidence(display,row.sequence,now)};
        if(a.raf.length>=1024)throw Error('RAF receipt bound');a.raf.push(row);
      }
      const timedOut=now-a.down>5000;
      // Retain the old RAF confirmation/spacing phase to avoid faster trials.
      if(timedOut||(a.up!==null&&a.rafFirstResult&&now-a.rafFirstResult.at>=100)){
        const bytes=read(ctx,0,20,320,88),confirmedAt=performance.now();
        if(!lastCanvas||String(lastCanvas.sequence)!==frame||!a.latestDisplay||a.latestDisplay.sequence!==lastCanvas.sequence||!same(bytes,a.latestDisplay.bytes))throw Error('Confirmation actual-frame association');
        const confirmation=comparison(bytes,a.value);
        api.result={...a,latestDisplay:undefined,confirmedAt,confirmation,confirmed:evidence(bytes,lastCanvas.sequence,confirmedAt,a.latestDisplay.sourceBytes),timedOut,errors:[...errors],savedAt:window.iphone.savedAt,metrics:window.iphone.metrics};active=null;
      }
    }catch(e){fail(e.message);api.result={failed:true,errors:[...errors]};active=null;}
    requestAnimationFrame(sample);
  }
  requestAnimationFrame(sample);
})();
