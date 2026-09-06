from pathlib import Path
import shutil,json,hashlib
p=Path('.context/jit-mixed-coverage');old=Path('.context/jit-fallback-census');v3=Path('.context/jit-return-visit-v3');sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
for variant in ['original','candidate']:
 site=p/'sites'/variant;site.mkdir(parents=True,exist_ok=True)
 for n in ['index.html','app.js','phone.css','emu-worker.js','seed-jit.js','storage.js','checkpoint-worker.js','manifest.json']:shutil.copy(old/'frozen-web'/n,site/n)
 for n in ['assets.js','offline.js']:
  if(old/'site'/n).exists():shutil.copy(old/'site'/n,site/n)
 for n in ['images','icons']:
  if(old/'site'/n).exists():shutil.copytree(old/'site'/n,site/n,dirs_exist_ok=True)
 for n in ['jit','dist']:(site/n).mkdir(exist_ok=True)
 for n in ['seed.wasm','regions.json','abi.json']:shutil.copy(p/variant/n,site/'jit'/n)
 for n in ['emu.js','emu.wasm']:shutil.copy(p/variant/'dist'/n,site/'dist'/n)
 if variant=='candidate':shutil.copy(p/variant/'seed-jit.js',site/'seed-jit.js')
 original=(site/'emu-worker.js').read_text();hook="      case 'profileCounters': send('profileCounters', {id:message.id, retired:diagnosticRetired, ticks:api.ticks(), cycles:String(api.cycles()), jit:jit?.stats(), at:performance.now()}); return;\n"
 s='let diagnosticRetired = 0;\n'+original;s=s.replace('      const ran = api.run(50000);','      const ran = api.run(50000); diagnosticRetired += ran;').replace('    switch (message.type) {\n','    switch (message.type) {\n'+hook)
 assert s.replace(hook,'').replace(' diagnosticRetired += ran;','').removeprefix('let diagnosticRetired = 0;\n')==original;(site/'emu-worker.js').write_text(s)
for n in ['cache-server.mjs','frames.mjs','frame-packet.mjs']:shutil.copy(v3/n,p/n)
(p/'references').mkdir(exist_ok=True)
for name in ['C0-settings','C0-home','H1-settings','H1-home']:
 for ext in ['rgba','png']:shutil.copy(v3/'references'/f'{name}.{ext}',p/'references'/f'{name}.{ext}')
s=(v3/'run-browser.mjs').read_text();s=s.replace('.context/jit-return-visit-v3','.context/jit-mixed-coverage')
s=s.replace("import {execFileSync} from 'node:child_process';","import {cacheServer} from './cache-server.mjs';")
s=s.replace("const profile=resolve('.context/jit-mixed-coverage/profiles',variant);","const profile=resolve(dir,'profile');")
s=s.replace("const report={variant,expectedSeed,", "const expectedRegions=variant==='candidate'?576:512;\nconst report={variant,expectedSeed,")
s=s.replace("method:'Return-visit loader-only no-cache/ETag; fixed paused Import/512regions and1second resume; no Profiler/Debugger/interception; measured trace ends before resume; fixed1second idle+50ms paired raw validation; ordinary30second Save'","method:'Fresh-profile mixed seed comparison, frozen census UI. Fixed paused Import then1second resume;512 original/576 candidate regions required. No Profiler/Debugger/interception; compile trace over complete navigation-to-fixed-work scope. Fixed1second idle+50ms raw validation; ordinary30second Save.240s perarm, no retry.'")
s=s.replace("const context=await chromium", "const serverRecord=await cacheServer(resolve('.context/jit-mixed-coverage/sites',variant),{cache:'no-cache',log:r=>{report.serverRequests??=[];report.serverRequests.push(r);}});const server=serverRecord.server;await new Promise(r=>server.listen(Number(new URL(origin).port),'127.0.0.1',r));\nconst context=await chromium")
s=s.replace('remote-debugging-port=9240','remote-debugging-port=9252').replace('127.0.0.1:9240','127.0.0.1:9252')
s=s.replace("page.locator('#pause').click()","page.locator('#pause').evaluate(b=>b.click())")
s=s.replace('c.jit.regions===512','c.jit.regions===expectedRegions').replace('c.jit.regions!==512','c.jit.regions!==expectedRegions')
a=s.index(" if(phase==='measured'){\n  await stopTrace();");b=s.index(" await page.locator('#pause')",a);s=s[:a]+s[b:]
s=s.replace("phase==='training'?2:11",'11')
s=s.replace("if(!result.passed)report.validationPassed=false;return result;","if(!result.passed)report.validationPassed=false;return result;")
s=s.replace("if(result.error)throw Error(result.error);", "if(result.error)throw Error(result.error);if(!validation.passed)throw Error('Strict settled frame gate failed: '+label+'-'+kind);")
s=s.replace("report.afterFixedWork=await counters();", "report.afterFixedWork=await counters();await mark('fixed-work-end');")
s=s.replace("['emu.wasm','b3237ef3843412aea164d3db111299650ea8433bf16311417116799959ab6606']","['emu.wasm',site['dist/emu.wasm']]")
s=s.replace("client.close();await context.close();", "client.close();await context.close();server.closeAllConnections();await new Promise(r=>server.close(r));")
(p/'run-browser.mjs').write_text(s)
pins={'sites':{v:{str(f.relative_to(p/'sites'/v)):sha(f)for f in sorted((p/'sites'/v).rglob('*'))if f.is_file()}for v in ['original','candidate']},'origins':{'original':'http://127.0.0.1:8214','candidate':'http://127.0.0.1:8215'},'fixture':{'path':'.context/graphics-speed/fixed-dismiss.snap','sha256':sha(Path('.context/graphics-speed/fixed-dismiss.snap'))},'chrome':{'path':'/usr/bin/google-chrome','sha256':sha(Path('/usr/bin/google-chrome'))},'scope':'Identical frozen census frontend before visible-recovery UI; isolated observer-free core/seed expansion only. Same normalSave/clock/input. Original independent settled references from declared1s+50ms method.'}
(p/'site-pins.json').write_text(json.dumps(pins,indent=2)+'\n')
