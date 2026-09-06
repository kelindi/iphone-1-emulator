"""Focused redistributable HFSX tests; never open the actual guest image."""
import contextlib,hashlib,io,struct,sys,tempfile,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
import hfsx_extract as H
import test_hfsx_compact as F
BASE=Path(__file__).resolve().parent

import rootfs_work as N
# Frozen original helper (SHA256 2c7b5445b254ec46cbe5476c1358255ef011852f6d4c4aecebff40c7080a08d1)
# generated this complete synthetic output before permission support. Retained at
# docs/traces/guest-hello/rootfs_work_original.py; no actual firmware is involved.
DEFAULT_IMAGE_SHA256 = "93359f6244f95d0a91fd9a1089ca3e82ae42934631b4f7a4988be082fe4f85cb"

def record(parent,name,kind,ident):
 data=bytearray(88 if kind==1 else 248)
 struct.pack_into('>H',data,0,kind);struct.pack_into('>I',data,8,ident)
 struct.pack_into('>II',data,32,501,20);struct.pack_into('>H',data,42,0o40755 if kind==1 else 0o100644)
 return N.Work._make_key(parent,name)+data

def fixture():
 image=F.fixture();cat=bytearray(image[5*F.BS:7*F.BS]);cat[F.BS:]=bytes(F.BS);cat[F.BS+8]=255;cat[51]=0xbc
 rows=[record(1,'FixtureVolume',1,2),record(1451,'Preferences',1,1452),record(1452,'.GlobalPreferences.plist',2,1453)]
 template=bytearray(rows[-1]);offset=len(template)-248;F.fork(template,offset+88,4,[(20,1)]);rows[-1]=bytes(template);image[20*F.BS:20*F.BS+4]=b'KEEP'
 struct.pack_into('>H',cat,F.BS+10,len(rows));struct.pack_into('>I',cat,20,len(rows));pos=14
 for i,row in enumerate(rows):
  struct.pack_into('>H',cat,2*F.BS-2*(i+1),pos);cat[F.BS+pos:F.BS+pos+len(row)]=row;pos+=len(row)
 struct.pack_into('>H',cat,2*F.BS-2*(len(rows)+1),pos);image[5*F.BS:7*F.BS]=cat
 for start in (1024,len(image)-1024):struct.pack_into('>I',image,start+64,1500)
 return image

def objects(path):
 w=N.Work(path);result={}
 for _,node in w._leaf_chain():
  for _,_,raw in w._leaf_records(node):
   p=H.parse_record(raw)
   if p and p[2] in (1,2):result[(p[0],p[1])]=raw[p[3]:]
 w.vol.f.close();return result

class Tests(unittest.TestCase):
 def setUp(self):self.tmp=tempfile.TemporaryDirectory();self.root=Path(self.tmp.name)
 def tearDown(self):self.tmp.cleanup()
 def run_add(self,name,mod,**kw):
  p=self.root/name;p.write_bytes(fixture());w=mod.Work(p)
  with contextlib.redirect_stdout(io.StringIO()):w.add_file_data('/own-program',b'OWN '+bytes(range(251))*19,**kw)
  w.vol.f.close();return p
 def test_default_exact_original_tool(self):
  b=self.run_add('new',N);self.assertEqual(hashlib.sha256(b.read_bytes()).hexdigest(),DEFAULT_IMAGE_SHA256)
 def test_only_explicit_catalog_permission_fields_differ(self):
  a=self.run_add('default',N);b=self.run_add('explicit',N,mode=0o755,uid=0,gid=0)
  x=objects(a);y=objects(b);self.assertEqual(set(x),set(y));key=(2,'own-program');allowed={32,33,34,35,36,37,38,39,42,43}
  for k in x:
   if k!=key:self.assertEqual(x[k],y[k])
  self.assertEqual(H.u32(y[key],32),0);self.assertEqual(H.u32(y[key],36),0);self.assertEqual(H.u16(y[key],42),0o100755)
  self.assertTrue(all(i in allowed for i,(m,n) in enumerate(zip(x[key],y[key])) if m!=n))
  self.assertEqual(sum(m!=n for m,n in zip(a.read_bytes(),b.read_bytes())),sum(m!=n for m,n in zip(x[key],y[key])))
  volume=H.Volume(b);self.assertEqual(volume.read_fork(H.Fork(y[key],88)),b'OWN '+bytes(range(251))*19);self.assertEqual(volume.read_fork(H.Fork(y[(1452,'.GlobalPreferences.plist')],88)),b'KEEP');volume.f.close()
 def test_bounds_before_any_directory_or_allocation_changes(self):
  for key,values in [('mode',[-1,0o1000,0o4755,0o100755,True,1.5,'755']),('uid',[-1,1<<32,True,0.5]),('gid',[-1,1<<32,False,'0'])]:
   for value in values:
    with self.subTest(key=key,value=value):
     p=self.root/'negative';p.write_bytes(fixture());before=p.read_bytes();w=N.Work(p)
     with self.assertRaises(ValueError):w.add_file_data('/new-directory/own-program',b'x',**{key:value})
     self.assertEqual(p.read_bytes(),before);w.vol.f.close()
 def test_admitted_extremes_and_independent_ownership(self):
  for mode in (0,0o644,0o755,0o777):
   p=self.run_add(str(mode),N,mode=mode,uid=0xffffffff,gid=7);d=objects(p)[(2,'own-program')]
   self.assertEqual(H.u16(d,42),0o100000|mode);self.assertEqual(H.u32(d,32),0xffffffff);self.assertEqual(H.u32(d,36),7)
 def test_existing_file_still_rejected_without_mutation(self):
  p=self.run_add('one',N,mode=0o755);before=p.read_bytes();w=N.Work(p)
  with self.assertRaises(SystemExit):w.add_file_data('/own-program',b'REPLACE',mode=0o644,uid=0)
  self.assertEqual(p.read_bytes(),before);w.vol.f.close()

if __name__=='__main__':unittest.main(verbosity=2)
