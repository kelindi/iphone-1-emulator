import tempfile,gzip,runpy
from pathlib import Path
replace=runpy.run_path('.context/jit-executed-work/dedup-new.py')['replace_matching']
checks=0
with tempfile.TemporaryDirectory(prefix='executed-work-dedup-')as t:
 p=Path(t);root=p/'owned';root.mkdir();old=p/'control.gz';old.write_bytes(gzip.compress(b'immutable raw state',mtime=0));original=old.read_bytes();new=root/'raw.snap.gz';new.write_bytes(original)
 r=replace(new,old,root,[old]);assert r['sameInode'] and new.read_bytes()==original and old.read_bytes()==original;checks+=1
 new.unlink();new.write_bytes(b'different');before=new.read_bytes()
 try:replace(new,old,root,[old]);raise RuntimeError('accepted mismatch')
 except AssertionError:assert new.read_bytes()==before and old.read_bytes()==original;checks+=1
 for name,ref,scope in [(new,old,root/'foreign'),(new,old,root)]:
  try:replace(name,ref,scope,[]);raise RuntimeError('accepted foreign')
  except AssertionError:checks+=1
 new.unlink();new.symlink_to(old)
 try:replace(new,old,root,[old]);raise RuntimeError('accepted symlink')
 except AssertionError:checks+=1
print('PASS',checks,'atomic dedup controls; no retained experiment file touched')
