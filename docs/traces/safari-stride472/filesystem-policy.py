from pathlib import Path
import mmap,gzip,sys,hashlib,json,plistlib,struct,io
sys.path.insert(0,'tools/firmware');import hfsx_extract as H
class Slice:
 def __init__(self,f,base):self.f=f;self.base=base
 def seek(self,o):self.f.seek(self.base+o)
 def read(self,n):return self.f.read(n)
def inspect(f,base,label):
 v=H.Volume.__new__(H.Volume);v.f=Slice(f,base);v.f.seek(1024);vh=v.f.read(512);assert vh[:2] in (b'HX',b'H+')
 v.block_size=H.u32(vh,40);v.total_blocks=H.u32(vh,44);v.catalog=H.Fork(vh,272);assert v.catalog.covered()==v.catalog.total_blocks
 cat=v.read_fork(v.catalog);size=H.u16(cat,32);node=H.u32(cat,24);seen=set();folders={};files=[]
 while node:
  assert node not in seen and node*size+size<=len(cat);seen.add(node);b=cat[node*size:(node+1)*size]
  for record in H.leaf_records(b,size):
   row=H.parse_record(record)
   if not row:continue
   parent,name,kind,off,raw=row
   if kind==1:folders[H.u32(raw,off+8)]=(parent,name)
   if kind==2 and name=='com.apple.SpringBoard.plist':files.append((parent,name,H.Fork(raw,off+88)))
  node=H.u32(b,0)
 assert len(files)==1
 parent,name,fork=files[0];parts=[name]
 while parent!=2:parent,name=folders[parent];parts.append(name)
 path='/'+'/'.join(reversed(parts));assert path=='/System/Library/LaunchDaemons/com.apple.SpringBoard.plist'
 assert fork.covered()==fork.total_blocks and fork.logical<=65536
 data=v.read_fork(fork);env=plistlib.loads(data)['EnvironmentVariables'];print(label,path,env)
 return {'label':label,'imageOffset':base,'filesystemSignature':vh[:2].decode(),'blockSize':v.block_size,'totalBlocks':v.total_blocks,'catalogSHA256':hashlib.sha256(cat).hexdigest(),'path':path,'forkBytes':len(data),'forkExtents':fork.extents,'plistSHA256':hashlib.sha256(data).hexdigest(),'environment':env}
reports=[]
with Path('.context/safari-default-mbx/ram.bin').open('rb') as f:reports.append(inspect(f,0x5d0000,'actual failed Safari snapshot RAM filesystem'))
with gzip.open('.context/guest-safari/installed/server.img.gz','rb') as f:reports.append(inspect(f,0,'actual retained091 installed filesystem image'))
# Verify the actual091 listener checkpoint imported by its browser, not only
# the work image from which it was booted. Decode privately in memory; no CPU.
with gzip.open('.context/guest-safari/native-run-1/listener.snap.gz','rb') as f:
 header=f.read(40);assert header.startswith(b'S5LBox SNAPSHOT\0')
 while True:
  tag,reserved,n=struct.unpack('<4sIQ',f.read(16));assert not reserved
  if tag==b'RAM ':break
  f.seek(n,1)
 pages=struct.unpack('<Q',f.read(8))[0];assert pages==65536;ram=bytearray(pages*4096)
 for page in range(pages):
  cls=f.read(1)[0]
  if cls==1:ram[page*4096:(page+1)*4096]=f.read(1)*4096
  elif cls==2:ram[page*4096:(page+1)*4096]=f.read(4096)
  else:assert cls==0
 reports.append(inspect(io.BytesIO(ram),0x5d0000,'actual091 imported listener checkpoint RAM filesystem'))
Path('.context/safari-default-mbx/filesystem-policy.json').write_text(json.dumps({'passed':True,'results':reports,'guestExecuted':False},indent=2)+'\n')
