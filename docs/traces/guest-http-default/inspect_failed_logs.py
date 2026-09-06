"""Artifact-only RAMDisk inspection; original v39 loader, anonymous RAM backing, no guest instructions."""
import hashlib,json,os,subprocess,sys,time
from pathlib import Path
sys.path.insert(0,str(Path('tools/firmware').resolve()))
import rootfs_work as R
H=R.H
base=Path('.context/guest-http-default'); out=base/'browser-run-1/guest-log-audit';out.mkdir(exist_ok=False)
def sha(p):
 h=hashlib.sha256()
 with Path(p).open('rb') as f:
  for b in iter(lambda:f.read(1048576),b''):h.update(b)
 return h.hexdigest()
snap=base/'browser-run-1/final-home.snap'; expected='c407b3734919c76dc0eae153b1bb70366969c4548e3fa2232716a1bc3395e693'
assert sha(snap)==expected
fd=os.memfd_create('read-only-failed-form-ramdisk')
try:
 with (out/'extract.stderr').open('wb') as e:
  p=subprocess.run([str(base/'extract_failed_ramdisk'),str(snap)],stdout=fd,stderr=e,timeout=30)
 assert p.returncode==0 and os.fstat(fd).st_size==0x0a800000
 w=R.Work('/proc/self/fd/'+str(fd)); rows=[];folders={2:''}
 for _,node in w._leaf_chain():
  for _,_,raw in w._leaf_records(node):
   r=H.parse_record(raw)
   if r:
    parent,name,typ,off,raw=r
    if typ==1:folders[H.u32(raw,off+8)]=(parent,name)
    if typ==2:rows.append((parent,name,off,raw))
 def path(parent):
  parts=[];seen=set()
  while parent!=2:
   assert parent not in seen and parent in folders
   seen.add(parent);parent,name=folders[parent];parts.append(name)
  return '/'+('/'.join(reversed(parts))+'/') if parts else '/'
 selected=[];catalog=[]
 for parent,name,off,raw in rows:
  full=path(parent)+name;fork=H.Fork(raw,off+88)
  catalog.append({'path':full,'logicalBytes':fork.logical,'coveredBlocks':fork.covered(),'totalBlocks':fork.total_blocks})
  wanted=('/Logs/' in full or '/logs/' in full or '/log/' in full or 'CrashReporter' in full or 'local-demo' in name)
  if wanted:
   row={'path':full,'logicalBytes':fork.logical,'completeExtents':fork.covered()==fork.total_blocks}
   if row['completeExtents'] and fork.logical<=8*1024*1024:
    data=w.vol.read_fork(fork);assert len(data)==fork.logical
    row.update(sha256=hashlib.sha256(data).hexdigest(),artifact=str(len(selected))+'.bin')
    (out/row['artifact']).write_bytes(data)
   selected.append(row)
 report={'snapshotSHA256':expected,'ramdiskPhysicalBase':'085d0000','ramdiskBytes':os.fstat(fd).st_size,'scope':'Original loader; catalog and file backing bytes only, no guest execution. Dirty filesystem buffers may not yet be reflected in backing bytes. Missing logs do not prove no event.','catalogFiles':len(catalog),'selected':selected,'pins':{str(p):sha(p) for p in [base/'extract_failed_ramdisk.c',base/'extract_failed_ramdisk',Path(__file__)]}}
 (out/'catalog.json').write_text(json.dumps(catalog,indent=2)+'\n');(out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
finally:os.close(fd)
