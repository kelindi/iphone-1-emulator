"""Prepare a private clean-image copy with two NEW independently authored files.
Successor installer is prepared, not yet executed. Never writes source or existing destination.
"""
import hashlib,json,os,struct,sys,tempfile
from pathlib import Path
sys.path.insert(0,str(Path('tools/firmware').resolve()))
sys.path.insert(0,str(Path(__file__).resolve().parent))
import rootfs_work as W
import hfsx_extract as H
B=Path(__file__).resolve().parent
SOURCE=Path('.context/legacy-upgrade/shutdown/disk.img')
SOURCE_SHA='87271faa21af0830e2c8d174759f525d25e62adc31bceccaae9acb5230a82e12'
PROGRAM_SHA='41d6a5247e4e540ae183151fbc7a242333518af0aad5a11b7f1fa9a0e970144d'
ADDS=[('/usr/libexec/iphone1-own-loopback',B/'iphone1-own-loopback',0o755),('/System/Library/LaunchDaemons/com.iphone1.qualification.loopback.plist',B/'org.iphone1.qualification.loopback.plist',0o644)]
sha=lambda data:hashlib.sha256(data).hexdigest()

def inventory(path):
 w=W.Work(path);records={};files={};dirs={2:'/'};objects=[]
 for _,node in w._leaf_chain():
  for _,_,raw in w._leaf_records(node):
   obj=H.parse_record(raw)
   if obj and obj[2] in (1,2):objects.append(obj)
 for _ in range(20):
  for par,name,typ,off,raw in objects:
   if typ==1 and par in dirs:dirs[H.u32(raw,off+8)]=dirs[par].rstrip('/')+'/'+name
 for par,name,typ,off,raw in objects:
  path=dirs.get(par,str(par)).rstrip('/')+'/'+name;data=raw[off:];records[path]=data
  if typ==2:
   forks=[]
   for foff in (88,168):
    f=H.Fork(data,foff);assert f.covered()==f.total_blocks;forks.append({'bytes':f.logical,'sha256':sha(w.vol.read_fork(f))})
   files[path]=forks
 w.vol.f.close();return records,files

def preflight():
 raw=SOURCE.read_bytes();assert sha(raw)==SOURCE_SHA and H.u32(raw,1028)==0x100
 primary,alternate=raw[1024:1536],raw[-1024:-512]
 # Original clean shutdown leaves a stale alternate header. Preserve it;
 # bind the known pair, never copy primary over alternate or fake clean bits.
 assert [o for o in range(0,512,4) if primary[o:o+4]!=alternate[o:o+4]]==[4,20,32,36,48,52,64,68]
 assert H.u32(alternate,4)==0 and primary[40:48]==alternate[40:48] and primary[112:]==alternate[112:]
 assert sha((B/'iphone1-own-loopback').read_bytes())==PROGRAM_SHA
 w=W.Work(SOURCE);nodes=[(n,[r[2] for r in w._leaf_records(node)])for n,node in w._leaf_chain()];proof=[];cid=w.next_catalog_id
 for path,file,mode in ADDS:
  parent=w._resolve_dir(str(Path(path).parent));assert parent and not w.find_file_path(path)
  name=Path(path).name
  # Same new-file/thread lengths and keys as Work._new_file. Planning only.
  entries=[W.Work._make_key(parent,name)+bytes(248),W.Work._make_key(cid,'')+struct.pack('>HHIH',4,0,parent,len(name))+name.encode('utf-16-be')]
  for row in entries:
   key=w._key(row);chosen=None
   for n,rows in nodes:
    chosen=(n,rows)
    if not rows or w._key(rows[-1])>=key:break
   assert chosen;n,rows=chosen
   # No changed leftmost key means existing index separators remain valid.
   assert rows and key>w._key(rows[0])
   pos=next((i for i,r in enumerate(rows) if w._key(r)>key),len(rows));assert all(w._key(r)!=key for r in rows)
   rows.insert(pos,row);used=14+sum(map(len,rows))+2*(len(rows)+1);assert used<=w.node_size
   proof.append({'path':path,'node':n,'usedAfter':used,'capacity':w.node_size,'existingFirstKeyUnchanged':True})
  cid+=1
 w.vol.f.close();assert sha(SOURCE.read_bytes())==SOURCE_SHA;return proof

def install(target):
 assert not target.exists();proof=preflight();before,before_forks=inventory(SOURCE)
 template=before['/private/var/root/Library/Preferences/.GlobalPreferences.plist'];assert H.Fork(template,168).logical==0
 for parent in ('/usr/libexec','/System/Library/LaunchDaemons'):
  r=before[parent];assert H.u16(r,42)==0o40755 and H.u32(r,32)==H.u32(r,36)==0
 target.parent.mkdir(parents=True,exist_ok=True);fd,name=tempfile.mkstemp(prefix='guest-hello-',suffix='.partial',dir=target.parent);temp=Path(name)
 try:
  with os.fdopen(fd,'wb')as f:f.write(SOURCE.read_bytes())
  w=W.Work(temp)
  for path,host,mode in ADDS:w.add_file(path,host,mode=mode,uid=0,gid=0)
  w.vol.f.close();after,after_forks=inventory(temp)
  assert set(after)-set(before)=={a[0] for a in ADDS};assert not set(before)-set(after)
  for path,data in before.items():
   new=after[path]
   if path in ('/usr/libexec','/System/Library/LaunchDaemons'):
    assert H.u32(new,4)==H.u32(data,4)+1 and new[:4]+new[8:]==data[:4]+data[8:]
   else:assert new==data,path
  for path,forks in before_forks.items():assert after_forks[path]==forks,path
  for path,host,mode in ADDS:
   r=after[path];assert H.u16(r,42)==0o100000|mode and H.u32(r,32)==H.u32(r,36)==0
   assert after_forks[path][0]=={'bytes':host.stat().st_size,'sha256':sha(host.read_bytes())}
  assert sha(SOURCE.read_bytes())==SOURCE_SHA
  with temp.open('rb')as f:os.fsync(f.fileno())
  report={'passed':True,'sourceSHA256':SOURCE_SHA,'sourceUnchanged':True,'outputSHA256':sha(temp.read_bytes()),'unchangedExistingFileForks':2*len(before_forks),'newFiles':[a[0]for a in ADDS],'catalogPreflight':proof}
  os.link(temp,target) # Atomic no-clobber publication; source is never linked.
  target.with_suffix(target.suffix+'.json').write_text(json.dumps(report,indent=2)+'\n')
 finally:temp.unlink(missing_ok=True)

if __name__=='__main__':
 if sys.argv[1:]==['--preflight']:print(json.dumps(preflight(),indent=2))
 elif len(sys.argv)==2:install(Path(sys.argv[1]))
 else:raise SystemExit('prepare_image.py --preflight | NEW_OUTPUT_PATH (installation needs explicit authorization)')
