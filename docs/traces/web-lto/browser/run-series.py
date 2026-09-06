"""Frozen fresh-profile LTO ABBA; cleanup only new owned successful profiles."""
from pathlib import Path
import os,subprocess,json,hashlib,time,signal,runpy,shutil,socket
from profile_cleanup import archive_profile
r=Path.cwd();base=r/'.context/web-lto/browser';out=base/'measured';assert not out.exists(),'No duplicate series'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();pins=json.loads((base/'site-pins.json').read_text());frozen=json.loads((base/'browser-frozen.json').read_text());order=['original','candidate','candidate','original']
def verify():
 for n,h in frozen['files'].items():assert sha(Path(n))==h,n
 for v in order[:2]:
  for n,h in pins['sites'][v].items():assert sha(base/'sites'/v/n)==h,(v,n)
 assert sha(r/pins['fixture']['path'])==pins['fixture']['sha256']
verify();assert shutil.disk_usage('.').free>5.8*2**30,'Initial headroom requires>5.8GiB';out.mkdir()
report={'passed':False,'order':order,'pins':pins,'frozen':frozen,'runs':[],'start':time.time(),'armCapSeconds':240,'seriesCapSeconds':1040}
def save():(out/'series.json').write_text(json.dumps(report,indent=2)+'\n')
def proc(pid):
 try:
  raw=Path(f'/proc/{pid}/stat').read_text();fields=raw[raw.rindex(')')+2:].split(' ');return dict(pid=pid,ppid=int(fields[1]),startTicks=fields[19],exe=os.readlink(f'/proc/{pid}/exe'))
 except OSError:return None
def receiptPids(folder):
 p=folder/'process-receipt.json'
 if not p.exists():return []
 data=json.loads(p.read_text());return list({(p['pid'],p['startTicks']):p for rec in data['receipts']for p in rec['processes']}.values())
def matches(p):
 now=proc(p['pid']);return now is not None and now['startTicks']==p['startTicks'] and now['exe']==p['exe']
def cleanup(targets):
 for sig in [signal.SIGTERM,signal.SIGKILL]:
  for p in targets:
   if matches(p):
    try:os.kill(p['pid'],sig)
    except ProcessLookupError:pass
  time.sleep(.25)
def system(label):
 (out/(label+'-system.txt')).write_text(subprocess.check_output(['ps','-eo','pid,ppid,comm,cputime'],text=True));return shutil.disk_usage('.').free


save()
try:
 for index,variant in enumerate(order,1):
  assert time.time()-report['start']<1040-240,'Insufficientremaining fullarmbudget'
  verify();free=system(f'{index}-before');assert free>5.0*2**30,'Per-arm free floor>5GiB'
  for port in [8222,8223,9254]:
   with socket.socket()as s:assert s.connect_ex(('127.0.0.1',port))!=0,'Reserved port occupied'
  folder=out/f'{index}-{variant}';folder.mkdir();start=time.time();observed={};code=None
  with (folder/'run.log').open('w')as log:
   child=subprocess.Popen(['node',str(base/'run-browser.mjs')],cwd=r,env=dict(os.environ,PHASE='measured',VARIANT=variant,ARTIFACT_DIR=str(folder)),stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
   while child.poll()is None:
    entries=[p for f in Path('/proc').iterdir()if f.name.isdigit()for p in [proc(int(f.name))]if p];scope={child.pid};changed=True
    while changed:
     changed=False
     for p in entries:
      if p['ppid']in scope and p['pid']not in scope:scope.add(p['pid']);changed=True
    for p in entries:
     if p['pid']in scope:observed[(p['pid'],p['startTicks'])]=p
    for p in receiptPids(folder):observed[(p['pid'],p['startTicks'])]=p
    if time.time()-start>240:
     cleanup(list(observed.values()));
     if child.poll()is None:os.killpg(child.pid,signal.SIGKILL)
     report['failure']='240second per-arm bound';break
    time.sleep(.5)
   code=child.wait()
  targets=list(observed.values())+receiptPids(folder);cleanup(targets);row={'index':index,'variant':variant,'exitCode':code,'elapsedSeconds':time.time()-start,'freeBefore':free,'freeAfterClose':system(f'{index}-after'),'scopedProcesses':targets,'allClosed':all(not matches(p)for p in targets)};report['runs'].append(row);save();assert targets and row['allClosed'],'Scopedprocess cleanup failed'
  if(folder/'report.json').exists():row['report']=json.loads((folder/'report.json').read_text());save()
  assert code==0 and row.get('report',{}).get('passed'),'Browsergate failed; entire failedprofile retained'
  assert row['report']['traceComplete'].get('dataLossOccurred')is False
  analyzer=runpy.run_path(str(base/'analyze-compile.py'))['analyze'];row['compilation']=analyzer(json.loads((folder/'trace.json').read_text())['traceEvents']);save();assert row['compilation']['missingThreadFields']==0
  if index in [2,4]:
   control=report['runs'][0 if index==2 else 3]['compilation'];candidate=report['runs'][1 if index==2 else 2]['compilation'];assert candidate['cpuUpperMs']<=control['cpuLowerMs']*1.25+100,'CompilationCPUbudget failed'
  if index>1:
   a=report['runs'][0]['report']['C0Import'];b=row['report']['C0Import'];assert a['cycles']==b['cycles']and a['ticks']==b['ticks'],'Pausedimport identity differs'
  # Completed recordings/rawpixels/trace/reports already lie outside profile.
  row['profileCleanup']=archive_profile(folder,out);verify();save();print(index,variant,'complete; profile extracted/cleaned',flush=True)
 report['passed']=True;report['end']=time.time();save()
except BaseException as error:
 report['error']={'type':type(error).__name__,'message':str(error)};report['end']=time.time();save();raise
