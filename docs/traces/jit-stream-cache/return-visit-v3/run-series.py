"""Functional training or separately authorized quiet ABBA; never retries failures."""
from pathlib import Path
import os,subprocess,json,hashlib,time,signal,sys
r=Path.cwd();base=r/'.context/jit-return-visit-v3';phase=sys.argv[1]
assert phase in ['training','measured']
out=base/phase;out.mkdir(exist_ok=False)
pins=json.loads((base/'site-pins.json').read_text());sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
sourceFiles=['run-browser.mjs','run-series.py','cache-server.mjs','site-pins.json','frames.mjs','frame-packet.mjs','check-receipt.py','analyze-cache.py']
sourcePins={n:sha(base/n) for n in sourceFiles};helper=r/'docs/traces/browser-first-use/cdp.mjs';helperSha=sha(helper)
order=['original','candidate'] if phase=='training' else ['original','candidate','candidate','original']
if phase=='training':
 for v in order:assert not (base/'profiles'/v).exists(),'Training profile already exists'
 assert not list((base/'references').glob('*')),'Original references already exist'
 referencePins={}
else:
 previous=json.loads((base/'training/series.json').read_text());assert previous['passed'] and len(previous['runs'])==2
 referencePins=previous['referencePins'];assert len(referencePins)==8
 for v in ['original','candidate']:assert (base/'profiles'/v).is_dir()
report=dict(passed=False,phase=phase,order=order,sourcePins=sourcePins,cdpHelperSha256=helperSha,sitePins=pins,referencePins=referencePins,runs=[],start=time.time())
def save():(out/'series.json').write_text(json.dumps(report,indent=2)+'\n')
def verify():
 for v in ['original','candidate']:
  for n,h in pins['sites'][v].items():assert sha(base/v/n)==h,(v,n)
 for n,h in sourcePins.items():assert sha(base/n)==h,n
 assert sha(helper)==helperSha
 for n,h in referencePins.items():assert sha(base/'references'/n)==h,n
 assert sha(r/pins['fixture']['path'])==pins['fixture']['sha256']
 assert sha(Path(pins['chrome']['path']))==pins['chrome']['sha256']
def proc(pid):
 try:
  raw=Path(f'/proc/{pid}/stat').read_text();fields=raw[raw.rindex(')')+2:].split(' ')
  return dict(pid=pid,ppid=int(fields[1]),startTicks=fields[19],exe=os.readlink(f'/proc/{pid}/exe'))
 except OSError:return None
def receiptPids(folder):
 p=folder/'process-receipt.json'
 if not p.exists():return []
 data=json.loads(p.read_text());return list({(p['pid'],p['startTicks']):p for rec in data['receipts'] for p in rec['processes']}.values())
def matches(p):
 now=proc(p['pid']);return now is not None and now['startTicks']==p['startTicks'] and now['exe']==p['exe']
def cleanupScoped(folder,nodepid):
 targets=receiptPids(folder);allp=[p for d in Path('/proc').iterdir() if d.name.isdigit() for p in [proc(int(d.name))] if p]
 scoped={nodepid};changed=True
 while changed:
  changed=False
  for p in allp:
   if p['ppid'] in scoped and p['pid'] not in scoped:scoped.add(p['pid']);changed=True
 targets+=[p for p in allp if p['pid'] in scoped and p['pid']!=nodepid]
 for sig in [signal.SIGTERM,signal.SIGKILL]:
  for p in targets:
   if matches(p):
    try:os.kill(p['pid'],sig)
    except ProcessLookupError:pass
  time.sleep(.25)
 return targets
save();verify()
for index,variant in enumerate(order,1):
 folder=out/f'{index}-{variant}';folder.mkdir();verify();start=time.time()
 with (folder/'run.log').open('w') as log:
  child=subprocess.Popen(['node',str(base/'run-browser.mjs')],cwd=r,env=dict(os.environ,PHASE=phase,VARIANT=variant,ARTIFACT_DIR=str(folder)),stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
  try:code=child.wait(timeout=240)
  except subprocess.TimeoutExpired:
   report['timeoutCleanup']=cleanupScoped(folder,child.pid);os.killpg(child.pid,signal.SIGKILL);code=child.wait();report['failure']='240-second process bound'
 row=dict(index=index,variant=variant,exitCode=code,elapsedSeconds=time.time()-start);report['runs'].append(row)
 if (folder/'report.json').exists():row['report']=json.loads((folder/'report.json').read_text())
 save()
 closed=receiptPids(folder);assert closed,'Missing scoped process receipt'
 for p in closed:assert not matches(p),'Scoped Chrome survived: '+str(p)
 row['scopedProcessesClosed']=closed;save()
 assert code==0 and row.get('report',{}).get('passed'),row
 assert row['report']['traceComplete'].get('dataLossOccurred') is False
 if phase=='training' and variant=='original':
  referencePins={p.name:sha(p) for p in sorted((base/'references').iterdir())};assert len(referencePins)==8
  report['referencePins']=referencePins
 if index>1:
  initial=report['runs'][0]['report']['C0Import'];current=row['report']['C0Import']
  assert initial['cycles']==current['cycles'] and initial['ticks']==current['ticks'],'Imported guest identity changed'
 if phase=='measured':
  prior=next(x['report']['C0Import'] for x in previous['runs'] if x['variant']==variant);current=row['report']['C0Import']
  assert prior['cycles']==current['cycles'] and prior['ticks']==current['ticks'],'Training/return guest identity changed'
 verify();save();print(phase,index,variant,'complete',flush=True)
report['passed']=True;report['end']=time.time();save()
