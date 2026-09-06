"""One reviewed functional browser upgrade, bounded and never retried."""
from pathlib import Path
import json,hashlib,os,subprocess,time,signal,shutil
b=Path('.context/mbx-stride-integration/final-release');out=b/'run-1';assert not out.exists(),'No duplicate attempt'
pins=json.loads((b/'execution-pins.json').read_text())
def sha(p):
 h=hashlib.sha256()
 with open(p,'rb')as f:
  while x:=f.read(1048576):h.update(x)
 return h.hexdigest()
for row in pins['files']:assert sha(row['path'])==row['sha256'],row['path']
assert shutil.disk_usage('.').free>=6*1024**3,'Require6GiB initial free space'
report={'passed':False,'started':time.time(),'capSeconds':360,'freeBefore':shutil.disk_usage('.').free,'frozenSha256':sha(b/'execution-pins.json')}
(b/'process-before.txt').write_text(subprocess.check_output(['ps','-eo','pid,ppid,comm,cputime'],text=True))
def proc(pid):
 try:
  s=Path('/proc',str(pid),'stat').read_text();a=s[s.rindex(')')+2:].split();return {'pid':pid,'ppid':int(a[1]),'startTicks':a[19],'state':a[0]}
 except (FileNotFoundError,ProcessLookupError):return None
def live(p):
 x=proc(p['pid']);return x and x['startTicks']==p['startTicks'] and x['state']!='Z'
observed={};child=None
with (b/'stdout.log').open('x')as stdout,(b/'stderr.log').open('x')as stderr:
 child=subprocess.Popen(['node',str(b/'smoke.mjs')],stdout=stdout,stderr=stderr,start_new_session=True)
 try:
  while child.poll()is None:
   rows=[v for f in Path('/proc').iterdir()if f.name.isdigit()for v in [proc(int(f.name))]if v];scope={child.pid};changed=True
   while changed:
    changed=False
    for x in rows:
     if x['ppid']in scope and x['pid']not in scope:scope.add(x['pid']);changed=True
   for x in rows:
    if x['pid']in scope:observed[(x['pid'],x['startTicks'])]=x
   (b/'controller-processes.json').write_text(json.dumps(list(observed.values()),indent=2))
   if time.time()-report['started']>=360:report['timeout']=True;break
   time.sleep(.2)
 finally:
  forced=[]
  for sig in [signal.SIGTERM,signal.SIGKILL]:
   for x in observed.values():
    if live(x):
     forced.append({'pid':x['pid'],'signal':sig.value})
     try:os.kill(x['pid'],sig)
     except ProcessLookupError:pass
   time.sleep(.2)
  if child.poll()is None:
   try:os.killpg(child.pid,signal.SIGKILL)
   except ProcessLookupError:pass
  report['exitCode']=child.wait();report['observed']=list(observed.values());report['allClosed']=all(not live(x)for x in observed.values());report['forced']=forced;report['ended']=time.time();report['freeAfter']=shutil.disk_usage('.').free
  try:r=json.loads((out/'report.json').read_text())
  except (OSError,ValueError)as e:r={};report['reportReadError']=str(e)
  report['postPinsExact']=all(sha(row['path'])==row['sha256'] for row in pins['files'])
  report['passed']=report['exitCode']==0 and not report.get('timeout') and report['allClosed'] and not forced and report['postPinsExact'] and r.get('passed') and r.get('browserClosed') and r.get('serverClosed') and len(r.get('checks',[]))==21
  (out/'process-after.txt').write_text(subprocess.check_output(['ps','-eo','pid,ppid,comm,cputime'],text=True));(out/'controller.json').write_text(json.dumps(report,indent=2)+'\n')
assert report['passed'],report
