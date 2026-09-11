from pathlib import Path
import os,subprocess,time,socket,json
b=Path('.context/runtime-comparison');r={'runs':[]};env=dict(os.environ,PORT='8240')
with (b/'server.log').open('x') as log:
 server=subprocess.Popen(['node','tools/web-server.mjs'],env=env,stdout=log,stderr=log)
 try:
  for _ in range(100):
   try:
    with socket.create_connection(('127.0.0.1',8240),timeout=.2):break
   except OSError:time.sleep(.1)
  for name in ['browser-b1','browser-b2','native-a2']:
   d=b/name;assert not d.exists()
   if name.startswith('native'):
    d.mkdir();cmd=[str(b/'native-navigation'),'web/firmware/kernel_1A543a.macho','web/firmware/devicetree_m68ap.bin',str(b/'home.snap'),str(d),'4'];env2=os.environ.copy();out=d/'report.jsonl'
   else:
    cmd=['node','web/tests/performance.mjs'];env2=dict(os.environ,BASE_URL='http://127.0.0.1:8240',ARTIFACT_DIR=str(d),ROUNDS='4');out=b/(name+'.stdout.log')
   began=time.time()
   with out.open('x')as f,(b/(name+'.stderr.log')).open('x')as err:
    p=subprocess.run(cmd,env=env2,stdout=f,stderr=err,timeout=240)
   r['runs'].append({'name':name,'exit':p.returncode,'seconds':time.time()-began});(b/'controller.json').write_text(json.dumps(r,indent=2)+'\n');print(r['runs'][-1],flush=True)
   if p.returncode:break
 finally:
  server.terminate();server.wait(timeout=10);r['serverClosed']=True;(b/'controller.json').write_text(json.dumps(r,indent=2)+'\n')
