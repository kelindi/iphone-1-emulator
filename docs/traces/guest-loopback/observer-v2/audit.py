"""Independent retained trace audit; optional large-pin pass only outside quiet."""
from pathlib import Path
import hashlib,json,re,sys
B=Path(__file__).resolve().parent;R=B/'run-1'
request=b'GET /qualification HTTP/1.0\r\nHost: 127.0.0.1:18081\r\n\r\n'
response=b'HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 29\r\nConnection: close\r\n\r\nIPHONE1_GUEST_LOOPBACK_V1_OK\n'
def sha(data):return hashlib.sha256(data).hexdigest()
def audit(out,serial):
 out=out.replace('\r','');serial=re.sub(r'^\d+ ','',serial.replace('\r',''),flags=re.M)
 entries=re.findall(r'OWN entry cycle=(\d+) pc=([0-9a-f]+) cpsr=([0-9a-f]+) ttbr=([0-9a-f]+) exact_entry_text=(\d+)',out)
 assert len(entries)==1;cyc,pc,cpsr,ttbr,n=entries[0];assert int(pc,16)==0x1a60 and int(cpsr,16)&0x3f==0x10 and n=='64'
 assert out.count('OWN full_text bytes=4848 exact=1')==out.count('OWN exit_entry status=0')==1
 assert 'OWN final seen=1 entries=1 pass_written=1 serial_pass=1 exit_seen=1 exit_status=0 failed=0 full_text=1 ttbr='+ttbr in out
 assert 'reason=own-loopback-qualified instructions=261000000' in out
 assert serial.count('LOOPBACK_BEGIN_V1\n')==serial.count('LOOPBACK_PASS_V1\n')==1
 segment=serial[serial.index('LOOPBACK_BEGIN_V1\n'):serial.index('LOOPBACK_PASS_V1\n')];lines=[line for line in segment.splitlines() if line];rows=[];blobs=[];i=0
 while i<len(lines):
  m=re.fullmatch(r'(\w+) ([0-9a-f]{8}) ([0-9a-f]{8})',lines[i])
  if m:
   name=m[1];value=int(m[2],16);detail=int(m[3],16);rows.append((name,value,detail))
   if i+1<len(lines) and lines[i+1].startswith('D '):
    parts=[];i+=1
    while i<len(lines) and lines[i].startswith('D '):parts.append(bytes.fromhex(lines[i][2:]));i+=1
    data=b''.join(parts);assert len(data)==value;blobs.append((name,data));continue
  i+=1
 native_rows=[(a,int(b,16),int(c,16))for a,b,c in re.findall(r'OWN result label=(\w+) value=([0-9a-f]{8}) error_or_detail=([0-9a-f]{8})',out)]
 assert native_rows==rows
 native_blobs=[(a,bytes.fromhex(c))for a,b,c in re.findall(r'OWN bytes label=(\w+) length=(\d+) hex=([0-9a-f]*)',out)]
 assert native_blobs==blobs
 def row(name):
  values=[r[1:]for r in rows if r[0]==name];assert len(values)==1;return values[0]
 def blob(name):
  values=[r[1]for r in blobs if r[0]==name];assert len(values)==1;return values[0]
 for name in ['bind','listen','connect_status','close_response','close_client','close_server']:assert row(name)==(0,0)
 assert row('socket_server')==(3,0) and row('socket_client')==(4,0) and row('accept')==(5,16)
 assert row('connect')==(0xffffffff,36) and row('readiness_attempt')==(0,0)
 assert blob('actual_request')==request and blob('actual_response')==response
 assert blob('bound_address')==bytes.fromhex('100246a17f0000010000000000000000')
 assert blob('peer_address')==bytes.fromhex('1002c0007f0000010000000000000000')
 assert len(blob('ifconf_bytes'))==68 and len(blob('route_bytes'))==124
 calls=[];last=int(cyc)
 for match in re.finditer(r'OWN witness cycle=(\d+) pc=([0-9a-f]{8}) r0=([0-9a-f]{8}) r1=([0-9a-f]{8}) r2=([0-9a-f]{8}) r3=([0-9a-f]{8}) lr=([0-9a-f]{8})',out):
  c,p,*regs=[int(x,10 if i==0 else 16)for i,x in enumerate(match.groups())];assert c>=last;last=c;calls.append({'cycle':c,'pc':p,'r':regs})
 important=[c for c in calls if c['pc'] in [0x3000c400,0x3000e8e0,0x30030500,0x3000d2b0,0x3000e880,0x30030520,0x300099f0] or c['pc']in[0x300097e0,0x30009860]and c['r'][0]in[3,4,5]]
 expected=[(0x3000c400,2,0),(0x3000e8e0,3,16),(0x30030500,3,None),(0x3000c400,2,0),(0x3000d2b0,4,16),(0x3000e880,4,0x1007),(0x30030520,3,None),(0x30009860,4,54),(0x300097e0,5,54),(0x30009860,5,113),(0x300097e0,4,113),(0x300097e0,4,1),(0x300099f0,0,None)]
 assert len(important)==len(expected)
 for c,(pc,r0,r2)in zip(important,expected):assert c['pc']==pc and c['r'][0]==r0 and (r2 is None or c['r'][2]==r2)
 assert important[-1]['cycle']<261000000
 return {'entry':{'cycle':int(cyc),'pc':int(entries[0][1],16),'cpsr':cpsr,'ttbr':ttbr,'bytes':64},'nativeWitnesses':len(calls),'matchingResultRows':len(rows),'matchingNativeAndUARTBuffers':len(blobs),'orderedAPICalls':important,'request':{'bytes':54,'sha256':sha(request)},'response':{'bytes':113,'bodyBytes':29,'sha256':sha(response)},'interfaceHex':blob('ifconf_bytes').hex(),'routeHex':blob('route_bytes').hex(),'initialReady':True,'readinessRetries':0,'EOF':'Final one-byte original read entered; exact verified own code requires its return0 before PASS. Direct syscall-return instruction was not traced.'}
out=(R/'stdout.log').read_text();serial=(R/'serial.log').read_text();proof=audit(out,serial)
mutants={'wrong_entry_mode':out.replace('cpsr=40000010 ttbr=14a03000','cpsr=40000030 ttbr=14a03000',1),'wrong_full_text':out.replace('OWN full_text bytes=4848 exact=1','OWN full_text bytes=4848 exact=0',1),'wrong_bind':out.replace('OWN result label=bind value=00000000','OWN result label=bind value=ffffffff',1),'wrong_native_buffer':out.replace('label=actual_response length=113 hex=4854','label=actual_response length=113 hex=4954',1),'missing_eof_read':re.sub(r'OWN witness cycle=\d+ pc=300097e0 r0=00000004 r1=[0-9a-f]+ r2=00000001[^\n]*\n','',out,count=1),'wrong_exit':out.replace('OWN exit_entry status=0','OWN exit_entry status=1',1)}
for name,mutant in mutants.items():
 assert mutant!=out,name
 try:audit(mutant,serial)
 except AssertionError:continue
 raise AssertionError('accepted mutant '+name)
report=json.loads((R/'report.json').read_text());assert report['passed'] and report['returncode']==0 and report['elapsedSeconds']<900
assert report['sourcePinsSHA256']==sha((B/'source-pins.json').read_bytes())
pincheck=False
if '--verify-pins' in sys.argv:
 for p,h in json.loads((B/'source-pins.json').read_text()).items():
  actual=hashlib.sha256()
  with Path(p).open('rb')as f:
   for chunk in iter(lambda:f.read(1024*1024),b''):actual.update(chunk)
  assert actual.hexdigest()==h,p
 pincheck=True
result={'traceAuditPassed':True,'sourcePinsVerifiedAfterRun':pincheck,'passed':pincheck,'negativeArtifactChecks':list(mutants),'proof':proof,'artifactHashes':{n:sha((R/n).read_bytes())for n in ['report.json','stdout.log','serial.log','stderr.log']},'limitations':['No process-reap proof.','No Safari or Weather request.','No network interface/route mutation.','No speed claim.','Initial attempt retained incomplete; full-text demand-page explanation was a source-grounded inference, synthetic reproduction proved the unsafe admission pattern.']}
(R/'audit.json').write_text(json.dumps(result,indent=2)+'\n');print('Trace audit PASS,6 artifact negatives reject; post-run large pin verification='+str(pincheck))
