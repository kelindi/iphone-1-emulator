"""Independent artifact acceptance; no emulator or mutable filesystem helper."""
import hashlib,json,re,sys
from pathlib import Path
sys.path.insert(0,str(Path('tools/firmware').resolve()))
import hfsx_extract as H
B=Path('.context/guest-hello'); R=B/'run-1'
def sha(p):
 h=hashlib.sha256()
 with Path(p).open('rb') as f:
  for b in iter(lambda:f.read(1024*1024),b''):h.update(b)
 return h.hexdigest()
pins=json.loads((B/'source-pins.json').read_text())
for p,s in pins.items(): assert sha(p)==s,p
def inventory(path):
 v=H.Volume(path);cat=v.read_fork(v.catalog);size=H.u16(cat,32);n=H.u32(cat,24);seen=set();objects=[];dirs={2:'/'}
 while n:
  assert n not in seen;seen.add(n);node=cat[n*size:(n+1)*size];assert len(node)==size
  for raw in H.leaf_records(node,size):
   obj=H.parse_record(raw)
   if obj and obj[2] in (1,2):objects.append(obj)
  n=H.u32(node,0)
 for _ in range(len(objects)):
  old=len(dirs)
  for par,name,typ,off,raw in objects:
   if typ==1 and par in dirs:dirs[H.u32(raw,off+8)]=dirs[par].rstrip('/')+'/'+name
  if len(dirs)==old:break
 records={};forks={}
 for par,name,typ,off,raw in objects:
  assert par in dirs;path=dirs[par].rstrip('/')+'/'+name;assert path not in records;data=raw[off:];records[path]=data
  if typ==2:
   hashes=[]
   for offset in (88,168):
    f=H.Fork(data,offset);assert f.covered()==f.total_blocks;content=v.read_fork(f);assert len(content)==f.logical;hashes.append((len(content),hashlib.sha256(content).hexdigest()))
   forks[path]=hashes
 v.f.close();return records,forks
before,bf=inventory('.context/legacy-upgrade/shutdown/disk.img');after,af=inventory(B/'installed/hello.img')
added={'/usr/libexec/iphone1-own-program','/System/Library/LaunchDaemons/com.iphone1.qualification.hello.plist'}
assert set(after)-set(before)==added and not set(before)-set(after)
for p,r in before.items():
 a=after[p]
 if p in ('/usr/libexec','/System/Library/LaunchDaemons'):
  assert H.u32(a,4)==H.u32(r,4)+1 and a[:4]+a[8:]==r[:4]+r[8:]
 else:assert a==r,p
for p,f in bf.items():assert af[p]==f,p
for p,host,mode in [('/usr/libexec/iphone1-own-program','iphone1-own-program',0o100755),('/System/Library/LaunchDaemons/com.iphone1.qualification.hello.plist','org.iphone1.qualification.hello.plist',0o100644)]:
 assert H.u16(after[p],42)==mode and H.u32(after[p],32)==H.u32(after[p],36)==0
 assert af[p][0]==((B/host).stat().st_size,sha(B/host)) and af[p][1][0]==0
out=(R/'stdout.log').read_text(); report=json.loads((R/'report.json').read_text())
patterns=[r'HELLO entry cycle=(\d+) ttbr=14ca4000 pc=00001f04 cpsr=40000010 exact_text_bytes=212',r'HELLO getpid_return cycle=(\d+) pid=21',r'HELLO original_write cycle=(\d+) fd=1 length=38 exact=1',r'HELLO write_return cycle=(\d+) bytes=38',r'HELLO original_exit_entry cycle=(\d+) status=0']
witnesses=[];lastpos=-1;lastcycle=-1
for pat in patterns:
 matches=list(re.finditer(pat,out));assert len(matches)==1;match=matches[0];cycle=int(match[1]);assert match.start()>lastpos and cycle>lastcycle;lastpos=match.start();lastcycle=cycle;witnesses.append({'text':match[0],'cycle':cycle})
marker=b'IPHONE1_OWN_PROGRAM_V1 pid=0000000021\n';serial=(R/'serial.log').read_bytes().replace(b'\r',b'');assert serial.count(marker)==1 and out.count(marker.decode())==1
assert 'HELLO final seen=1 serial=1 write=1 exit_entry=1 failed=0 entries=1 ttbr=14ca4000 pid=21' in out
assert 'reason=hello-qualified instructions=262000000' in out
assert report['passed'] and report['returncode']==0 and report['elapsedSeconds']<900
assert (R/'stderr.log').stat().st_size==0
result={'passed':True,'sourcePinsChecked':len(pins),'sourcePinsSHA256':sha(B/'source-pins.json'),'existingFiles':len(bf),'unchangedForks':2*len(bf),'existingCatalogRecords':len(before),'newFiles':sorted(added),'witnesses':witnesses,'uartMarker':marker.decode(),'normalRunReturnInstructions':262000000,'limitations':['Exit entry with status0, not process reap.','Serial line prefix is a buffered chunk label, not the marker instruction.','No loopback, HTTP, firmware-interface or production integration claim.','Existing unrelated OS warnings retained; no globally error-free OS claim.'],'artifacts':{p.name:sha(p) for p in R.iterdir() if p.is_file()}}
(R/'independent-audit.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
