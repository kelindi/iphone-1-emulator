"""Audit only: immutable positive artifacts, ten separately changed witnesses."""
from pathlib import Path
import hashlib,json,re,subprocess,sys
base=Path(__file__).parent;run=base.parent/'run-1';root=base/'actual-negatives';root.mkdir(exist_ok=False)
audit=base/'suspend-audit.py';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
controls=[]
def flip(data):v=bytearray(data);v[123]^=1;return bytes(v)
def field(name,value):
 def f(data):j=json.loads(data);j[name]=value;return json.dumps(j).encode()
 return f
cases=[('queue','suspended-queue.bin',flip,'actual suspended queue differs'),('sram','suspended-sram.bin',flip,'actual suspended SRAM differs'),('active','suspend.json',field('activeInitialized',1),'active hardware was retained'),('owner','suspend.json',field('owned',0),'transaction incomplete'),('clock','suspend.json',field('clock',0x4fffe27f^0x10000),'final controller differs'),('fabricated-kernel','suspend.json',field('driverState',5),'unavailable final kernel state fabricated'),('driver-store','trace.log',lambda b:re.sub(rb'^SUSPEND DRIVER .*\n',b'',b,flags=re.M),'driver state receipt absent'),('successor','trace.log',lambda b:re.sub(rb'(SUSPEND RETIRED index0 .* afterPC)([0-9a-f]{8})',lambda m:m[1]+b'ffffffff',b),'suspension retirement differs'),('power','trace.log',lambda b:re.sub(rb'(SUSPEND WRITE index1 .* powerAfter)([0-9a-f]{8})',lambda m:m[1]+b'000012ec',b),'controller transition/order differs'),('service','suspended-control.bin',lambda b:b[:8]+(1).to_bytes(4,'little')+b[12:],'retained control ownership differs')]
for name,target,mutate,expected in cases:
 p=root/name;p.mkdir()
 for f in run.iterdir():
  if f.name.startswith('negative-') or f.name=='suspend-audit-corrected.json':continue
  if f.name==target:
   original=f.read_bytes();changed=mutate(original);assert original!=changed;(p/f.name).write_bytes(changed)
  else:(p/f.name).symlink_to(f.resolve(),target_is_directory=f.is_dir())
 log=root/(name+'.log');command=[sys.executable,str(audit),str(p)]
 with log.open('w') as out:r=subprocess.run(command,stdout=out,stderr=subprocess.STDOUT,timeout=30)
 assert r.returncode!=0 and expected in log.read_text(),name
 controls.append({'name':name,'target':target,'originalSHA256':sha(run/target),'mutatedSHA256':sha(p/target),'returncode':r.returncode,'expected':expected,'command':command})
report={'passed':True,'guestReexecuted':False,'auditorSHA256':sha(audit),'frozenSaveHelperSHA256':sha(base/'save-audit.py'),'actualNegatives':controls}
(base/'actual-negatives.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({'passed':True,'actualNegatives':len(controls),'guestReexecuted':False}))
