from pathlib import Path
import subprocess,os,signal,json,time,hashlib
root=Path.cwd();p=root/'.context/jit-fallback-census';out=p/'browser-1';out.mkdir(exist_ok=True)
assert not (out/'report.json').exists(),'No repeated browser run allowed'
pins=json.loads((p/'browser-harness-pins.json').read_text())
for row in pins['files']:assert hashlib.sha256((root/row['path']).read_bytes()).hexdigest()==row['sha256'],row['path']
assert os.statvfs(p).f_bavail*os.statvfs(p).f_frsize>=5*1024**3,'5GiB disk floor'
with (out/'stdout.log').open('w') as stdout,(out/'stderr.log').open('w') as stderr:
 child=subprocess.Popen(['node',str(p/'browser.mjs')],stdout=stdout,stderr=stderr,start_new_session=True)
 (out/'launcher.json').write_text(json.dumps({'pid':child.pid,'started':time.time(),'capSeconds':240,'methodPins':pins},indent=2)+'\n')
 timedout=False
 try:code=child.wait(timeout=240)
 except subprocess.TimeoutExpired:timedout=True;os.killpg(child.pid,signal.SIGTERM)
 if timedout:
  try:child.wait(timeout=5)
  except subprocess.TimeoutExpired:os.killpg(child.pid,signal.SIGKILL);child.wait()
 receipts=json.loads((out/'process-receipt.json').read_text()) if (out/'process-receipt.json').exists() else []
 identities={(x['pid'],x['startTicks']) for r in receipts for x in r['processes']};checks=[]
 for pid,start in identities:
  def live():
   try:
    raw=Path('/proc',str(pid),'stat').read_text();f=raw[raw.rindex(')')+2:].split();return f[19]==start and f[0]!='Z'
   except FileNotFoundError:return False
  forced=False
  if live():forced=True;os.kill(pid,signal.SIGTERM)
  deadline=time.monotonic()+3
  while live() and time.monotonic()<deadline:time.sleep(.05)
  if live():os.kill(pid,signal.SIGKILL);time.sleep(.1)
  checks.append({'pid':pid,'startTicks':start,'closed':not live(),'forced':forced})
 result={'timeout':timedout,'exitCode':child.returncode,'identities':checks,'allClosed':bool(checks) and all(x['closed'] for x in checks),'ended':time.time()}
 (out/'cleanup.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
 assert not timedout and child.returncode==0 and result['allClosed'] and not any(x['forced'] for x in checks),'Browser/cap/scoped-cleanup gate failed'
