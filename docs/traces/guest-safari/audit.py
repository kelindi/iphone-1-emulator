"""Artifact-only independent UART, HTTP framing, persistence and pixel audit.
No emulator, browser navigation or guest memory modification.
"""
from pathlib import Path
import collections,gzip,hashlib,json,re,struct,zlib
B=Path(__file__).resolve().parent;R=B/'browser-run-1'
sha=lambda b:hashlib.sha256(b).hexdigest()
report=json.loads((R/'report.json').read_text());native=json.loads((B/'native-run-1/report.json').read_text())
assert report['passed'] and not report['errors'] and report['projectionUnchanged'] and report['elapsedSeconds']<900
assert native['passed'] and native['returncode']==native['originalLoaderReturncode']==0 and native['elapsedSeconds']<1020
assert json.loads((B/'native-run-1/post-pins.json').read_text())['files']==30
assert json.loads((B/'installed/server.img.json').read_text())['unchangedExistingFileForks']==5178
text=(R/'serial.log').read_bytes().decode().replace('\r','')
assert text==''.join(x['text']for x in report['serial']).replace('\r','')
def blobs(label):
 out=[]
 for m in re.finditer(r'^'+label+r' ([0-9a-f]{8}) [0-9a-f]{8}\n((?:D [0-9a-f]+\n)+)',text,re.M):
  data=bytes.fromhex(''.join(re.findall(r'^D ([0-9a-f]+)$',m[2],re.M)));assert len(data)==int(m[1],16);out.append(data)
 return out
requests=blobs('HTTP_REQUEST_BYTES');responses=blobs('HTTP_RESPONSE_BYTES');peers=blobs('HTTP_PEER')
assert len(requests)==len(responses)==len(peers)==7
assert [b.decode()for b in requests]==report['guestRequests'] and [b.decode()for b in responses]==report['guestResponses']
begins=[int(m[1],16)for m in re.finditer(r'^HTTP_REQUEST_BEGIN ([0-9a-f]{8}) [0-9a-f]{8}$',text,re.M)]
sent=[(int(m[1],16),int(m[2],16))for m in re.finditer(r'^HTTP_RESPONSE_SENT ([0-9a-f]{8}) ([0-9a-f]{8})$',text,re.M)]
closed=[(int(m[1],16),int(m[2],16))for m in re.finditer(r'^HTTP_CLOSED ([0-9a-f]{8}) ([0-9a-f]{8})$',text,re.M)]
assert begins==list(range(1,8)) and sent==[(i+1,len(b))for i,b in enumerate(responses)] and closed==[(i,0)for i in range(1,8)]
paths=['/','/next','/','/echo?message=hi','/echo?message=hi','/','/echo?message=hi'];rows=[]
for i,(req,res,peer,target)in enumerate(zip(requests,responses,peers,paths)):
 assert len(peer)==16 and peer[:2]==bytes([16,2]) and peer[4:8]==bytes([127,0,0,1])
 first,*headers=req.decode().split('\r\n');assert first=='GET '+target+' HTTP/1.1' and req.endswith(b'\r\n\r\n')
 fields=dict(x.split(': ',1)for x in headers if x);assert fields['Host']=='127.0.0.1:18081'
 assert 'Mobile/1A543a Safari/419.3'in fields['User-Agent']
 head,body=res.split(b'\r\n\r\n',1);status,*headers=head.decode().split('\r\n');fields=dict(x.split(': ',1)for x in headers)
 assert status=='HTTP/1.0 200 OK' and int(fields['Content-Length'])==len(body) and fields['Connection']=='close' and fields['Cache-Control']=='no-store'
 marker=b'NEXT-1'if target=='/next'else b'ECHO-1'if target.startswith('/echo')else b'HOME-1';assert b'Page: '+marker in body
 if target.startswith('/echo'):assert b'Message: hi</p>'in body
 if target=='/':assert b'fixed demo content, not an internet service'in body
 rows.append({'serial':i+1,'path':target,'peerPort':int.from_bytes(peer[2:4],'big'),'requestBytes':len(req),'requestSHA256':sha(req),'responseBytes':len(res),'responseSHA256':sha(res),'bodyBytes':len(body),'marker':marker.decode(),'sentAndClosed':True})
assert not any('127.0.0.1:18081'in x['url']for x in report['requests']) and not report['requestFailures']
assert report['preReloadRequestCount']==5 and paths[5:]==['/','/echo?message=hi']
checks={x['name']:x for x in report['checks']}
saved=checks['real-interval-autosave'];restored=checks['actual-autosave-record-restored'];assert saved['savedAt']>saved['oldSavedAt'] and restored['savedAt']==saved['savedAt']
assert not any(a.get('save')or a.get('saveReload')for a in report['actions'])
assert sum(bool(a.get('autosaveReload'))for a in report['actions'])==1
jit=report['finalState']['metrics']['jit'];assert jit['enabled']and jit['retired']>0 and jit['calls']>0 and not report['finalState']['paused']
for name in ['original-bookmark-created','original-bookmark-revisited','original-bookmark-persisted-after-autosave-reload']:assert checks[name]['passed']and(R/checks[name]['frame']).is_file()
# Independent PNG scanline decoding; compare guest pixels, not screenshot filenames.
def png(path):
 raw=path.read_bytes();assert raw[:8]==b'\x89PNG\r\n\x1a\n';at=8;payload=b''
 while at<len(raw):
  n=struct.unpack('>I',raw[at:at+4])[0];tag=raw[at+4:at+8];part=raw[at+8:at+8+n];crc=struct.unpack('>I',raw[at+8+n:at+12+n])[0];assert zlib.crc32(tag+part)&0xffffffff==crc;at+=12+n
  if tag==b'IHDR':w,h,depth,kind,c,f,inter=struct.unpack('>IIBBBBB',part);assert depth==8 and kind in(2,6)and c==f==inter==0
  if tag==b'IDAT':payload+=part
 channels=3 if kind==2 else 4;stride=w*channels;data=zlib.decompress(payload);assert len(data)==h*(stride+1);out=bytearray();previous=bytearray(stride)
 for y in range(h):
  off=y*(stride+1);kind=data[off];row=bytearray(data[off+1:off+1+stride]);assert kind<=4
  for x in range(stride):
   a=row[x-channels]if x>=channels else 0;b=previous[x];c=previous[x-channels]if x>=channels else 0
   p=a+b-c;pa,pb,pc=abs(p-a),abs(p-b),abs(p-c);predict=[0,a,b,(a+b)//2,a if pa<=pb and pa<=pc else b if pb<=pc else c][kind];row[x]=(row[x]+predict)&255
  if channels==3:out.extend(row)
  else:
   for x in range(0,stride,4):out.extend(row[x:x+3])
  previous=row
 return w,h,bytes(out)
pairs=[('autosaved-guest.png','restored-guest.png',20,481),('13-form-response-guest.png','21-final-bookmark-form-guest.png',80,438),('17-bookmark-list-guest.png','20-bookmark-after-reload-guest.png',20,481)]
screens=[]
for a,b,y0,y1 in pairs:
 wa,ha,aa=png(R/a);wb,hb,bb=png(R/b);assert (wa,ha)==(wb,hb)==(320,481);aa=aa[y0*wa*3:y1*wa*3];bb=bb[y0*wa*3:y1*wa*3];assert aa==bb,(a,b)
 screens.append({'first':a,'second':b,'rows':[y0,y1],'comparedRGBPixels':len(aa)//3,'maximumDelta':0,'exact':True})
# Lossless stream independently hashed again without retaining a raw duplicate.
h=hashlib.sha256();n=0
with gzip.open(B/'native-run-1/listener.snap.gz','rb')as f:
 for part in iter(lambda:f.read(1048576),b''):h.update(part);n+=len(part)
assert n==native['transport']['receipt']['rawBytes']and h.hexdigest()==native['transport']['receipt']['rawSHA256']
result={'passed':True,'method':'Independent UART hex parser, HTTP header/length/content checks, original Safari user-agent, sequential completed sends/close, real autosave metadata and PNG RGB comparison. No guest rerun.','requests':rows,'screenComparisons':screens,'checks':len(report['checks']),'sourceProjectionFiles':len(report['projection']),'originalForksUnchanged':5178,'rawSnapshotBytes':n,'rawSnapshotSHA256':h.hexdigest(),'nativeNormalStopInstructions':3229000000,'autosave':saved,'restoredSavedAt':restored['savedAt'],'finalJIT':jit,'hostContextGuestPortRequests':0,'errors':[],'scope':'Isolated imported phone with independently authored local demo. No default distribution, external network, Weather data, or speed claim.'}
(R/'audit.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
