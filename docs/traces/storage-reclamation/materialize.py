"""Restore one archived historical test export from its .archived.json receipt.
Run from the repository root: python3 .../materialize.py <receipt>.
"""
import gzip,hashlib,json,os,sys,tempfile
from pathlib import Path
receipt=json.loads(Path(sys.argv[1]).read_text());target=Path(receipt['originalPath'])
def digest(stream):
 h=hashlib.sha256();n=0
 for b in iter(lambda:stream.read(1024*1024),b''):h.update(b);n+=len(b)
 return n,h.hexdigest()
expected=(receipt['rawBytes'],receipt['rawSha256'])
if target.exists():
 with target.open('rb')as f:assert digest(f)==expected,'Existing file differs; refusing to overwrite'
else:
 fd,name=tempfile.mkstemp(prefix=target.name+'.restore-',dir=target.parent)
 try:
  with os.fdopen(fd,'wb')as out,gzip.open(receipt['gzipPath'],'rb')as source:
   for b in iter(lambda:source.read(1024*1024),b''):out.write(b)
  with open(name,'rb')as f:assert digest(f)==expected,'Archive identity mismatch'
  os.link(name,target)  # Atomic creation; never replace a concurrently created file.
 finally:
  if os.path.exists(name):os.unlink(name)
print(target)
