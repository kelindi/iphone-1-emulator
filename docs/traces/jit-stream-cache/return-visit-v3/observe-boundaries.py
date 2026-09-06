"""Observation only: leaves frozen runner, phone and timing rules unchanged."""
from pathlib import Path
import json,time,datetime,subprocess,hashlib,os
b=Path(__file__).resolve().parent;out=b/'system-boundaries';out.mkdir(exist_ok=False);seen=set();deadline=time.monotonic()+1100
(out/'observer-pin.json').write_text(json.dumps(dict(sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),pid=os.getpid(),scope='System receipts on observed arm-directory/series transitions; up to poll/scheduling delay, no guest instrumentation. ps percentages are lifetime averages, not interval utilization.'),indent=2)+'\n')
def capture(label):
 raw=subprocess.run(['ps','-eo','pid,ppid,pcpu,etime,comm','--sort=-pcpu'],capture_output=True,text=True,check=True).stdout
 rows=[]
 for p in Path('/proc').iterdir():
  if not p.name.isdigit():continue
  try:
   s=(p/'stat').read_text();f=s[s.rindex(')')+2:].split();rows.append(dict(pid=int(p.name),ppid=int(f[1]),name=s[s.index('(')+1:s.rindex(')')],utimeTicks=int(f[11]),stimeTicks=int(f[12]),startTicks=int(f[19])))
  except (FileNotFoundError,ProcessLookupError,PermissionError):pass
 (out/(label+'.json')).write_text(json.dumps(dict(utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),label=label,ps=raw,processes=rows),indent=2)+'\n')
capture('before-series')
while time.monotonic()<deadline:
 for p in sorted((b/'measured').glob('[1-4]-*')):
  if p.name not in seen:seen.add(p.name);capture('start-'+p.name)
 p=b/'measured/series.json'
 if p.exists():
  try:r=json.loads(p.read_text())
  except json.JSONDecodeError:r={}
  for run in r.get('runs',[]):
   key='completed-'+str(run['index'])+'-'+run['variant']
   if key not in seen:seen.add(key);capture(key)
  if r.get('passed') or r.get('failure') or any(x.get('exitCode',0)!=0 for x in r.get('runs',[])):
   capture('series-end');break
 time.sleep(1)
else:capture('observer-bound-reached')
