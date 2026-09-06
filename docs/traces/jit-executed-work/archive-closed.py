"""Archive explicitly approved closed outputs; never overwrite existing archives/inodes."""
import pathlib,gzip,hashlib,json,os,shutil,sys,time
base=pathlib.Path('.context/jit-executed-work');receipt=base/('closed-archive-'+str(time.time_ns())+'.json')
rows=[];result={'freeBefore':shutil.disk_usage('.').free,'rows':rows}
def sha(p):
 h=hashlib.sha256()
 with open(p,'rb')as f:
  while b:=f.read(1024*1024):h.update(b)
 return h.hexdigest()
for name in sys.argv[1:]:
 p=pathlib.Path(name);s=p.stat();assert s.st_nlink==1 and not p.is_symlink();dest=pathlib.Path(str(p)+'.gz');assert not dest.exists();tmp=pathlib.Path(str(dest)+'.archiving');assert not tmp.exists()
 rawsha=sha(p)
 with p.open('rb')as src,tmp.open('xb')as f:
  with gzip.GzipFile(filename='',mode='wb',fileobj=f,compresslevel=1,mtime=0)as z:shutil.copyfileobj(src,z,1024*1024)
 h=hashlib.sha256();n=0
 with gzip.open(tmp,'rb')as z:
  while b:=z.read(1024*1024):h.update(b);n+=len(b)
 assert n==s.st_size and h.hexdigest()==rawsha and sha(p)==rawsha
 os.replace(tmp,dest)
 row={'path':str(p),'archive':str(dest),'rawBytes':n,'rawSha256':rawsha,'gzipSha256':sha(dest),'allocatedBefore':s.st_blocks*512,'gzipAllocated':dest.stat().st_blocks*512,'inodeBefore':s.st_ino,'linksBefore':s.st_nlink,'roundtripPassed':True}
 rows.append(row);receipt.write_text(json.dumps(result,indent=2))
 p.unlink();row['rawRemovedAfterProof']=True;result['freeAfter']=shutil.disk_usage('.').free;receipt.write_text(json.dumps(result,indent=2));print(json.dumps(row),flush=True)
print(json.dumps({'receipt':str(receipt),'freeAfter':shutil.disk_usage('.').free}),flush=True)
