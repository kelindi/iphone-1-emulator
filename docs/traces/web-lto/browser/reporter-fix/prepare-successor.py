from pathlib import Path
b=Path('.context/web-lto/browser')
s=(b/'run-browser.mjs').read_text()
s="import {validateAssets} from './reporter-fix/validate-assets.mjs';\n"+s
start=s.index(" for(const file of ['index.html'")
end=s.index("\n}\nif(!report.passed)",start)
s=s[:start]+''' const assets=validateAssets(report.assets,site,origin);report.assetValidation=assets;
 report.errors.push(...assets.errors);if(!assets.passed)report.passed=false;
 try{await processReceipt('before-close');}catch(e){report.errors.push('Process receipt: '+e.message);report.passed=false;}
 try{client.close();}catch(e){report.errors.push('CDP close: '+e.message);}
 try{await context.close();report.browserClosed=true;}catch(e){report.errors.push('Browser close: '+e.message);report.browserClosed=false;}
 try{server.closeAllConnections();await new Promise((resolve,reject)=>server.close(e=>e?reject(e):resolve()));report.serverClosed=true;}catch(e){report.errors.push('Server close: '+e.message);}
 try{report.video=await page.video().path();}catch(e){report.errors.push('Video path: '+e.message);}
 report.passed=report.passed&&report.errors.length===0&&report.browserClosed===true&&report.serverClosed===true;
 await writeFile(resolve(dir,'report.json'),json(report));console.log(JSON.stringify({finished:true,passed:report.passed,errors:report.errors}));'''+s[end:]
(b/'run-browser-successor.mjs').write_text(s)
c=(b/'run-series.py').read_text()
c=c.replace('out=base/\'measured\'','out=base/\'continuation\'')
c=c.replace("base/'browser-frozen.json'","base/'successor-frozen.json'")
c=c.replace("order=['original','candidate','candidate','original']","order=['candidate','candidate','original']\nfirst=json.loads((base/'measured/1-original/report-partial.json').read_text());firstCompilation=json.loads((base/'first-arm-compilation.json').read_text());old=json.loads((base/'measured/series.json').read_text());assert not old['passed'] and len(old['runs'])==1 and old['runs'][0]['allClosed']\nassert json.loads((base/'first-arm-artifact-audit.json').read_text())['artifactAuditPassed']")
c=c.replace('for v in order[:2]:',"for v in ['original','candidate']:")
c=c.replace("assert shutil.disk_usage('.').free>5.8*2**30,'Initial headroom requires>5.8GiB'","assert shutil.disk_usage('.').free>5.8*2**30,'Continuation start headroom requires>5.8GiB'")
c=c.replace("'seriesCapSeconds':1040}","'seriesCapSeconds':800,'originalSeriesPassed':False,'originalFirstArm':'../measured/1-original','limitation':'Recovered first control has failed finalizer, outer cleanup only and truncated recording; this is a separately started B/B/A continuation, not an uninterrupted passing ABBA.'}")
c=c.replace('enumerate(order,1)','enumerate(order,2)').replace("<1040-240","<800-240")
c=c.replace("base/'run-browser.mjs'","base/'run-browser-successor.mjs'")
a=c.index('  if index in [2,4]:')
z=c.index('  # Completed recordings',a)
c=c[:a]+'''  if index in [2,4]:
   control=firstCompilation if index==2 else row['compilation'];candidate=row['compilation'] if index==2 else report['runs'][1]['compilation'];assert candidate['cpuUpperMs']<=control['cpuLowerMs']*1.25+100,'CompilationCPUbudget failed'
  a=first['C0Import'];b=row['report']['C0Import'];assert a['cycles']==b['cycles']and a['ticks']==b['ticks'],'Pausedimport identity differs'
  assert row['report']['browserClosed'] and row['report']['serverClosed'],'Graceful closure required'
  assert Path(row['report']['video']).is_file() and Path(row['report']['video']).stat().st_size>0,'Finalized recording required'
'''+c[z:]
(b/'run-continuation.py').write_text(c)
