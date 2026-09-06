"""Read only the frozen clean disk; emit path/mode/plist/import inventory."""
import hashlib,json,plistlib,struct,sys
from pathlib import Path
sys.path.insert(0,str(Path('tools/firmware').resolve()))
import hfsx_extract as H
B=Path(__file__).resolve().parent
P=Path('.context/legacy-upgrade/shutdown/disk.img')
expected='87271faa21af0830e2c8d174759f525d25e62adc31bceccaae9acb5230a82e12'
assert hashlib.sha256(P.read_bytes()).hexdigest()==expected
v=H.Volume(P);v.f.seek(1024);header=v.f.read(512);assert H.u32(header,4)==0x100
cat=v.read_fork(v.catalog);size=H.u16(cat,32);node=H.u32(cat,24);objects=[];seen=set();dirs={2:'/'}
while node:
 assert node not in seen;seen.add(node);data=cat[node*size:(node+1)*size]
 for raw in H.leaf_records(data,size):
  obj=H.parse_record(raw)
  if obj and obj[2] in (1,2):objects.append(obj)
 node=H.u32(data,0)
for _ in range(20):
 for par,name,typ,off,raw in objects:
  if typ==1 and par in dirs:dirs[H.u32(raw,off+8)]=dirs[par].rstrip('/')+'/'+name
rows=[]
for par,name,typ,off,raw in objects:
 data=raw[off:];path=dirs.get(par,str(par)).rstrip('/')+'/'+name
 row={'path':path,'kind':typ,'uid':H.u32(data,32),'gid':H.u32(data,36),'mode':oct(H.u16(data,42))}
 if typ==2:
  fork=H.Fork(data,88);resource=H.Fork(data,168);row.update(bytes=fork.logical,resourceBytes=resource.logical)
  if path in ('/private/etc/hosts','/usr/lib/libSystem.B.dylib','/usr/lib/dyld','/sbin/launchd','/bin/launchctl') or '/LaunchDaemons/' in path:
   assert fork.covered()==fork.total_blocks;content=v.read_fork(fork);row['sha256']=hashlib.sha256(content).hexdigest()
   if path.endswith('.plist'):row['plist']=plistlib.loads(content)
 rows.append(row)
names={x['path'].split('/')[-1] for x in rows if x['kind']==2}
missing=[n for n in ['sh','bash','python','perl','ifconfig','ipconfig','scutil','route','ping','netstat','nc','inetd'] if n not in names]
v.f.close();assert hashlib.sha256(P.read_bytes()).hexdigest()==expected
report={'source':str(P),'sourceSHA256':expected,'sourceUnchanged':True,'volumeAttributes':'0x100','cleanUnmounted':True,'fileCount':sum(x['kind']==2 for x in rows),'directories':[{k:v for k,v in x.items()}for x in rows if x['path'] in ('/usr/libexec','/System/Library/LaunchDaemons')],'missingNamedUtilities':missing,'template':[x for x in rows if x['path'].endswith('/Library/Preferences/.GlobalPreferences.plist')],'entries':rows}
(B/'inventory-complete.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:v for k,v in report.items() if k!='entries'},indent=2))
