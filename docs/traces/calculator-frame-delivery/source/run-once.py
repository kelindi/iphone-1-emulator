"""One reviewed harness-only guest run;180s process cap, no retry."""
from pathlib import Path
import os,subprocess,json,time,signal,hashlib,socket
base=Path('.context/input-delivery').resolve();root=Path.cwd();out=base/'run-1';out.mkdir(exist_ok=False);pins=json.loads((base/'frozen.json').read_text());report={'passed':False,'started':time.time(),'scope':'One separately named per-frame delivery diagnostic, no scheduling change or retries','pins':pins,'observedProcesses':[]};sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def save():(out/'controller.json').write_text(json.dumps(report,indent=2))
def proc(pid):
 try:
  s=Path(f'/proc/{pid}/stat').read_text();f=s[s.rindex(')')+2:].split(' ');return {'pid':pid,'ppid':int(f[1]),'startTicks':f[19],'exe':os.readlink(f'/proc/{pid}/exe')}
 except OSError:return None
def match(x):
 now=proc(x['pid']);return now and now['startTicks']==x['startTicks']and now['exe']==x['exe']
def verify():
 for n,h in pins['files'].items():assert sha(Path(n))==h,n
known={}
def capture(nodepid):
 allp=[p for d in Path('/proc').iterdir()if d.name.isdigit()for p in[proc(int(d.name))]if p];scope={nodepid};changed=True
 while changed:
  changed=False
  for p in allp:
   if p['ppid']in scope and p['pid']not in scope:scope.add(p['pid']);changed=True
 for p in allp:
  if p['pid']in scope:known[(p['pid'],p['startTicks'])]=p
 p=out/'processes.json'
 if p.exists():
  for receipt in json.loads(p.read_text()):
   for x in receipt['processes']:known[(x['pid'],x['startTicks'])]=x
 report['observedProcesses']=list(known.values());save()
verify();free=os.statvfs(root);report['freeBefore']=free.f_bavail*free.f_frsize;save();assert report['freeBefore']>5*1024**3,'5GiB fresh browser headroom required'
with socket.socket()as check:assert check.connect_ex(('127.0.0.1',8220))!=0,'Diagnostic port already occupied'
server=None;child=None
try:
 serverLog=(out/'server.log').open('w');server=subprocess.Popen(['node','tools/web-server.mjs'],env=dict(os.environ,WEB_ROOT=str(root/'web'),PORT='8220'),stdout=serverLog,stderr=subprocess.STDOUT,start_new_session=True);report['serverPid']=server.pid;save()
 for i in range(50):
  with socket.socket()as s:
   if s.connect_ex(('127.0.0.1',8220))==0:break
  assert server.poll()is None;time.sleep(.1)
 else:raise AssertionError('Server startup bound')
 with(out/'run.log').open('w')as log:
  child=subprocess.Popen(['node',str(base/'run.mjs')],env=dict(os.environ,BASE_URL='http://127.0.0.1:8220',ARTIFACT_DIR=str(out)),stdout=log,stderr=subprocess.STDOUT,start_new_session=True);start=time.time()
  while child.poll()is None:
   capture(child.pid)
   if time.time()-start>180:report['timeout']=True;break
   time.sleep(.5)
  capture(child.pid)
  if child.poll()is None:raise TimeoutError('180-second browser process cap')
  report['exitCode']=child.returncode;assert child.returncode==0,'Browser diagnostic gate failed'
  data=json.loads((out/'report.json').read_text());assert data['passed']and data['correctnessPassed'];verify();report['passed']=True
finally:
 if child:
  capture(child.pid)
  for sig in [signal.SIGTERM,signal.SIGKILL]:
   for p in known.values():
    if match(p):
     try:os.kill(p['pid'],sig)
     except ProcessLookupError:pass
   time.sleep(.2)
  child.wait(timeout=5);report['remainingProcesses']=[p for p in known.values()if match(p)]
 if server:
  server.terminate()
  try:server.wait(timeout=3)
  except subprocess.TimeoutExpired:server.kill();server.wait()
  serverLog.close();report['serverClosed']=server.poll()is not None
 report['ended']=time.time();f=os.statvfs(root);report['freeAfter']=f.f_bavail*f.f_frsize;save()
assert not report.get('remainingProcesses');print(json.dumps({'passed':report['passed'],'elapsed':report['ended']-report['started']}))
