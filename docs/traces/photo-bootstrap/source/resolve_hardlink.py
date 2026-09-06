"""Read-only actual OS1 HFS hard links; Apple TN1150 Hard Links contract."""
import hashlib,struct

def resolve(volume,files,path,expected):
 off,row=files[path];data=row[off:]
 assert data[48:56]==b'hlnkhfs+', 'actual own final path must be a hard link'
 ref=struct.unpack_from('>I',data,44)[0];assert ref!=0
 target='/'+chr(0)*4+'HFS+ Private Data/iNode'+str(ref)
 toff,trow=files[target];inode=trow[toff:]
 assert struct.unpack_from('>H',inode,42)[0]==0o100644
 assert struct.unpack_from('>I',inode,44)[0]==1, 'one surviving original link after temp unlink'
 assert inode[48:56]!=b'hlnkhfs+'
 # Fork class is supplied by the independent HFS reader, not the installer.
 import hfsx_extract as H
 fork=H.Fork(inode,88);assert fork.covered()==fork.total_blocks
 payload=volume.read_fork(fork)
 assert len(payload)==expected['bytes'] and hashlib.sha256(payload).hexdigest()==expected['sha256']
 return {'path':path,'target':target,'linkReference':ref,'targetCNID':H.u32(inode,8),
         'bytes':len(payload),'sha256':expected['sha256'],'mode':oct(H.u16(inode,42))}
