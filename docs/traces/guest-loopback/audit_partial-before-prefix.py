"""Read-only audit of preserved UART-only evidence; deliberately not acceptance."""
from pathlib import Path
import hashlib,json,re
B=Path(__file__).resolve().parent;R=B/'run-1'
pins=json.loads((B/'source-pins.json').read_text())
for p,h in pins.items():assert hashlib.sha256(Path(p).read_bytes()).hexdigest()==h,p
s=(R/'stdout.log').read_bytes().replace(b'\r',b'').decode();serial=(R/'serial.log').read_bytes().replace(b'\r',b'').decode()
assert s.count('LOOPBACK_BEGIN_V1\n')==s.count('LOOPBACK_PASS_V1\n')==1 and 'OWN entry' not in s
start=s.index('LOOPBACK_BEGIN_V1\n');stop=s.index('LOOPBACK_PASS_V1\n')+len('LOOPBACK_PASS_V1\n');segment=s[start:stop];assert segment in serial
lines=segment.splitlines();blobs={};rows=[];i=0
while i<len(lines):
 m=re.fullmatch(r'(\w+) ([0-9a-f]{8}) ([0-9a-f]{8})',lines[i])
 if m:
  name=m[1];value=int(m[2],16);detail=int(m[3],16);rows.append({'label':name,'value':value,'detail':detail})
  if i+1<len(lines) and lines[i+1].startswith('D '):
   parts=[];i+=1
   while i<len(lines) and lines[i].startswith('D '):parts.append(bytes.fromhex(lines[i][2:]));i+=1
   blob=b''.join(parts);assert len(blob)==value;blobs.setdefault(name,[]).append(blob);continue
 i+=1
req=b'GET /qualification HTTP/1.0\r\nHost: 127.0.0.1:18081\r\n\r\n';resp=b'HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 29\r\nConnection: close\r\n\r\nIPHONE1_GUEST_LOOPBACK_V1_OK\n'
assert blobs['actual_request']==[req] and blobs['actual_response']==[resp]
lookup=lambda name:[r for r in rows if r['label']==name]
for name in ['bind','listen','connect_status','close_response','close_client','close_server']:assert len(lookup(name))==1 and lookup(name)[0]['value']==0 and lookup(name)[0]['detail']==0
assert lookup('connect')==[{'label':'connect','value':0xffffffff,'detail':36}]
assert lookup('readiness_attempt')==[{'label':'readiness_attempt','value':0,'detail':0}]
conf=blobs['ifconf_bytes'][0];assert len(conf)==68
addr=[];offset=0
while offset<len(conf):
 assert len(conf)-offset>=32;n=max(32,16+conf[offset+16]);row=conf[offset:offset+n];assert len(row)==n
 addr.append({'name':row[:16].split(b'\0')[0].decode(),'sockaddrLength':row[16],'family':row[17],'addressBytes':row[16:].hex()});offset+=n
assert addr[1]['family']==2 and bytes.fromhex(addr[1]['addressBytes'])[4:8]==bytes([127,0,0,1])
report={'artifactChecksPassed':True,'qualificationPassed':False,'boundary':'Guest-authored UART says exact loopback exchange succeeded; required native process/text/API/exit witnesses absent. Original report remains failure, no retry.','sourcePinsChecked':len(pins),'readinessSamples':1,'interfaceRecords':addr,'interfaceFlags':[hex(r['detail'])for r in lookup('ifflags')],'routeBytes':len(blobs['route_bytes'][0]),'routeHex':blobs['route_bytes'][0].hex(),'requestBytes':len(req),'requestSHA256':hashlib.sha256(req).hexdigest(),'responseBytes':len(resp),'responseSHA256':hashlib.sha256(resp).hexdigest(),'rows':rows,'blobs':{k:[v.hex()for v in vs]for k,vs in blobs.items()},'artifactSHA256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest()for p in R.iterdir()if p.is_file() and p.name not in ('partial-audit.json','partial-audit.log')}}
(R/'partial-audit.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS UART-byte artifact audit; native qualification remains FAILED/incomplete')
