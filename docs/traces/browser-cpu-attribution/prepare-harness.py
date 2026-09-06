from pathlib import Path
import hashlib,json
root=Path.cwd();out=root/'.context/jit-attribution';source=(root/'web/tests/performance.mjs').read_text();s=source
s=s.replace("import {chromium} from 'playwright';","import {chromium} from 'playwright';\nimport {Attribution} from './cdp.mjs';\nlet attribution;")
s=s.replace("args:['--no-sandbox','--disable-dev-shm-usage']","args:['--no-sandbox','--disable-dev-shm-usage','--remote-debugging-port=9237']")
s=s.replace("{viewport:{width:1280,height:1000},recordVideo:{dir,size:{width:1280,height:1000}}}","{viewport:{width:1280,height:1000}}")
a=" await page.waitForTimeout(1000);\n report.client=";assert s.count(a)==1
s=s.replace(a," await page.waitForTimeout(1000);\n attribution=await Attribution.connect(9237,resolve(dir,'profiles'));\n report.client=")
a="   await page.evaluate(k=>window.phoneTiming.begin(k),kind);";assert s.count(a)==1;s=s.replace(a,"   await attribution.begin(`${round}-${kind}`,await page.evaluate(()=>({metrics:window.iphone.metrics,savedAt:window.iphone.savedAt})));\n"+a)
a="   report.actions.push({kind,round,...result});";assert s.count(a)==1;s=s.replace(a,"   await attribution.end(await page.evaluate(()=>({metrics:window.iphone.metrics,savedAt:window.iphone.savedAt})));\n"+a)
a=" const percentile95=values=>";assert s.count(a)==1
s=s.replace(a,""" await attribution.begin('home-idle',await page.evaluate(()=>({metrics:window.iphone.metrics,savedAt:window.iphone.savedAt})));
 await page.waitForTimeout(3000);
 await attribution.end(await page.evaluate(()=>({metrics:window.iphone.metrics,savedAt:window.iphone.savedAt})));
 await page.waitForFunction(()=>!document.querySelector('#save').disabled,null,{timeout:60000});
 await attribution.begin('ordinary-save',await page.evaluate(()=>({metrics:window.iphone.metrics,savedAt:window.iphone.savedAt})));
 const savedBefore=await page.evaluate(()=>window.iphone.savedAt);
 await page.locator('#save').click();
 await page.waitForFunction(before=>window.iphone.savedAt!==before,before=savedBefore,{timeout:60000});
 await attribution.end(await page.evaluate(()=>({metrics:window.iphone.metrics,savedAt:window.iphone.savedAt})));
 const percentile95=values=>""")
# Explicit argument, not a JS assignment to an undeclared identifier.
s=s.replace('before=savedBefore','savedBefore')
a="finally{const video=page.video();await context.close();report.video=await video.path();await browser.close();await writeFile(resolve(dir,'report.json'),JSON.stringify(report,(_,value)=>typeof value==='bigint'?value.toString():value,2)+'\\n');}";assert s.count(a)==1
s=s.replace(a,"finally{if(attribution){try{if(attribution.active)await attribution.end({interrupted:true});report.modules=await attribution.collectModules();}catch(e){report.errors.push('Module mapping: '+e.message);}await attribution.close();}await context.close();await browser.close();await writeFile(resolve(dir,'report.json'),JSON.stringify(report,(_,value)=>typeof value==='bigint'?value.toString():value,2)+'\\n');}")
s=s.replace("Number(process.env.ROUNDS || 4)","Number(process.env.ROUNDS || 2)")
s=s.replace("method:'50 ms canvas sampling;", "method:'CPU attribution diagnostic with sampling overhead; never a speed acceptance result.50 ms canvas sampling;")
(out/'profile-browser.mjs').write_text(s);(out/'harness-proof.json').write_text(json.dumps({'source':'web/tests/performance.mjs','sourceSha256':hashlib.sha256(source.encode()).hexdigest(),'preparedSha256':hashlib.sha256(s.encode()).hexdigest(),'changes':['CDP worker/main sampling around existing navigation','three-second Home idle stage','explicit ordinary Save stage','video disabled; screenshots after profiler stops','Debugger bytecode collection only after all profiles'],'notRun':True},indent=2)+'\n')
