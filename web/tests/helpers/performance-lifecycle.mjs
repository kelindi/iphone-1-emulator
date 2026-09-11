import {readFile} from 'node:fs/promises';
import {resolve,basename} from 'node:path';
import {createHash} from 'node:crypto';

export async function bounded(operation,timeoutMs,label) {
  let timer;
  try {
    return await Promise.race([
      Promise.resolve().then(operation),
      new Promise((_,reject)=>{timer=setTimeout(()=>reject(Error(`${label} timed out after ${timeoutMs} ms`)),timeoutMs);})
    ]);
  } finally { clearTimeout(timer); }
}

export async function loadPerformanceFixture(file) {
  const path=resolve(file),buffer=await readFile(path);
  return {
    metadata:{path,bytes:buffer.length,sha256:createHash('sha256').update(buffer).digest('hex')},
    // Import these exact owned bytes, not a path Playwright would reread later.
    upload:{name:basename(path),mimeType:'application/octet-stream',buffer}
  };
}

export async function finishPerformanceRun({report,assetTasks=[],required=[],context,browser,page,
  writeReport,bodyTimeoutMs=10000,closeTimeoutMs=10000,videoTimeoutMs=5000}) {
  report.cleanup=[];
  const fail=(stage,error)=>{report.passed=false;report.errors.push(`${stage}: ${error?.stack||error}`);};
  let video;
  try {
    report.servedAssets=await Promise.all(assetTasks.map(({name,promise})=>
      bounded(()=>promise,bodyTimeoutMs,`Response body ${name}`)
        .catch(error=>({name,error:error.message,matches:false}))));
    if(report.servedAssets.some(a=>!a.matches))fail('Assets','Served asset identity mismatch or unavailable body');
    for(const name of required)if(!report.servedAssets.some(a=>a.name===name&&a.matches))fail('Assets','Missing served identity: '+name);
    try { video=page?.video(); } catch(error) { fail('Video handle',error); }
  } catch(error) { fail('Asset collection',error); }
  finally {
    // Each close gets its own attempt even if the previous close throws/hangs.
    // Timeouts are failures, never receipts that a process actually exited.
    for(const [stage,resource] of [['context.close',context],['browser.close',browser]]) {
      if(!resource){report.cleanup.push({stage,status:'not-created'});continue;}
      try { await bounded(()=>resource.close(),closeTimeoutMs,stage);report.cleanup.push({stage,status:'closed'}); }
      catch(error){report.cleanup.push({stage,status:'failed',error:error.message});fail(stage,error);}
    }
    if(video) {
      try { report.video=await bounded(()=>video.path(),videoTimeoutMs,'Video finalization'); }
      catch(error) { fail('Video finalization',error); }
    }
    report.passed=Boolean(report.passed)&&report.errors.length===0;
    // Filesystem failure cannot guarantee an artifact, but the write is always
    // attempted after all cleanup paths, and its rejection remains visible.
    await writeReport(report);
  }
  return report;
}
