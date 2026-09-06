"""Two fresh browser processes sharing one new persistent profile. Explicit quiet authorization required."""
from pathlib import Path
import os,subprocess,json,hashlib,time,signal
r=Path.cwd();base=r/'.context/jit-stream-cache';out=base/'mechanism';out.mkdir(exist_ok=False)
profile=base/'persistent-profile';assert not profile.exists(),'Refuse pre-existing mechanism profile'
pins=json.loads((base/'browser-pins.json').read_text());sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
sourceFiles=['run-browser.mjs','run-pair.py','cache-server.mjs','browser-pins.json','review-screens.py','prepare-browser.py']
sourcePins={n:sha(base/n) for n in sourceFiles}
helper=r/'docs/traces/browser-first-use/cdp.mjs';helperSha=sha(helper)
report={'passed':False,'scope':'Mechanism-only HTTP native code cache; no speed acceptance','sourcePins':sourcePins,'cdpHelperSha256':helperSha,'runs':[],'start':time.time()}
def save(): (out/'pair.json').write_text(json.dumps(report,indent=2)+'\n')
def verify():
 for name,value in pins['site'].items():assert sha(base/'site'/name)==value,name
 for name,value in sourcePins.items():assert sha(base/name)==value,name
 assert sha(helper)==helperSha
 for name,value in pins['references'].items():assert sha(base/'references'/name)==value,name
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
 targets=receiptPids(folder)
 # Also capture current node descendants if timeout preceded the first receipt.
 allp=[p for d in Path('/proc').iterdir() if d.name.isdigit() for p in [proc(int(d.name))] if p]
 scoped={nodepid};changed=True
 while changed:
  changed=False
  for p in allp:
   if p['ppid'] in scoped and p['pid'] not in scoped:scoped.add(p['pid']);changed=True
 targets+= [p for p in allp if p['pid'] in scoped and p['pid']!=nodepid]
 for sig in [signal.SIGTERM,signal.SIGKILL]:
  for p in targets:
   if matches(p):
    try:os.kill(p['pid'],sig)
    except ProcessLookupError:pass
  time.sleep(.25)
 return targets
save();verify()
for i in [1,2]:
 folder=out/f'{i}-persistent'
 folder.mkdir();env=dict(os.environ,ARTIFACT_DIR=str(folder),PROFILE_DIR=str(profile))
 verify();start=time.time()
 with (folder/'run.log').open('w') as log:
  child=subprocess.Popen(['node',str(base/'run-browser.mjs')],cwd=r,env=env,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
  try:code=child.wait(timeout=240)
  except subprocess.TimeoutExpired:
   report['timeoutCleanup']=cleanupScoped(folder,child.pid);os.killpg(child.pid,signal.SIGKILL);code=child.wait();report['failure']='240-second process bound'
 row={'session':i,'exitCode':code,'elapsedSeconds':time.time()-start};report['runs'].append(row)
 if (folder/'report.json').exists():row['report']=json.loads((folder/'report.json').read_text())
 save();assert code==0 and row.get('report',{}).get('passed'),row
 assert row['report']['traceComplete'].get('dataLossOccurred') is False
 # No old browser process may survive and supply an in-process module cache.
 closed=receiptPids(folder);assert closed,'Missing process receipts'
 for p in closed:assert not matches(p),'Scoped Chrome process survived clean close: '+str(p)
 row['scopedProcessesClosed']=closed;save();verify()
 if i==2:
  previous=report['runs'][0]['report']['C0Import'];current=row['report']['C0Import']
  assert previous['cycles']==current['cycles'] and previous['ticks']==current['ticks'],'Paused imported guest identity changed'
 print('Session',i,'complete',flush=True)
report['passed']=True;report['end']=time.time();save()
