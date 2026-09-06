"""One retained guest's artifacts only; no emulation or source mutation."""
from pathlib import Path
import json,subprocess,sys,hashlib
b=Path(__file__).parent;r=b.parent/'run-1';out=b/'actual-negatives';out.mkdir(exist_ok=False);auditor=b/'binding-audit.py';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();results=[]
def change(operation):
 def f(data):j=json.loads(data);operation(j);return json.dumps(j).encode()
 return f
cases=[('fetch',change(lambda j:j.update(actualFetchExecuted=True)),'prefetch observation falsely retired'),('cp15',change(lambda j:j['cp15'].__setitem__(1,j['cp15'][1]^0x4000)),'actual binding CP15/reason differs'),('availability',change(lambda j:j['walks'][0].update(available=1)),'complete code availability differs'),('unavailable-bytes',change(lambda j:j['walks'][0].update(codeHex='01'+'00'*31)),'unavailable code fabricated')]
for i in (2,3):
 cases.append(('fault-'+str(i),change(lambda j,i=i:j['walks'][i]['spans'][0].__setitem__(3,5)),'cloned fault kind differs'))
 cases.append(('table-address-'+str(i),change(lambda j,i=i:j['walks'][i]['tables'][0].__setitem__(0,j['walks'][i]['tables'][0][0]^4)),'actual cloned page-table provenance differs'))
 cases.append(('table-value-'+str(i),change(lambda j,i=i:j['walks'][i]['tables'][1].__setitem__(1,2)),'cloned fault kind differs'))
for name,mutate,expected in cases:
 p=out/name;p.mkdir()
 for f in r.iterdir():
  if f.name.startswith('negative-') or f.name=='binding-audit-corrected.json':continue
  if f.name=='binding.json':(p/f.name).write_bytes(mutate(f.read_bytes()))
  else:(p/f.name).symlink_to(f.resolve(),target_is_directory=f.is_dir())
 command=[sys.executable,str(auditor),str(p)];log=out/(name+'.log')
 with log.open('w') as f:done=subprocess.run(command,stdout=f,stderr=subprocess.STDOUT,timeout=30)
 assert done.returncode and expected in log.read_text(),(name,log.read_text())
 results.append({'name':name,'expected':expected,'exit':done.returncode,'mutatedSHA256':sha(p/'binding.json'),'command':command})
report={'passed':True,'guestReexecuted':False,'auditorSHA256':sha(auditor),'originalBindingSHA256':sha(r/'binding.json'),'artifactNegatives':results};(b/'actual-negatives.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({'passed':True,'actualNegatives':len(results),'guestReexecuted':False}))
