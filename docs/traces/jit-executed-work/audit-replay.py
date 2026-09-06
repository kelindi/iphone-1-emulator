"""Independent artifact-only verification after the single guest has closed."""
import pathlib,json,gzip,hashlib,struct,collections,os
b=pathlib.Path('.context/jit-executed-work');r=b/'replay';report=json.loads((r/'report.json').read_text());control=json.loads(pathlib.Path('.context/jit-fallback-census/control-100m.json').read_text());controller=json.loads((r/'controller.json').read_text());assert report['passed'] and controller['passed'] and controller['allClosed'] and controller['exitCode']==0
positions=json.loads((b/'positions.json').read_text());positions=positions['positions'] if isinstance(positions,dict) else positions
slotmap={p['slot']:p for p in positions};assert len(slotmap)==len(positions)
checks=[]
for a,c in zip(report['checkpoints'],control['checkpoints']):
 assert {k:a[k]for k in ['done','bytes','rawSha256','generated','calls','regions']}=={k:c[k]for k in ['done','bytes','rawSha256','generated','calls','regions']}
 folder=r/str(a['done']);h=hashlib.sha256();size=0
 with gzip.open(folder/'raw.snap.gz','rb')as f:
  while chunk:=f.read(1048576):h.update(chunk);size+=len(chunk)
 assert size==c['bytes'] and h.hexdigest()==c['rawSha256']
 cap=json.loads((folder/'capture.json').read_text());raw={}
 for item in cap['data']:
  p=folder/(item['name']+'.bin.gz');compressed=p.read_bytes();assert hashlib.sha256(compressed).hexdigest()==item['gzipSha256'];data=gzip.decompress(compressed);assert len(data)==item['rawBytes'] and hashlib.sha256(data).hexdigest()==item['rawSha256'];raw[item['name']]=data
 s=struct.unpack('<9Q2I',raw['summary']);calls,ok,fetched,nofetch,nonok,overflow,mapoverflow,epoch,installs,rows,enabled=s
 assert not any([overflow,mapoverflow,installs,enabled,int(cap['generatedOverflow'])]);assert epoch==int(cap['epochStart'])
 sums=collections.Counter();work=set();seen=set()
 for pc,op,flags,prov,guard,admission,status,used,count in struct.iter_unpack('<8IQ',raw['rows']):
  if not used:assert not any((pc,op,flags,prov,guard,admission,status,count));continue
  key=(pc,op,flags,prov,guard,admission,status);assert key not in seen;seen.add(key)
  sums['calls']+=count;sums['ok'if status==0 else'nonok']+=count;sums['fetched'if flags&1 else'nofetch']+=count
  work.add((pc,op,bool(flags&256),flags&31,status))
 assert (sums['calls'],sums['ok'],sums['fetched'],sums['nofetch'],sums['nonok'],len(seen))==(calls,ok,fetched,nofetch,nonok,rows)
 generated=0;active=0
 for slot,(count,)in enumerate(struct.iter_unpack('<Q',raw['generated'])):
  if not count:continue
  assert slot in slotmap;p=slotmap[slot];generated+=count;active+=1;work.add((p['pc'],p['opcode'],p['thumb'],1,0))
 assert generated==c['generated'] and generated+ok==a['done']
 ref=pathlib.Path(json.loads((folder/'dedup.json').read_text())['retained']);assert os.path.samefile(folder/'raw.snap.gz',ref)
 checks.append({'done':a['done'],'rawSha256':h.hexdigest(),'rawBytes':size,'generated':generated,'interpreted':ok,'interpreterRows':rows,'generatedPositions':active,'mergedWorkKeys':len(work),'fetchedInterpreter':fetched,'unfetchedInterpreter':nofetch,'allPolicyCountersExact':True,'noLoss':True,'sameWitnessInode':True})
assert len(checks)==4
frame=gzip.decompress((r/'frame.rgba.gz').read_bytes());assert hashlib.sha256(frame).hexdigest()==control['frame']['sha256'];assert report['frame']==control['frame'];assert len(frame)==320*480*4
out={'passed':True,'scope':'Independent retained-byte audit, no guest run or speed measurement','checkpoints':checks,'frameExact':True,'controllerElapsedSeconds':controller['ended']-controller['started'],'scopedObservedProcesses':len(controller['processes']),'allClosed':controller['allClosed']};(b/'replay-audit.json').write_text(json.dumps(out,indent=2)+'\n');print(json.dumps(out,indent=2))
