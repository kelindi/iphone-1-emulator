"""Artifact-only independent snapshot/RAM/files audit. Does not run instructions."""
from pathlib import Path
import hashlib,json,re,struct,sys,subprocess
sys.path.insert(0,'tools/firmware');import hfsx_extract as H
B=Path('.context/photo-bootstrap');R=B/'native-continuation-1'
sha=lambda b:hashlib.sha256(b).hexdigest()
report={'passed':False,'homeAccepted':False,'reason':'Separate4B→10B continuation. Manual Home acceptance recorded separately after actual frame review; no Photos browser run.'}
try:
 run=json.loads((R/'run.json').read_text());assert run['passed'] and run['processClosed']
 with (R/'home.snap').open('rb')as f:
  result=subprocess.run(['.context/guest-safari/test_snapshot_transport','verify'],stdin=f,capture_output=True,text=True,timeout=30)
 report['originalLoader']={'exitCode':result.returncode,'stdout':result.stdout,'stderr':result.stderr};assert result.returncode==0
 # Independently expand every RAM page and compare the original CLI's final dump.
 with (R/'home.snap').open('rb')as f,(R/'ram.bin').open('rb')as ram:
  magic,v,hlen,payload,flags=struct.unpack('<16sIIQQ',f.read(40));assert magic==b'S5LBox SNAPSHOT'.ljust(16,b'\0') and v==39 and hlen==40 and flags==0
  saw=False
  while f.tell()<40+payload:
   tag,zero,length=struct.unpack('<4sIQ',f.read(16));assert zero==0;end=f.tell()+length
   if tag==b'RAM ':
    assert not saw;saw=True;pages=struct.unpack('<Q',f.read(8))[0];assert pages==65536
    h=hashlib.sha256()
    for i in range(pages):
     kind=f.read(1);assert kind in [b'\0',b'\1',b'\2']
     data=bytes(4096) if kind==b'\0' else f.read(1)*4096 if kind==b'\1' else f.read(4096)
     assert len(data)==4096 and data==ram.read(4096),i;h.update(data)
    assert f.tell()==end and not ram.read(1);report['fullRAMSHA256']=h.hexdigest()
   f.seek(end)
  assert saw and f.tell()==40+payload and len(f.read())==8
 layout=(R/'stdout.log').read_text();phys=int(re.search(r'phys_base=0x([0-9a-f]+)',layout)[1],16);base=int(re.search(r'ramdisk_pa=0x([0-9a-f]+)',layout)[1],16);size=int(re.search(r'ramdisk_size=0x([0-9a-f]+)',layout)[1],16)
 offset=base-phys;assert 0<=offset and size==176160768 and offset+size<=268435456
 with (R/'ram.bin').open('rb')as f:f.seek(offset);disk=f.read(size)
 assert len(disk)==size
 # A bounded view avoids a second168MiB extraction on disk.
 class View:
  def __init__(self):self.pos=0
  def seek(self,n):assert 0<=n<=len(disk);self.pos=n
  def read(self,n):assert 0<=n and self.pos+n<=len(disk);b=disk[self.pos:self.pos+n];self.pos+=n;return b
 vh=disk[1024:1536];assert vh[:4]==b'HX\0\5'
 volume=H.Volume.__new__(H.Volume);volume.f=View();volume.block_size=H.u32(vh,40);volume.total_blocks=H.u32(vh,44);volume.catalog=H.Fork(vh,272)
 assert volume.catalog.covered()==volume.catalog.total_blocks
 cat=volume.read_fork(volume.catalog);node_size=H.u16(cat,32);leaf=H.u32(cat,24);seen=set();objects=[]
 while leaf:
  assert leaf not in seen;seen.add(leaf);node=cat[leaf*node_size:(leaf+1)*node_size];assert len(node)==node_size and node[8]==255
  rows=list(H.leaf_records(node,node_size));assert len(rows)==H.u16(node,10)
  objects += [H.parse_record(row)for row in rows];leaf=H.u32(node,0)
 dirs={2:'/'}
 for _ in range(len(objects)):
  before=len(dirs)
  for parent,name,kind,off,row in objects:
   if kind==1 and parent in dirs and H.u32(row,off+8)!=2:dirs[H.u32(row,off+8)]=dirs[parent].rstrip('/')+'/'+name
  if len(dirs)==before:break
 files={}
 for parent,name,kind,off,row in objects:
  if kind==2:
   assert parent in dirs;files[dirs[parent].rstrip('/')+'/'+name]=(off,row)
 expected=json.loads(Path('tools/legacy_services/photo_bootstrap/assets.json').read_text())['files'];verified=[]
 from resolve_hardlink import resolve
 for a in expected:
  path='/private/var/root/Media/DCIM/100APPLE/'+a['name']
  verified.append(resolve(volume,files,path,a))
 controls=[];a=expected[0];path='/private/var/root/Media/DCIM/100APPLE/'+a['name']
 for label in ['bad-type','bad-creator','zero-reference','missing-reference','wrong-target-mode','wrong-link-count','wrong-payload-hash']:
  altered=dict(files);wanted=dict(a);key=path
  if label in ['wrong-target-mode','wrong-link-count']:key=verified[0]['target']
  off,row=altered[key];row=bytearray(row)
  if label=='bad-type':row[off+48]^=1
  elif label=='bad-creator':row[off+52]^=1
  elif label=='zero-reference':struct.pack_into('>I',row,off+44,0)
  elif label=='missing-reference':struct.pack_into('>I',row,off+44,0xffffff)
  elif label=='wrong-target-mode':row[off+43]^=1
  elif label=='wrong-link-count':struct.pack_into('>I',row,off+44,0)
  elif label=='wrong-payload-hash':wanted['sha256']='0'*64
  altered[key]=(off,bytes(row))
  try:resolve(volume,altered,path,wanted)
  except (AssertionError,KeyError):controls.append({'case':label,'rejected':True})
  else:raise AssertionError('actual negative accepted: '+label)
 report['actualHardlinkNegatives']=controls

 report.update(fileAuditPassed=True,files=verified,diskOffset=offset,diskBytes=size,diskSHA256=sha(disk),primaryFlags=hex(H.u32(vh,4)),catalogLeaves=len(seen),snapshotSHA256=sha((R/'home.snap').read_bytes()),scope='Snapshot original-loader PASS and all256MiB RAM exact; five actual backing-file payloads independently exact. No Photos app acceptance; manual Home result separate.')
except BaseException as e:
 report['error']={'type':type(e).__name__,'message':str(e)};raise
finally:(R/'artifact-audit-corrected.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
