"""Read-only insertion simulation, preserving every existing B-tree first key."""
import sys,json,struct,copy,itertools
from pathlib import Path
sys.path.insert(0,str(Path('tools/firmware').resolve()));sys.path.insert(0,str(Path('.context/guest-safari').resolve()))
import rootfs_work as W
B=Path(__file__).resolve().parent
w=W.Work('.context/graphics-speed/rootfs-mbx2d.img')
nodes=[(i,[r[2]for r in w._leaf_records(n)])for i,n in w._leaf_chain()]
used=w._catalog_object_ids();holes=[i for i in range(16,w.next_catalog_id)if i not in used]
root=w._resolve_dir('/private/var/root');assert root
folder=w._find_record(1451,'Preferences',1);folder_bytes=len(folder[4])-folder[3]
file=w._find_record(1452,'.GlobalPreferences.plist',2);file_bytes=len(file[4])-file[3]
assets=sorted(p.name for p in (B/'assets').iterdir())

def simulate(ids):
 ns=[(i,list(rs))for i,rs in nodes];events=[]
 def insert(row,label):
  key=w._key(row);selected=None
  for i,rs in ns:
   selected=(i,rs)
   if not rs or w._key(rs[-1])>=key:break
  i,rs=selected
  if not rs or key<=w._key(rs[0]):raise ValueError('First key')
  if any(w._key(r)==key for r in rs):raise ValueError('Duplicate')
  n=14+sum(map(len,rs))+len(row)+2*(len(rs)+2)
  if n>w.node_size:raise ValueError('Capacity '+label+' node '+str(i)+' bytes '+str(n))
  j=next((j for j,r in enumerate(rs)if w._key(r)>key),len(rs));rs.insert(j,row);events.append({'label':label,'node':i,'used':n})
 parent=root
 for name,ident in zip(['Media','DCIM','100APPLE'],ids):
  insert(w._make_key(parent,name)+bytes(folder_bytes),name+' directory')
  insert(w._make_key(ident,'')+struct.pack('>HHIH',3,0,parent,len(name))+name.encode('utf-16-be'),name+' thread');parent=ident
 for ident,name in enumerate(assets,max(w.next_catalog_id,max(ids)+1)):
  insert(w._make_key(parent,name)+bytes(file_bytes),name)
  insert(w._make_key(ident,'')+struct.pack('>HHIH',4,0,parent,len(name))+name.encode('utf-16-be'),name+' thread')
 return events
failures=[];answer=None
for candidate in itertools.permutations(holes+[w.next_catalog_id],3):
 ids=list(candidate)
 try:answer={'passed':True,'directoryIDs':ids,'nextFileID':max(w.next_catalog_id,max(ids)+1),'events':simulate(ids),'priorRejectedCandidates':failures};break
 except ValueError as e:failures.append({'directoryIDs':ids,'reason':str(e)})
w.vol.f.close()
if answer is None:answer={'passed':False,'failures':failures}
(B/'catalog-plan.json').write_text(json.dumps(answer,indent=2)+'\n');print(json.dumps(answer));raise SystemExit(0 if answer['passed']else 1)
