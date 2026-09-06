from pathlib import Path
import os,subprocess,json,hashlib,time,signal,runpy
r=Path.cwd();base=r/'.context/jit-mixed-coverage';out=base/'fourth-continuation';out.mkdir(exist_ok=False);folder=out/'4-original';folder.mkdir()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();frozen=json.loads((base/'fourth-frozen.json').read_text());pins=json.loads((base/'site-pins.json').read_text());prior=json.loads((base/'measured/series.json').read_text())
report={'passed':False,'scope':'Separately authorized continuation of ONLY unstarted fourth original; first failed series and first3 observations unchanged. HTTP/Code cache cleanup gap disclosed. No repeated guest.','frozen':frozen,'priorSeriesPassed':prior['passed'],'started':time.time(),'armCapSeconds':240}
def save():(out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
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

for name,h in frozen['files'].items():assert sha(r/name)==h,name
assert len(prior['runs'])==3 and prior['passed']is False
assert not (base/'measured/4-original/profile').exists()
for row in prior['runs']:
 assert row['exitCode']==0 and row['report']['passed'] and all(not matches(p)for p in row['scopedProcessesClosed'])
for variant in ['original','candidate']:
 for name,h in pins['sites'][variant].items():assert sha(base/'sites'/variant/name)==h,(variant,name)
report['freeBefore']=os.statvfs(r).f_bavail*os.statvfs(r).f_frsize
report['headroom']={'perArmFloor':int(4.5*1024**3),'observedLargestPriorArmBytes':max(x['artifactAllocated']for x in json.loads((base/'measured/disk-stop.json').read_text())['runs']),'chromeReserveBytes':4*1024**3,'sixGiBRole':'Original four-arm series start headroom, not per-arm rule'}
report['headroom']['expectedAfter']=report['freeBefore']-report['headroom']['observedLargestPriorArmBytes']
save();assert report['freeBefore']>report['headroom']['perArmFloor'];assert report['headroom']['expectedAfter']>report['headroom']['chromeReserveBytes']
(out/'before-system.txt').write_text(subprocess.check_output(['ps','-eo','pid,ppid,lstart,pcpu,comm,args'],text=True))
with(folder/'run.log').open('w')as log:
 child=subprocess.Popen(['node',str(base/'run-browser.mjs')],cwd=r,env=dict(os.environ,PHASE='measured',VARIANT='original',ARTIFACT_DIR=str(folder)),stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
 try:code=child.wait(timeout=240)
 except subprocess.TimeoutExpired:
  report['timeoutCleanup']=cleanup(folder,child.pid);os.killpg(child.pid,signal.SIGKILL);code=child.wait();report['failure']='Original240-second per-arm bound'
report['exitCode']=code;report['elapsedSeconds']=time.time()-report['started'];report['freeAfter']=os.statvfs(r).f_bavail*os.statvfs(r).f_frsize
if(folder/'report.json').exists():report['browser']=json.loads((folder/'report.json').read_text())
closed=receiptPids(folder)
if code!=0 or any(matches(p)for p in closed):report['failureCleanup']=cleanup(folder,child.pid)
report['scopedProcessesClosed']=closed;save();assert closed and all(not matches(p)for p in closed);assert code==0 and report.get('browser',{}).get('passed')
assert report['browser']['traceComplete']['dataLossOccurred']is False
first=prior['runs'][0]['report']['C0Import'];current=report['browser']['C0Import'];assert first['cycles']==current['cycles']and first['ticks']==current['ticks']
analyze=runpy.run_path(str(base/'analyze-compile.py'))['analyze'];report['compilation']=analyze(json.loads((folder/'trace.json').read_text())['traceEvents']);save();assert not report['compilation']['missingThreadFields']
report['compilePairs']=[]
for control,candidate in [(prior['runs'][0]['compilation'],prior['runs'][1]['compilation']),(report['compilation'],prior['runs'][2]['compilation'])]:
 limit=control['cpuLowerMs']*1.25+100;pair={'controlLowerMs':control['cpuLowerMs'],'candidateUpperMs':candidate['cpuUpperMs'],'limitMs':limit,'passed':not(control['missingThreadFields']or candidate['missingThreadFields'])and candidate['cpuUpperMs']<=limit};report['compilePairs'].append(pair);save();assert pair['passed'],'Unchanged compilation CPU gate failed'
for name,h in frozen['files'].items():assert sha(r/name)==h,name
(out/'after-system.txt').write_text(subprocess.check_output(['ps','-eo','pid,ppid,lstart,pcpu,comm,args'],text=True))
report['passed']=True;report['ended']=time.time();save();print(json.dumps({'passed':True,'elapsed':report['elapsedSeconds'],'compilation':report['compilation']}))
