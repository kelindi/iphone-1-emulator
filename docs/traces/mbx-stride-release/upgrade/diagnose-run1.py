from pathlib import Path
import json,hashlib,re
b=Path('.context/mbx-stride-integration');run=b/'run-1';r=json.loads((run/'report.json').read_text());sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();css=(b/'candidate/phone.css').read_bytes();csshash=sha(b/'candidate/phone.css');mismatches=[];positive=[]
for x in r['servedAssets']:
 if x.get('file'):assert sha(run/x['file'])==x['sha256']
 if x.get('error'):continue
 if x['path']=='phone.css' and x['sha256']!=csshash:
  body=(run/x['file']).read_bytes();assert body==css.decode('cp1252').encode('utf8');mismatches.append(x)
 if x['path']=='phone.css' and x['sha256']==csshash:positive.append(x)
workers=[]
for v in ['A','B']:
 sw=r['variants'][v]['hashes']['service-worker.js'];body=(run/('served/'+sw+'.bin')).read_text();assert sha(run/('served/'+sw+'.bin'))==sw
 assert 'response.clone().arrayBuffer()'in body and 'if (hash !== expected) throw' in body and 'await cache.put(url, response)'in body
 manifest=json.loads(re.search(r'const FILES = (\{.*?\});',body).group(1));assert manifest['phone.css']==csshash
 workers.append({'version':v,'actualReceivedSWsha256':sw,'declaredCssSha256':csshash,'rawArrayBufferSHAcheckedBeforeCachePut':True})
report={'originalOverallResult':r['result'],'originalFunctionalChecks':len(r['checks']),'notRetrospectivelyAccepted':True,'exactCp1252ThenUtf8Rows':mismatches,'independentExactCssBodyReceipts':positive,'installedWorkerRawIntegritySource':workers,'unavailableBodies':[x for x in r['servedAssets']if x.get('error')],'serverSourceSha256':sha(b/'offline-upgrade.mjs'),'serverHeadersFromFrozenSource':{'Content-Type':'text/css','Content-Length':len(css),'Content-Encoding':None},'headersLimitation':'Original run did not retain actual response headers or CDP base64Encoded bit. Server source explicitly emits these headers; transformation path is source-supported inference, exact transformed bytes are directly established.','playwrightBundleSha256':sha(Path('node_modules/playwright-core/lib/coreBundle.js')),'playwrightSourceLines':[36437,36450],'playwrightBehavior':'Network.getResponseBody body is re-encoded UTF8 when base64Encoded false; detached request throws Response body is unavailable.','originalSavedArtifactHash':sha(run/'saved-before.checkpoint'),'sameSavedRecord':r['savedBefore']==r['savedAfter'],'closure':r['closure']}
(b/'run1-diagnosis.json').write_text(json.dumps(report,indent=2)+'\n');print('diagnosis retained; original FAIL unchanged')
