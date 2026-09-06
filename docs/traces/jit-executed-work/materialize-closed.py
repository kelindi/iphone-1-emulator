"""Restore one archived closed output using its receipt; stages a new inode."""
import sys,pathlib,json,gzip,hashlib,shutil,os
def sha(p):
 h=hashlib.sha256()
 with open(p,'rb')as f:
  while b:=f.read(1024*1024):h.update(b)
 return h.hexdigest()
receipt=json.loads(pathlib.Path(sys.argv[1]).read_text());r=next(x for x in receipt['rows']if x['path']==sys.argv[2]);p=pathlib.Path(r['path']);assert not p.exists();a=pathlib.Path(r['archive']);assert sha(a)==r['gzipSha256'];t=pathlib.Path(str(p)+'.restoring');assert not t.exists()
with gzip.open(a,'rb')as src,t.open('xb')as dest:shutil.copyfileobj(src,dest)
assert t.stat().st_size==r['rawBytes'] and sha(t)==r['rawSha256'];os.replace(t,p)
