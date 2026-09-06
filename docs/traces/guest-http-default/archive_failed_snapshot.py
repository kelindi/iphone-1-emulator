"""Preserve exact failed-form snapshot bytes before removing only the raw duplicate."""
import gzip,hashlib,json,os,time
from pathlib import Path
p=Path('.context/guest-http-default/browser-run-1/final-home.snap');out=p.with_suffix(p.suffix+'.gz');receipt=p.parent/'failed-snapshot-archive.json'
expected='c407b3734919c76dc0eae153b1bb70366969c4548e3fa2232716a1bc3395e693'
assert p.stat().st_size==260767808 and not out.exists() and not receipt.exists()
def hash_file(path):
 h=hashlib.sha256()
 with open(path,'rb') as f:
  for b in iter(lambda:f.read(1048576),b''):h.update(b)
 return h.hexdigest()
assert hash_file(p)==expected
h=hashlib.sha256();size=0;t=time.monotonic()
with p.open('rb') as f,out.open('xb') as raw:
 with gzip.GzipFile(filename='',mode='wb',fileobj=raw,mtime=0,compresslevel=1) as g:
  for b in iter(lambda:f.read(1048576),b''):g.write(b)
 raw.flush();os.fsync(raw.fileno())
with gzip.open(out,'rb') as g:
 for b in iter(lambda:g.read(1048576),b''):size+=len(b);h.update(b)
assert size==260767808 and h.hexdigest()==expected and hash_file(p)==expected
r={'state':'FAILED SAFARI FORM, NOT HOME; generic original filename retained','rawPath':str(p),'rawBytes':size,'rawSHA256':expected,'gzipPath':str(out),'gzipBytes':out.stat().st_size,'gzipSHA256':hash_file(out),'roundtripExact':True,'readersReleasedBy':'graphics_acceleration and root; all our readers closed','elapsedSeconds':time.monotonic()-t,'rawRemoved':False}
receipt.write_text(json.dumps(r,indent=2)+'\n');p.unlink();r['rawRemoved']=True;receipt.write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r,indent=2))
