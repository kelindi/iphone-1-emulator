from pathlib import Path
p=Path('.context/input-delivery');s=Path('web/tests/input-feedback.mjs').read_text();s=s.replace("import {chromium} from 'playwright';","import {chromium} from 'playwright';\nimport {persistCrops} from './packets.mjs';")
s=s.replace("'.context/input-feedback/report'","'.context/input-delivery/run-1'")
s=s.replace("new URL('./include/","new URL('../../web/tests/include/").replace("new URL('../'+name","new URL('../../web/'+name")
s=s.replace("const page=await context.newPage();", "await context.addInitScript({path:resolve('.context/input-delivery/observer.js')});\nconst page=await context.newPage();")
a=s.index(' await page.evaluate(async references=>{');b=s.index(' },referenceImages);',a)+len(' },referenceImages);')
s=s[:a]+''' await page.evaluate(async references=>{
  const targets={};const hex=b=>[...new Uint8Array(b)].map(v=>v.toString(16).padStart(2,'0')).join('');
  for(const [value,ref]of Object.entries(references)){const image=new Image();image.src=ref.url;await image.decode();if(image.width!==320||image.height!==88)throw Error('Reference dimensions');const c=document.createElement('canvas');c.width=320;c.height=88;c.getContext('2d').drawImage(image,0,0);const bytes=c.getContext('2d').getImageData(0,0,320,88).data;if(hex(await crypto.subtle.digest('SHA-256',bytes))!==ref.sha256)throw Error('Reference identity');targets[value]=bytes;}
  window.deliveryObserver.configure(targets);
 },referenceImages);'''+s[b:]
s=s.replace('window.inputFeedback','window.deliveryObserver')
s=s.replace("const result={round,videoSeconds:(performance.now()-began)/1000,...await page.evaluate(()=>window.deliveryObserver.result)};", "const encodingStart=performance.now();const packet=await page.evaluate(()=>window.deliveryObserver.takeResult());const serialized=performance.now();const raw=await persistCrops(dir,packet,String(round).padStart(2,'0')+'-'+key);const result={round,videoSeconds:(performance.now()-began)/1000,...raw,transportMs:serialized-encodingStart,cropWriteMs:performance.now()-serialized};")
s=s.replace("assert.ok(!result.timedOut&&result.confirmation.passed&&result.up!==null,'Genuine result/confirmation failed');", "assert.ok(!result.failed&&!result.timedOut&&result.confirmation.passed&&result.up!==null&&!result.errors.length,'Genuine result/confirmation or frame association failed');assert.ok(result.firstButton&&result.firstResult&&result.rafFirstButton&&result.rafFirstResult,'Required genuine frame evidence missing');")
a=s.index(' report.summary={};');b=s.index(' report.jit=',a)
s=s[:a]+''' report.summary={};
 const fields={receiptButton:r=>r.firstButton.receivedAt-r.down,canvasButton:r=>r.firstButton.canvasDoneAt-r.down,rafButton:r=>r.rafFirstButton.at-r.down,canvasResult:r=>r.firstResult.canvasDoneAt-r.down,rafResult:r=>r.rafFirstResult.at-r.down,releaseCanvasResult:r=>r.firstResult.canvasDoneAt-r.up,releaseRafResult:r=>r.rafFirstResult.at-r.up};
 for(const key of ['9','C']){const rows=report.samples.filter(r=>r.key===key);report.summary[key]=Object.fromEntries(Object.entries(fields).map(([name,fn])=>[name,stats(rows.map(fn))]));}
 report.correctnessPassed=report.samples.length===60&&report.samples.every(r=>!r.timedOut&&r.confirmation.passed&&!r.errors.length)&&report.errors.length===0;
 report.latencyPassed=Object.values(report.summary).every(s=>s.canvasButton.count===30&&s.canvasButton.p95Ms<=150);
 report.observerErrors=await page.evaluate(()=>window.deliveryObserver.errors);assert.deepEqual(report.observerErrors,[]);
'''+s[b:]
s=s.replace('report.passed=report.correctnessPassed&&report.latencyPassed;', "report.passed=report.correctnessPassed; // Diagnostic completion does not depend on a favorable latency result.")
s=s.replace("method:'30 real presses per key;30ms requested pulse; actual page pointer events; new guest frame sampled in requestAnimationFrame.40x40 key crop changes>=16pixels by>12RGB. Full320x88 result matches independently reviewed reference within2RGB/exactalpha, confirmed100ms later.650ms post-result spacing. Normal guest clock/autosave. Canvas observation, not physical display timing.'", "method:'Separate harness-only per-frame delivery diagnostic.30 real presses/key after one warmup pair; unchanged30ms pulse/650ms spacing, guest clock/autosave. Early actual worker frame receipt→original canvas put completion→legacy RAF observations. Same40x40 button threshold/full320x88<=2RGB exactalpha results. Source/canvas ROI byte equality and raw crops retained. Confirmation100ms after legacy RAF first result; no favorable-latency retry. Observer copying/analysis overhead recorded; not physical display timing or an uninstrumented optimization.'")
(p/'run.mjs').write_text(s)
