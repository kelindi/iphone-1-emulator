"""One frozen fresh-profile ABBA.240seconds PER ARM; no retry."""
from pathlib import Path
import os,subprocess,json,hashlib,time,signal,runpy
r=Path.cwd();base=r/'.context/jit-mixed-coverage';out=base/'measured';out.mkdir(exist_ok=False)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();pins=json.loads((base/'site-pins.json').read_text());frozen=json.loads((base/'browser-frozen.json').read_text());order=['original','candidate','candidate','original']
report={'passed':False,'order':order,'pins':pins,'frozen':frozen,'runs':[],'start':time.time(),'armCapSeconds':240,'seriesCapSeconds':1040}
def save():(out/'series.json').write_text(json.dumps(report,indent=2)+'\n')
def proc(pid):
 try:
  raw=Path(f'/proc/{pid}/stat').read_text();fields=raw[raw.rindex(')')+2:].split(' ');return dict(pid=pid,ppid=int(fields[1]),startTicks=fields[19],exe=os.readlink(f'/proc/{pid}/exe'))
 except OSError:return None
def receiptPids(folder):
 p=folder/'process-receipt.json'
 if not p.exists():return []
 data=json.loads(p.read_text());return list({(p['pid'],p['startTicks']):p for rec in data['receipts'] for p in rec['processes']}.values())
def matches(p):
 now=proc(p['pid']);return now is not None and now['startTicks']==p['startTicks'] and now['exe']==p['exe']
def cleanup(folder,nodepid):
 targets=receiptPids(folder);allp=[p for d in Path('/proc').iterdir() if d.name.isdigit() for p in [proc(int(d.name))] if p];scope={nodepid};changed=True
 while changed:
  changed=False
  for p in allp:
   if p['ppid'] in scope and p['pid'] not in scope:scope.add(p['pid']);changed=True
 targets+=[p for p in allp if p['pid'] in scope and p['pid']!=nodepid]
 for sig in [signal.SIGTERM,signal.SIGKILL]:
  for p in targets:
   if matches(p):
    try:os.kill(p['pid'],sig)
    except ProcessLookupError:pass
  time.sleep(.25)
 return targets
def verify():
 for n,h in frozen['files'].items():assert sha(r/n)==h,n
 for v in ['original','candidate']:
  for n,h in pins['sites'][v].items():assert sha(base/'sites'/v/n)==h,(v,n)
 assert sha(r/pins['fixture']['path'])==pins['fixture']['sha256']
 assert not any((out/f'{i}-{v}'/'profile').exists() for i,v in enumerate(order,1) if not(out/f'{i}-{v}'/'report.json').exists()),'Unexpected preexisting profile'
def system(label):
 (out/(label+'-system.txt')).write_text(subprocess.check_output(['ps','-eo','pid,ppid,lstart,pcpu,comm,args'],text=True))
 available=os.statvfs(r);return available.f_bavail*available.f_frsize
save();verify();assert system('before-series')>6*1024**3,'Requires>6GiB free before Chrome series'
for index,variant in enumerate(order,1):
 assert time.time()-report['start']<1040;folder=out/f'{index}-{variant}';folder.mkdir();verify();free=system(f'{index}-before');assert free>int(4.5*1024**3),'Blob reserve floor before arm';start=time.time()
 with (folder/'run.log').open('w') as log:
  child=subprocess.Popen(['node',str(base/'run-browser.mjs')],cwd=r,env=dict(os.environ,PHASE='measured',VARIANT=variant,ARTIFACT_DIR=str(folder)),stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
  try:code=child.wait(timeout=240)
  except subprocess.TimeoutExpired:
   report['timeoutCleanup']=cleanup(folder,child.pid);os.killpg(child.pid,signal.SIGKILL);code=child.wait();report['failure']='240-second per-arm bound'
 row={'index':index,'variant':variant,'exitCode':code,'elapsedSeconds':time.time()-start,'freeBefore':free,'freeAfter':system(f'{index}-after')};report['runs'].append(row)
 if(folder/'report.json').exists():row['report']=json.loads((folder/'report.json').read_text())
 closed=receiptPids(folder)
 if code!=0 or any(matches(p) for p in closed):row['failureCleanup']=cleanup(folder,child.pid)
 row['scopedProcessesClosed']=closed;save();assert closed and all(not matches(p)for p in closed),'Scoped cleanup failed'
 assert code==0 and row.get('report',{}).get('passed'),f'Arm{index} gate failed; see retained report'
 assert row['report']['traceComplete'].get('dataLossOccurred') is False
 analyzer=runpy.run_path(str(base/'analyze-compile.py'))['analyze'];row['compilation']=analyzer(json.loads((folder/'trace.json').read_text())['traceEvents']);save();assert row['compilation']['missingThreadFields']==0,'Compilation thread fields missing'
 if index==2:
  control=report['runs'][0]['compilation'];candidate=row['compilation'];assert candidate['cpuUpperMs']<=control['cpuLowerMs']*1.25+100,'Forward compilation CPU budget failed'
 if index==4:
  control=row['compilation'];candidate=report['runs'][2]['compilation'];assert candidate['cpuUpperMs']<=control['cpuLowerMs']*1.25+100,'Reverse compilation CPU budget failed'
 if index>1:
  first=report['runs'][0]['report']['C0Import'];current=row['report']['C0Import'];assert first['cycles']==current['cycles']and first['ticks']==current['ticks'],'Import identity differs'
 verify();save();print(index,variant,'complete',flush=True)
report['passed']=True;report['end']=time.time();save()
