from pathlib import Path
import hashlib,json,subprocess,os
roots=['.context/clock','.context/graphics-speed','.context/organizer-debug','.context/graphics-final','.context/jit-v3','.context/jit-index','.context/jit-v4','.context/jit-experiment','.context/legacy-upgrade']
def digest(p):
 h=hashlib.sha256()
 with p.open("rb") as f:
  for chunk in iter(lambda:f.read(4*1024*1024),b""): h.update(chunk)
 return h.hexdigest()
rows=[]; seen=set()
for root in roots:
 for p in sorted(Path(root).rglob('*.snap')):
  st=p.stat()
  if st.st_size<100_000_000 or (st.st_dev,st.st_ino) in seen:continue
  seen.add((st.st_dev,st.st_ino))
  before=digest(p)
  r=subprocess.run(['fallocate','--dig-holes',str(p)],capture_output=True,text=True)
  after=digest(p)
  assert before==after,(p,before,after)
  row={'path':str(p),'bytes':st.st_size,'beforeAllocated':st.st_blocks*512,'afterAllocated':p.stat().st_blocks*512,'sha256':before,'exitCode':r.returncode,'error':r.stderr}
  rows.append(row)
  Path('.context/sparsify-frozen-snapshots.json').write_text(json.dumps({'byteIdentityVerified':True,'files':rows},indent=2)+'\n')
  print(json.dumps(row),flush=True)
print('RECLAIMED',sum(r['beforeAllocated']-r['afterAllocated'] for r in rows),flush=True)
