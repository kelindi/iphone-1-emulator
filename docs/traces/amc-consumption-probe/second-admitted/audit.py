"""Frozen external read/release auditor. No device implementation imports it."""
from pathlib import Path
import hashlib,json,re,struct,sys

def audit_reads(ledger,coverage,observed,pcm,first_cycles,release_cycles):
 assert len(pcm)==len(coverage)==len(observed)==4096
 assert len(ledger)%32==0 and 0<len(ledger)<=65536*32
 actual=bytearray(4096);seen=bytearray(4096);previous=first_cycles
 for address,width,value,pc,cpsr,owner,cycles in struct.iter_unpack('<6IQ',ledger):
  assert width in (1,2,4) and owner==0 and (cpsr&31) in (0x10,0x11,0x12,0x13,0x17,0x1b,0x1f)
  assert previous<=cycles<=release_cycles;previous=cycles
  assert pc and pc%(2 if cpsr&0x20 else 4)==0, 'reader ISA alignment differs'
  assert address+width>0x2200f220 and address<0x22010220
  for i in range(width):
   physical=address+i
   if 0x2200f220<=physical<0x22010220:
    offset=physical-0x2200f220;byte=(value>>(8*i))&255
    assert byte==pcm[offset], 'returned ledger byte differs'
    actual[offset]=byte;seen[offset]=1
 assert coverage==seen==bytes([1])*4096, 'missing actual read coverage'
 assert observed==actual==pcm, 'observed read bytes differ'
 return len(ledger)//32

def audit_release(trace,release,retired,pc,cpsr):
 stores=list(re.finditer(r'^ENGINE SRAM W 2200f208 width(\d+) value([0-9a-f]+) pc([0-9a-f]+)$',trace,re.M))
 assert len(stores)==1, 'actual owner store differs'
 store=stores[0]
 assert (int(store[1]),int(store[2],16),int(store[3],16))==(2,1,pc), 'actual owner store differs'
 assert pc in (0xc0272690,0xc02726a8,0xc02726c4) and retired==release+1
 accepted=re.search(r'^OWNER WRITE accepted address2200f208 width2 value00000001 before0 after1 cycles(\d+) pc([0-9a-f]+) cpsr([0-9a-f]+)$',trace,re.M)
 assert accepted and (int(accepted[1]),int(accepted[2],16),int(accepted[3],16))==(release,pc,cpsr), 'accepted owner transition differs'
 pending=re.search(r'^RELEASE storePending cycles(\d+) pc([0-9a-f]+) cpsr([0-9a-f]+) covered4096 path1 index0 driverccaa2c00$',trace,re.M)
 assert pending and (int(pending[1]),int(pending[2],16),int(pending[3],16))==(release,pc,cpsr)
 completed=trace.index('PASS first PCM read and ownership returned')
 assert store.end()<accepted.start()<accepted.end()<pending.start()<pending.end()<completed
 assert not re.search(r'^(?:AMC W|ENGINE SRAM W|PUBLICATION second|PCM READ|CONSUMER HOOK)',trace[pending.end():completed],re.M), 'work occurred after release'
 return True

def run(directory):
 out=Path(directory);first=out/'first';trace=(out/'trace.log').read_text();continuation=trace.split('CONTINUE frozen075 boundary matched;',1)[1]
 assert json.loads((first/'functional-audit.json').read_text())['passed']
 assert 'STOP ' not in continuation and 'PASS first PCM read and ownership returned' in continuation
 final=re.search(r'CONSUMPTION result(\d+) covered(\d+) reads(\d+) ipc(\d+) copy(\d+) acknowledged(\d+) enabled(\d+) service(\d+) releasePending(\d+) releaseCycles(\d+) retiredCycles(\d+) releasePC([0-9a-f]+) releaseCPSR([0-9a-f]+) index(\d+) driver([0-9a-f]+) published(\d+)',trace)
 assert final
 result,covered,reads,ipc,copied,ack,enabled,service,pending,release,retired=map(int,final.groups()[:11])
 assert (result,covered,ack,enabled,service,pending)==(6,4096,1,1,1,1)
 assert ipc or copied
 assert retired==release+1 and int(final[12],16) in (0xc0272690,0xc02726a8,0xc02726c4)
 assert int(final[13],16)&0x20==0 and (int(final[13],16)&31) in (0x11,0x12,0x13,0x17,0x1b,0x1f)
 assert int(final[14])==0 and int(final[15],16)==0xccaa2c00
 audit_release(continuation,release,retired,int(final[12],16),int(final[13],16))
 publications=int(final[16]);assert publications in (1,2)
 pcm=(first/'first-pcm.bin').read_bytes()
 count=audit_reads((out/'read-ledger.bin').read_bytes(),(out/'read-coverage.bin').read_bytes(),(out/'read-bytes.bin').read_bytes(),pcm,9873234585,release)
 assert count==reads
 if ipc:assert 'HANDOFF IPC type1 index0 count2048 original kernel return0' in continuation
 if copied:
  call=re.search(r'COPY call pcc0273a50 source([0-9a-f]+) destination([0-9a-f]+) bytes4096 cpsr([0-9a-f]+) supported1',continuation)
  returned=re.search(r'HANDOFF COPY return pcc0273a54 source([0-9a-f]+) destination([0-9a-f]+) bytes4096 cpsr([0-9a-f]+) exact1',continuation)
  assert call and returned and call.groups()[:2]==returned.groups()[:2]
  assert all(int(x[3],16)&0x20==0 and (int(x[3],16)&31) in (0x11,0x12,0x13,0x17,0x1b,0x1f) for x in (call,returned))
  assert (out/'copy-destination.bin').read_bytes()==pcm
 # Replay actual control writes; no acknowledgement inferred from a label alone.
 events=0x600;mask=0x2000;real_ack=False;real_enable=False;real_service=False
 for line in continuation.splitlines():
  m=re.match(r'AMC W ([0-9a-f]+) width4 value([0-9a-f]+)',line)
  if m:
   address,value=map(lambda x:int(x,16),m.groups())
   if address==0x38500b0c:mask|=value;real_enable|=bool(mask&0x400)
   if address==0x38500b10:mask&=~value
   if address==0x38500b04:real_ack|=bool(events&value&0x400);events&=~value
   if address==0x38500004:assert value==1;real_service=True
  if line.startswith('PUBLICATION second'):
   assert not events&0x400 and real_service;events|=0x400
 assert real_ack and real_enable and real_service
 rows=re.findall(r'ENGINE state configured(\d+) copiedBytes(\d+) copiedWords(\d+) queueBytes(\d+) decodedBytes(\d+) decodedFrames(\d+) publishedFrames(\d+) sampleUnits(\d+) events([0-9a-f]+) mask([0-9a-f]+) inputPending(\d+)',trace)
 assert int(rows[-1][8],16)==events and int(rows[-1][9],16)==mask
 configured,input_bytes,words,queued,consumed,frames,pubs,samples=map(int,rows[-1][:8])
 assert (configured,pubs,frames,samples)==(1,publications,publications+1,2048*publications)
 assert 0<=words<=4096 and input_bytes==16384+4*words and 16384<=input_bytes<=32768
 assert json.loads((out/'second-request-audit.json').read_text())['passed']
 metadata=(first/'packet-data.bin').read_bytes();sizes=[struct.unpack_from('<II',metadata,4+8*i)[1] for i in range(frames)]
 assert consumed==sum(sizes) and queued==input_bytes-consumed
 actual_dma=(first/'input-final.bin').read_bytes()+(out/'second-input.bin').read_bytes()[:input_bytes-16384]
 assert (out/'owned-queue.bin').read_bytes()==actual_dma[consumed:]
 golden=Path('.context/amc-first/stage-e/framing/run-1/packetized.pcm').read_bytes()
 assert hashlib.sha256(golden).hexdigest()=='44feeb01ec9697c2f620d2262d606a8c3cf0bd657ed746ad8c0eacee8d3c077f'
 if publications==2:assert (out/'second-pcm.bin').read_bytes()==golden[4096:8192]
 actual=(out/'sram.bin').read_bytes();original=(first/'upload/sram.bin').read_bytes()
 init=(out/'init-written.bin').read_bytes();pcm_mask=(out/'pcm-written.bin').read_bytes();host=(out/'host-written.bin').read_bytes()
 assert all(len(x)==0x2c000 for x in (actual,original,init,pcm_mask,host))
 assert init==(first/'init-written.bin').read_bytes()
 expected=bytearray(0x2c000)
 for slot in range(publications):
  off=(0xf220,0x11220)[slot];expected[off:off+4096]=b'\1'*4096;expected[0xf208+slot*2:0xf20a+slot*2]=b'\1'*2;expected[0xf20c+slot*2:0xf20e+slot*2]=b'\1'*2
  assert actual[off:off+4096]==golden[4096*slot:4096*(slot+1)]
 assert pcm_mask==expected
 allowed=set(range(0xf208,0xf20c))|set(range(0x2a000,0x2a006))|set(range(0x2a00a,0x2a00c))
 assert all(v in (0,1) and (not v or i in allowed) for i,v in enumerate(host))
 assert host[0xf208:0xf20a]==b'\1\1'
 assert actual[0x2a000:0x2a00c]==(first/'sram.bin').read_bytes()[0x2a000:0x2a00c]
 assert actual[0xf208:0xf214]==struct.pack('<6H',1,0 if publications==2 else 1,2048,2048 if publications==2 else 0,0x10,0x1010)
 for i in range(len(actual)):
  if not(init[i] or pcm_mask[i] or host[i]):assert actual[i]==original[i]
 report={'passed':True,'scope':'Actual source reads, original IPC or independently checked copy handoff, event acknowledgement and retired owner release. No downstream audio/playback claim.','readBytes':4096,'successfulReadRecords':reads,'ipcHandoff':bool(ipc),'copyHandoff':bool(copied),'releaseCycles':release,'retiredCycles':retired,'publications':publications,'pcmSHA256':hashlib.sha256(pcm).hexdigest()}
 (out/'consumption-audit.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':run(sys.argv[1])
