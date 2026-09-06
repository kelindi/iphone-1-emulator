from pathlib import Path
import subprocess,os,signal,json,time,hashlib
root=Path.cwd();p=root/'.context/jit-executed-work/warm-browser';out=p/'run-1';assert not out.exists(),'No duplicate attempt';out.mkdir()
pins=json.loads((p/'frozen.json').read_text())
for row in pins['files']:assert hashlib.sha256((root/row['path']).read_bytes()).hexdigest()==row['sha256'],row['path']
assert os.statvfs(p).f_bavail*os.statvfs(p).f_frsize>=5.5*1024**3,'5.5GiB disk floor'
with (out/'stdout.log').open('w') as stdout,(out/'stderr.log').open('w') as stderr:
 child=subprocess.Popen(['node',str(p/'browser.mjs')],stdout=stdout,stderr=stderr,start_new_session=True)
 (out/'launcher.json').write_text(json.dumps({'pid':child.pid,'started':time.time(),'capSeconds':240,'methodPins':pins},indent=2)+'\n')
 timedout=False;observed={};deadline=time.monotonic()+240
 def procinfo(pid):
  try:
   raw=Path('/proc',str(pid),'stat').read_text();v=raw[raw.rindex(')')+2:].split();return {'pid':pid,'ppid':int(v[1]),'startTicks':v[19]}
  except (FileNotFoundError,ProcessLookupError):return None
 try:
  while child.poll()is None:
   rows=[v for f in Path('/proc').iterdir()if f.name.isdigit()for v in [procinfo(int(f.name))]if v];scope={child.pid};changed=True
   while changed:
    changed=False
    for row in rows:
     if row['ppid']in scope and row['pid']not in scope:scope.add(row['pid']);changed=True
   for row in rows:
    if row['pid']in scope:observed[(row['pid'],row['startTicks'])]=row
   (out/'controller-processes.json').write_text(json.dumps(list(observed.values()),indent=2))
   if time.monotonic()>deadline:raise subprocess.TimeoutExpired(child.args,240)
   time.sleep(.2)
  code=child.returncode
 except subprocess.TimeoutExpired:timedout=True;os.killpg(child.pid,signal.SIGTERM)
 if timedout:
  try:child.wait(timeout=5)
  except subprocess.TimeoutExpired:os.killpg(child.pid,signal.SIGKILL);child.wait()
 receipts=json.loads((out/'process-receipt.json').read_text()) if (out/'process-receipt.json').exists() else []
 identities={(x['pid'],x['startTicks']) for r in receipts for x in r['processes']}|set(observed);checks=[]
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
