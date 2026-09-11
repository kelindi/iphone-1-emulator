from pathlib import Path
import os,shutil,subprocess,json,time,socket,hashlib
p=Path('.context/seed-gap');report={'runs':[],'roundsPerSession':11,'order':['control','candidate','candidate','control']}
for i,arm in enumerate(report['order']):
 assert shutil.disk_usage(p).free>5*2**30,'Chrome disk reserve'
 site=p/(arm+'-site');expected=json.loads((p/'frozen-site-hashes.json').read_text())[arm];assert all(hashlib.sha256((site/f).read_bytes()).hexdigest()==h for f,h in expected.items()),'Frozen site changed';env=dict(os.environ,WEB_ROOT=str(site),PORT='8244');log=(p/f'frozen-{i}-server.log').open('x');server=subprocess.Popen(['node','tools/web-server.mjs'],env=env,stdout=log,stderr=log)
 try:
  for _ in range(100):
   try:
    with socket.create_connection(('127.0.0.1',8244),timeout=.2):break
   except OSError:time.sleep(.1)
  out=p/f'frozen-{i}-{arm}';env.update(BASE_URL='http://127.0.0.1:8244',ARTIFACT_DIR=str(out),ROUNDS='11',EXPECT_CORE=str(site/'dist/emu.wasm'),EXPECT_WORKER=str(site/'emu-worker.js'),EXPECT_SEED=str(p/'seed/seed.wasm') if arm=='candidate' else 'web/jit/seed.wasm')
  with (p/f'frozen-{i}.log').open('x') as f:result=subprocess.run(['node',str(p/'performance-frozen.mjs')],env=env,stdout=f,stderr=f,timeout=300)
  report['runs'].append({'arm':arm,'directory':str(out),'exit':result.returncode});(p/'frozen-controller.json').write_text(json.dumps(report,indent=2));print(report['runs'][-1],flush=True)
  if result.returncode:break
 finally:server.terminate();server.wait(timeout=10);log.close()
report['closed']=True;(p/'frozen-controller.json').write_text(json.dumps(report,indent=2))
