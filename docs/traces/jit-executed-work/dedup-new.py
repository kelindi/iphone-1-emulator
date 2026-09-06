"""Only new byte-identical gzip outputs can be atomically replaced by hardlinks."""
from pathlib import Path
import sys,json,hashlib,os

def replace_matching(new,old,root,allowed):
    new,old,root=Path(new),Path(old),Path(root).resolve()
    assert not new.is_symlink(),'new output symlink';new=new.resolve();old=old.resolve()
    assert root in new.parents and new.name=='raw.snap.gz','foreign new output'
    assert old in {Path(p).resolve()for p in allowed},'unapproved reference'
    def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
    a,b=sha(new),sha(old);assert a==b,'Different gzip encoding; retain new output and stop before more work'
    tmp=new.with_name('raw.hardlink-stage');assert not tmp.exists();os.link(old,tmp);os.replace(tmp,new);assert sha(new)==a and new.stat().st_ino==old.stat().st_ino
    receipt={'new':str(new),'retained':str(old),'beforeSha256':a,'afterSha256':sha(new),'gzipBytes':new.stat().st_size,'sameInode':True,'scope':'Only new complete matched output atomically linked; retained control bytes/path untouched.'}
    (new.parent/'dedup.json').write_text(json.dumps(receipt,indent=2)+'\n');return receipt
if __name__=='__main__':
    allowed=[Path('.context/jit-mixed-coverage')/f'candidate-raw-{n}.snap.gz'for n in [25000000,50000000,75000000,100000000]]
    replace_matching(sys.argv[1],sys.argv[2],'.context/jit-executed-work/replay',allowed)
