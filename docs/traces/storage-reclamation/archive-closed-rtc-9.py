from pathlib import Path
import gzip,hashlib,json,os,time,shutil
paths=[Path('.context/rtc-contract/browser-2/after-manual-change.snap'),Path('.context/rtc-write/browser-preview/run-1/before-picker.snap')]
def digest(stream):
 h=hashlib.sha256();n=0
 for b in iter(lambda:stream.read(1024*1024),b''):h.update(b);n+=len(b)
 return n,h.hexdigest()
r={'started':time.time(),'scope':'Lossless archival of closed completed RTC browser-test snapshots only. No firmware, current fixture, IndexedDB or Service Worker deletion. Original bytes recoverable by gzip decompression.','freeBefore':shutil.disk_usage('.').free,'files':[]}
for p in paths:
 assert p.is_file() and not p.is_symlink()
 with p.open('rb')as f:size,sha=digest(f)
 out=p.with_suffix(p.suffix+'.gz');tmp=out.with_suffix(out.suffix+'.tmp');assert not out.exists() and not tmp.exists()
 with p.open('rb')as f,gzip.open(tmp,'wb',compresslevel=1)as z:shutil.copyfileobj(f,z,1024*1024)
 with gzip.open(tmp,'rb')as f:assert digest(f)==(size,sha)
 os.replace(tmp,out)
 row={'originalPath':str(p),'gzipPath':str(out),'rawBytes':size,'rawSha256':sha,'gzipBytes':out.stat().st_size}
 receipt=p.with_suffix(p.suffix+'.archived.json');receipt.write_text(json.dumps(row,indent=2)+'\n');p.unlink();r['files'].append(row)
 print('Archived',p,'saved',size-out.stat().st_size,flush=True)
r['ended']=time.time();r['freeAfter']=shutil.disk_usage('.').free
Path('docs/traces/storage-reclamation/archived-closed-rtc-9.json').write_text(json.dumps(r,indent=2)+'\n')
print('free',r['freeAfter'],flush=True)
