"""One bounded correctness run only; never launches a browser or retries."""
from pathlib import Path
import json,hashlib,gzip,subprocess,os,time,signal,shutil
root=Path.cwd();base=root/'.context/jit-executed-work';out=base/'replay';assert not out.exists(),'Never duplicate a run'
pins=json.loads((base/'replay-pins.json').read_text())
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
for path,h in pins['files'].items():assert sha(path)==h,path
expected=json.loads(Path(pins['controlReport']).read_text());assert expected['moduleSha256']==pins['originalCoreSha256'];assert expected['definitionCount']==expected['initialActive']==512
for ref in pins['rawWitnesses']:
 p=Path(ref['path']);assert sha(p)==ref['gzipSha256'];raw=gzip.decompress(p.read_bytes());assert len(raw)==ref['rawBytes'];assert hashlib.sha256(raw).hexdigest()==ref['rawSha256'];del raw
assert shutil.disk_usage('.').free>=pins['initialFloorBytes'],'Reserved native capture storage unavailable'
out.mkdir();report={'passed':False,'wallCapSeconds':180,'guestInstructionCap':100000000,'started':time.time(),'freeBefore':shutil.disk_usage('.').free}
(out/'process-before.txt').write_text(subprocess.check_output(['ps','-eo','pid,ppid,comm,cputime'],text=True))
def proc(pid):
 try:
  s=Path(f'/proc/{pid}/stat').read_text();a=s[s.rindex(')')+2:].split();return {'pid':pid,'ppid':int(a[1]),'start':a[19],'exe':os.readlink(f'/proc/{pid}/exe')}
 except OSError:return None
def same(p):
 x=proc(p['pid']);return x and x['start']==p['start'] and x['exe']==p['exe']
observed={}
with (out/'run.log').open('w')as log:
 child=subprocess.Popen(['node',str(base/'replay.mjs')],stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
 try:
  while child.poll()is None:
   items=[x for f in Path('/proc').iterdir()if f.name.isdigit()for x in [proc(int(f.name))]if x];scoped={child.pid};changed=True
   while changed:
    changed=False
    for x in items:
     if x['ppid']in scoped and x['pid']not in scoped:scoped.add(x['pid']);changed=True
   for x in items:
    if x['pid']in scoped:observed[(x['pid'],x['start'])]=x
   if time.time()-report['started']>180:report['timeout']=True;break
   time.sleep(.2)
 finally:
  for sig in [signal.SIGTERM,signal.SIGKILL]:
   for x in observed.values():
    if same(x):
     try:os.kill(x['pid'],sig)
     except ProcessLookupError:pass
   time.sleep(.2)
  if child.poll()is None:os.killpg(child.pid,signal.SIGKILL)
  report['exitCode']=child.wait();report['processes']=list(observed.values());report['allClosed']=all(not same(x)for x in observed.values());report['ended']=time.time();report['freeAfter']=shutil.disk_usage('.').free
  (out/'process-after.txt').write_text(subprocess.check_output(['ps','-eo','pid,ppid,comm,cputime'],text=True))
  try:result=json.loads((out/'report.json').read_text())
  except (OSError,ValueError)as e:result={};report['resultReadError']=str(e)
  report['passed']=report['exitCode']==0 and not report.get('timeout') and report['allClosed'] and result.get('passed',False)
  (out/'controller.json').write_text(json.dumps(report,indent=2)+'\n')
assert report['passed'],report
