from pathlib import Path
b=Path('.context/jit-executed-work');d=b/'warm-browser';old=Path('.context/jit-fallback-census/browser.mjs').read_text()
s=old.replace("'../../docs/traces/browser-first-use/cdp.mjs'","'../../../docs/traces/browser-first-use/cdp.mjs'").replace("const base=resolve('.context/jit-fallback-census'),dir=base+'/browser-1',pins=JSON.parse(await readFile(base+'/browser-pins.json'));","const base=resolve('.context/jit-executed-work/warm-browser'),dir=base+'/run-1',pins=JSON.parse(await readFile(base+'/pins.json'));\nconst coreBase=resolve('.context/jit-executed-work');\nlet windowBefore=null;")
s=s.replace("import assert from 'node:assert/strict';","import assert from 'node:assert/strict';\nimport {execFileSync} from 'node:child_process';")
a=s.index('function parseRows(');z=s.index('\ntry {',a)
s=s[:a]+'''async function retainCounts(label,before,after){
 const folder=dir+'/'+label;await mkdir(folder,{recursive:true});const data=[];
 // Persist all three immutable raw payloads before any count/status assertions.
 for(const part of after.parts||[]){const raw=Buffer.from(part.base64,'base64'),gzip=gzipSync(raw);await writeFile(folder+'/'+part.name+'.bin.gz',gzip,{flag:'wx'});data.push({name:part.name,rawBytes:raw.length,rawSha256:sha(raw),gzipBytes:gzip.length,gzipSha256:sha(gzip)});}
 const metadata={epochStart:before.census.epoch,generatedOverflow:after.census.generatedOverflow,retiredDelta:after.retired-before.retired,jitRetiredDelta:after.jit.retired-before.jit.retired,interpreterCapacity:pins.capacity,generatedCapacity:pins.generatedCapacity,positionsPath:coreBase+'/positions.json',positionsSha256:sha(await readFile(coreBase+'/positions.json')),stopped:true,data,before,after:{...after,parts:undefined}};
 await writeFile(folder+'/capture.json',json(metadata));assert.equal(data.length,3,'complete retained arrays');assert.deepEqual(after.binding,pins.seedBinding);assert.equal(after.census.enabled,0);assert.equal(after.guestStatus,0);assert(metadata.retiredDelta<=pins.windowInstructionCap);assert(metadata.jitRetiredDelta>0);
 execFileSync('python3',[coreBase+'/check-capture.py',folder],{stdio:'inherit',timeout:20000});return JSON.parse(await readFile(folder+'/decoded-summary.json'));
}
'''+s[z:]
s=s.replace("if(String(url).includes('emu-worker.js'))window.fcWorker=this;","if(String(url).includes('emu-worker.js')){window.fcWorker=this;this.addEventListener('message',e=>{if(e.data.type==='fatal'&&!window.fcFailure)window.fcFailure=e.data;});}")
s=s.replace("const {buffer,...rest}=e.data;if(buffer)rest.table=window.packFrame(new Uint8Array(buffer));resolve(rest);","const {parts,...rest}=e.data;rest.parts=(parts||[]).map(p=>({name:p.name,base64:window.packFrame(new Uint8Array(p.buffer))}));resolve(rest);")
s=s.replace("await page.waitForTimeout(1000);\n await page.evaluate(()=>{","await page.waitForTimeout(1000);\n await page.evaluate(()=>{")
s=s.replace("const before=await counters(round?'start':'read');await page.evaluate", "const before=await counters(round?'start':'read');if(round){windowBefore=before;assert.deepEqual(before.binding,pins.seedBinding);assert.equal(before.census.calls,0);assert.equal(before.census.generatedOverflow,'0');assert.equal(before.census.enabled,1);}await page.evaluate")
s=s.replace("()=>window.censusReady.result,null,{timeout:16000}","()=>window.censusReady.result||window.fcFailure,null,{timeout:16000}")
s=s.replace("const readiness=await page.evaluate(()=>window.censusReady.result)","const readiness=await page.evaluate(()=>window.fcFailure?{error:window.fcFailure.message}:window.censusReady.result)")
a=s.index('  const {table,...afterMetadata}=after');z=s.index('\n  await frame(label,kind);',a)
s=s[:a]+'''  const {parts,...afterMetadata}=after,action={label,before,after:afterMetadata,readiness};report.actions.push(action);await persist();
  if(round){action.counts=await retainCounts(label,before,after);windowBefore=null;}
  await persist();assert(!readiness.error,readiness.error);
'''+s[z:]
s=s.replace("}catch(e){report.errors.push(e.stack||e.message);report.passed=false;}","}catch(e){report.errors.push(e.stack||e.message);report.passed=false;if(windowBefore){try{const after=await counters('stop');await retainCounts('first-failure',windowBefore,after);}catch(extra){report.captureError=extra.stack||extra.message;}}}")
(d/'browser.mjs').write_text(s)
# The frozen old collector/controller remain unmodified. This separate controller cannot launch until explicit parent scheduling.
p=Path('.context/jit-fallback-census/run-browser.py').read_text().replace("p=root/'.context/jit-fallback-census';out=p/'browser-1'","p=root/'.context/jit-executed-work/warm-browser';out=p/'run-1'").replace("browser-harness-pins.json","frozen.json").replace('>=5*1024**3', '>=5.5*1024**3').replace("'5GiB disk floor'","'5.5GiB disk floor'")
(d/'run-browser.py').write_text(p)
# Stop at the actual cap before another50k run can begin.
f=d/'site/emu-worker.js';x=f.read_text().replace('diagnosticRetired-diagnosticWindowStart > 150000000','diagnosticRetired-diagnosticWindowStart >= 150000000');f.write_text(x)
f=d/'worker-watch.txt';f.write_text(f.read_text().replace('diagnosticRetired-diagnosticWindowStart > 150000000','diagnosticRetired-diagnosticWindowStart >= 150000000'))
