from pathlib import Path
import os,subprocess,json,hashlib,time,signal,shutil
r=Path.cwd();b=r/'.context/jit-sw-cache-v3';out=b/'run';out.mkdir(exist_ok=False)
assert not (b/'profile').exists(),'Fresh SW profile required'
pins=json.loads((b/'site-pins.json').read_text());sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
files=['runner.mjs','run-pair.py','site-pins.json','cache-server.mjs','frame-packet.mjs','frames.mjs','check-receipt.py','analyze-cache.py','candidate/web/setup.html','candidate/web/install-wait.mjs','install-wait.mjs']
sourcePins={n:sha(b/n) for n in files};refs=Path('.context/jit-return-visit-v3/references');referencePins={n:sha(refs/n) for n in ['C0-home.rgba','C0-settings.rgba']}
helper=r/'docs/traces/browser-first-use/cdp.mjs';helperSha=sha(helper);chrome=Path('/opt/google/chrome/chrome');chromeSha=sha(chrome)
report=dict(passed=False,scope='Two fresh Chrome processes, one profile, true offline CacheStorage and real autosave. Functional only; no speed claim or automatic retry.',sourcePins=sourcePins,referencePins=referencePins,sitePins=pins,helperSha256=helperSha,chromeSha256=chromeSha,runs=[],start=time.time())
def save():(out/'series.json').write_text(json.dumps(report,indent=2)+'\n')
def verify():
 for n,h in sourcePins.items():assert sha(b/n)==h,n
 for n,h in pins['sites']['candidate'].items():assert sha(b/'candidate/web'/n)==h,n
 for n,h in referencePins.items():assert sha(refs/n)==h,n
 assert sha(helper)==helperSha and sha(chrome)==chromeSha
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
for index,phase in enumerate(['train','return'],1):
 folder=out/f'{index}-{phase}';folder.mkdir();verify();start=time.time();freeBefore=shutil.disk_usage(r).free;assert freeBefore>=5*1024**3,('Insufficient disk headroom',freeBefore)
 with (folder/'run.log').open('w') as log:
  child=subprocess.Popen(['node',str(b/'runner.mjs')],cwd=r,env=dict(os.environ,PHASE=phase,ARTIFACT_DIR=str(folder)),stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
  try:code=child.wait(timeout=240)
  except subprocess.TimeoutExpired:
   report['timeoutCleanup']=cleanupScoped(folder,child.pid);os.killpg(child.pid,signal.SIGKILL);code=child.wait();report['failure']='240-second process cap'
 row=dict(index=index,phase=phase,exitCode=code,elapsedSeconds=time.time()-start,freeBytesBefore=freeBefore,freeBytesAfter=shutil.disk_usage(r).free);report['runs'].append(row)
 if (folder/'report.json').exists():row['report']=json.loads((folder/'report.json').read_text())
 save();closed=receiptPids(folder);assert closed,'No scoped browser receipt'
 for p in closed:assert not matches(p),'Scoped Chrome survived: '+str(p)
 row['scopedProcessesClosed']=closed;save();assert code==0 and row.get('report',{}).get('passed'),row
 if phase=='train':assert (b/'saved-after-training.json').exists()
 verify();print(phase,'PASS',flush=True)
report['passed']=True;report['end']=time.time();save()
