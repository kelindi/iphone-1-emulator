from pathlib import Path
import os,shutil,subprocess,json,time,socket,hashlib
p=Path('.context/seed-gap');report={'runs':[],'roundsPerSession':11,'order':['control','candidate','candidate','control']}
for i,arm in enumerate(report['order']):
 assert shutil.disk_usage(p).free>5*2**30,'Chrome disk reserve'
 site=p/(arm+'-site');env=dict(os.environ,WEB_ROOT=str(site),PORT='8244');log=(p/f'browser-{i}-server.log').open('x');server=subprocess.Popen(['node','tools/web-server.mjs'],env=env,stdout=log,stderr=log)
 try:
  for _ in range(100):
   try:
    with socket.create_connection(('127.0.0.1',8244),timeout=.2):break
   except OSError:time.sleep(.1)
  out=p/f'browser-{i}-{arm}';env.update(BASE_URL='http://127.0.0.1:8244',ARTIFACT_DIR=str(out),ROUNDS='11',EXPECT_CORE='web/dist/emu.wasm',EXPECT_SEED=str(p/'seed/seed.wasm') if arm=='candidate' else 'web/jit/seed.wasm')
  with (p/f'browser-{i}.log').open('x') as f:result=subprocess.run(['node',str(p/'performance.mjs')],env=env,stdout=f,stderr=f,timeout=300)
  report['runs'].append({'arm':arm,'directory':str(out),'exit':result.returncode});(p/'browser-controller.json').write_text(json.dumps(report,indent=2));print(report['runs'][-1],flush=True)
  if result.returncode:break
 finally:server.terminate();server.wait(timeout=10);log.close()
report['closed']=True;(p/'browser-controller.json').write_text(json.dumps(report,indent=2))
