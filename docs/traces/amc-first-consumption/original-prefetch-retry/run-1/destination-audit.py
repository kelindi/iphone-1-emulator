"""Exact083 plus original-library binding and real captured destination bytes."""
from pathlib import Path
import hashlib,json,struct,sys,re
LIB=Path('.context/amc-first/downstream/libSystem.original')
SHA='6243ad0b202fa364ac7b243f0416013f81416e32abb3c69e30c6b020250448e2'
def original_window(pc):
 data=LIB.read_bytes();assert hashlib.sha256(data).hexdigest()==SHA;off=28
 for _ in range(struct.unpack_from('<I',data,16)[0]):
  cmd,n=struct.unpack_from('<II',data,off)
  if cmd==1:
   va,vs,fo,fs=struct.unpack_from('<4I',data,off+24)
   if va<=pc and pc+32<=va+fs:return data[fo+pc-va:fo+pc-va+32]
  off+=n
 raise AssertionError('reader original mapping absent')
def run(out):
 out=Path(out);base=Path('.context/amc-first/stage-q/run-1');trace=(out/'trace.log').read_text();clean=lambda t:''.join(x for x in t.splitlines(True) if not x.startswith('DESTINATION '))
 assert clean(trace)==(base/'trace.log').read_text(),'083 trace differs'
 for name in ('sram.bin','expected.bin','predicted.bin','written.bin','init-written.bin','pcm-written.bin','host-written.bin','owned-queue.bin','registers.bin','first-pcm.bin','second-pcm.bin','read-coverage.bin','read-bytes.bin','read-ledger.bin','ordering.bin','dma-copies.bin','dma-bytes.bin'):
  assert (out/name).read_bytes()==(base/name).read_bytes(),'083 state differs: '+name
 for name in ('consumption-audit.json','retirement-audit.json','later-audit.json','queue-audit.json','ordering-audit.json'):assert json.loads((out/name).read_text())['passed']
 d=json.loads((out/'destination.json').read_text());assert d['captured'] and d['finished'];r=d['registers'];assert len(r)==16
 assert d['cpsr']&0x3f==0x10 and r[15]==0x3009a20c and r[2]==4032 and d['sourcePhysical']==0x2200f220,'actual user reader/transfer differs'
 assert 0<=r[0]<r[0]+4096<=2**32 and 12<=r[7] and r[7]+8<=2**32 and 0<=r[1]<2**32
 assert (out/'destination-code.bin').read_bytes()==original_window(0x3009a20c),'mapped reader bytes differ'
 assert (out/'destination-prologue.bin').read_bytes()==original_window(0x3009a1bc),'mapped prologue bytes differ'
 frame=(out/'destination-frame.bin').read_bytes();assert len(frame)==20 and struct.unpack_from('<I',frame,0)[0]==r[0] and struct.unpack_from('<I',frame,16)[0]==r[14],'actual saved frame differs'
 caller=(out/'destination-caller.bin').read_bytes();ret=r[14]&~1
 if ret>=4:assert d['callerWindowAddress']==ret-4
 else:assert not d['callerWindowAvailable'] and not d['callerWindowAddress']
 assert len(caller)==(32 if d['callerWindowAvailable'] else 0),'caller availability differs'
 rows=d['destinationMappings'];assert 1<=len(rows)<=5
 at=0
 for physical,n in rows:
  assert n==min(4096-at,1024-((r[0]+at)&1023)) and 0x10000000<=physical<physical+n<=0x20000000
  at+=n
 assert at==4096,'destination mapping bounds differ'
 pcm=(out/'first/first-pcm.bin').read_bytes();actual=(out/'destination.bin').read_bytes();assert actual==pcm,'actual copied destination differs'
 first=next(struct.iter_unpack('<6IQ',(out/'read-ledger.bin').read_bytes()));assert first[0]==d['sourcePhysical'] and first[3]==r[15] and first[4]==d['cpsr'] and first[6]==d['captureCycles']
 assert d['finishCycles']==9873752634 and d['captureCycles']<=d['finishCycles']
 begin=f"DESTINATION BEGIN cycles{d['captureCycles']} source{r[1]:08x} destination{r[0]:08x} readerPC{r[15]:08x} cpsr{d['cpsr']:08x} savedLR{r[14]:08x} callerWindowAvailable{int(d['callerWindowAvailable'])} ttbr0{d['ttbr0']:08x} ttbr1{d['ttbr1']:08x} ttbcr{d['ttbcr']:08x} dacr{d['dacr']:08x}"
 end=f"DESTINATION VERIFIED source{r[1]:08x} destination{r[0]:08x} bytes4096 captureCycles{d['captureCycles']} finishCycles{d['finishCycles']} retainedUserContext1"
 assert re.findall(r'^DESTINATION BEGIN.*$',trace,re.M)==[begin] and re.findall(r'^DESTINATION VERIFIED.*$',trace,re.M)==[end],'destination context/log differs'
 report={'passed':True,'scope':'Exact083, actual original-mapped user copy/frame/destination and retained-user-mapping revalidation. No downstream DMA or playback.','sourceVirtual':hex(r[1]),'destinationVirtual':hex(r[0]),'copiedBytes':len(actual),'pcmSHA256':hashlib.sha256(actual).hexdigest(),'savedReturn':hex(r[14]),'callerWindowAvailable':d['callerWindowAvailable'],'callerWindowAddress':hex(d['callerWindowAddress']),'callerImageIdentityEstablished':False,'capturedUSRContext':{k:d[k] for k in ('cpsr','sctlr','ttbr0','ttbr1','ttbcr','dacr')},'physicalMappings':rows,'captureCycles':d['captureCycles'],'finishCycles':d['finishCycles']}
 (out/'destination-audit.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':run(sys.argv[1])
