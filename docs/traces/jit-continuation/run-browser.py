from pathlib import Path
import os,json,subprocess,time,hashlib,urllib.request
root=Path.cwd();base=root/'.context/jit-continuation/browser';out=base/'quiet-navigation';out.mkdir(exist_ok=True)
proof=json.loads((base/'preflight.json').read_text());urls={'baseline':'http://localhost:8184','candidate':'http://localhost:8185'}
fixture=root/'.context/graphics-speed/fixed-dismiss.snap';sha=lambda b:hashlib.sha256(b).hexdigest()
assert sha(fixture.read_bytes())==proof['fixtureSha256']
report={'method':'Quiet original/continuation/continuation/original ABBA; fresh Chrome and normal UI import of identical MBX fixture per run. Eleven rounds each,first separate,ten warm;20 warm samples/action/arm. Original seed and frontend byte-identical; only runtime continuation, its linked glue and derived offline revision differ. Actual release timestamps, unchanged30ms pulse and50ms/500ms readiness method from055. VM diagnostic,not reference-laptop acceptance.','provenance':proof,'runs':[]}
for index,arm in enumerate(['baseline','candidate','candidate','baseline'],1):
 url=urls[arm]
 for name,expected in [('dist/emu.wasm',proof['arms'][arm]['runtimeSha256']),('jit/seed.wasm',proof['arms'][arm]['seedSha256'])]:assert sha(urllib.request.urlopen(url+'/'+name).read())==expected
 for name,expected in proof['frontendSha256'].items():assert sha((base/arm/name).read_bytes())==expected
 assert sha((base/'performance.mjs').read_bytes())==proof['performanceHarnessSha256']
 folder=out/f'{index}-{arm}';folder.mkdir(exist_ok=True)
 env=dict(os.environ,BASE_URL=url,FIXTURE_SNAPSHOT=str(fixture),ARTIFACT_DIR=str(folder),ROUNDS='11',EXPECTED_ASSETS=json.dumps({'dist/emu.wasm':proof['arms'][arm]['runtimeSha256'],'dist/emu.js':proof['arms'][arm]['runtimeGlueSha256'],'jit/seed.wasm':proof['arms'][arm]['seedSha256']}))
 start=time.time()
 with (folder/'run.log').open('w') as log:code=subprocess.run(['node',str(base/'performance.mjs')],env=env,stdout=log,stderr=subprocess.STDOUT).returncode
 data=json.loads((folder/'report.json').read_text());row={'index':index,'arm':arm,'elapsedSeconds':time.time()-start,'exitCode':code,'report':data};report['runs'].append(row);(out/'comparison.json').write_text(json.dumps(report,indent=2)+'\n')
 assert code==0 and data.get('passed'),str(row)
 previous=-1
 for action in data['actions']:
  jit=action['metrics']['jit'];assert jit['enabled'] and not jit.get('error') and 0<jit['regions']<=512 and jit['bytes']==proof['arms'][arm]['seedBytes'];assert jit['retired']>previous;previous=jit['retired']
 print(index,arm,'passed',flush=True)
for arm in urls:
 summary={}
 for kind in ['settings','home']:
  actions=[a for row in report['runs'] if row['arm']==arm for a in row['report']['actions'] if a['kind']==kind]
  warm=[a['releaseToReadyMs'] for a in actions if a['round']>0];ordered=sorted(warm);assert len(warm)==20
  summary[kind]={'firstReleaseToReadyMs':[a['releaseToReadyMs'] for a in actions if a['round']==0],'warmSamples':20,'warmReleaseToReadyMs':warm,'p95Ms':ordered[18],'medianMs':(ordered[9]+ordered[10])/2}
 report[arm]=summary
report['passed']=True;(out/'comparison.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({arm:report[arm] for arm in urls}),flush=True)
