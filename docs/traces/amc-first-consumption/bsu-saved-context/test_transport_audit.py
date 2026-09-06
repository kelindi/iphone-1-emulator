from pathlib import Path
import tempfile,shutil,runpy,struct,json
base=Path('.context/amc-first/stage-s');audit=runpy.run_path(str(base/'transport-audit.py'))['audit_stage'];checks=0
with tempfile.TemporaryDirectory(dir=base) as tmp:
 d=Path(tmp)
 for p in (base/'service-artifacts').glob('transport-synthetic*'):shutil.copy2(p,d/p.name)
 # Real collector receipt with explicitly synthetic valid manager geometry.
 path=d/'transport-synthetic-manager.bin';words=[0x200000,0x240000,0x200000,0x201000,0,4096,0,0,0x40000,0,0,44100,0,0,0,0];path.write_bytes(struct.pack('<16I',*words))
 assert audit(d,'synthetic')['managerAvailable'];checks+=1
 def bad(name,mutate,expected):
  global checks
  p=d/name;old=p.read_bytes();p.write_bytes(mutate(old))
  try:audit(d,'synthetic');raise RuntimeError('auditor accepted mutant')
  except AssertionError as e:assert expected in str(e),(expected,str(e));checks+=1
  finally:p.write_bytes(old)
 bad('transport-synthetic-values.bin',lambda b:bytes([b[0]^1])+b[1:],'transport scalar receipt differs')
 bad('transport-synthetic-manager.bin',lambda b:b[:3]+bytes([b[3]^128])+b[4:],'manager bounds differ')
 def unavailable(b):
  d=json.loads(b);d['managerAvailable']=False;return json.dumps(d).encode()
 bad('transport-synthetic.json',unavailable,'unavailable manager fabricated')
 bad('transport-synthetic-lli.bin',lambda b:b'\1'+b[1:],'unavailable LLI fabricated')
 def lli(b):
  d=json.loads(b);d['lliAvailableMask']=1;return json.dumps(d).encode()
 bad('transport-synthetic.json',lli,'early LLI fabricated')
print('transport scalar/manager/LLI external audit:',checks,'PASS; synthetic only')
