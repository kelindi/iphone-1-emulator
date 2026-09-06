"""External audit of real captured/copy-owned bytes; no bytes feed the device."""
from pathlib import Path
import hashlib,json,re,runpy,struct,sys
FMT='<4Q10I';assert struct.calcsize(FMT)==72
sha=lambda b:hashlib.sha256(b).hexdigest()
def run(out):
 out=Path(out);trace=(out/'trace.log').read_text();base=Path('.context/amc-first/stage-n/run-1')
 assert trace.split('CAPTURE later request4',1)[0]==(base/'trace.log').read_text().split('STOP fourth input request cap',1)[0], 'Stage N preadmission trace differs'
 names=('sram.bin','expected.bin','predicted.bin','written.bin','init-written.bin','pcm-written.bin','host-written.bin','owned-queue.bin','registers.bin','first-pcm.bin','read-coverage.bin','read-bytes.bin','read-ledger.bin','ordering.bin')
 for name in names:assert (out/'fourth-request'/name).read_bytes()==(base/name).read_bytes(),'Stage N preadmission state differs: '+name
 for name in ('second-input.bin','third-input.bin'):assert (out/name).read_bytes()==(base/name).read_bytes()
 assert json.loads((out/'third-request-audit.json').read_text())['passed']
 oracle=runpy.run_path(str(Path(__file__).with_name('mp4_oracle.py')));captures={};provenance=[]
 def locate(hay,needle):
  rows=[];at=0
  while (at:=hay.find(needle,at))>=0:rows.append(at);at+=1
  return rows
 pattern=r'^CAPTURE later request([45]) address([0-9a-f]+) words(\d+) cycles(\d+) ticks(\d+) pc([0-9a-f]+) lr([0-9a-f]+) cpsr([0-9a-f]+) copiedBytes(\d+) queueBytes(\d+) decodedBytes(\d+) pending(\d+) active(\d+) events([0-9a-f]+) mask([0-9a-f]+) phase(\d+)$'
 requests=re.findall(pattern,trace,re.M);assert 1<=len(requests)<=2
 for n,row in enumerate(requests,4):
  number,addr,words,cycles,ticks,pc,lr,cpsr,copied,queued,decoded,pending,active,events,mask,phase=row
  assert int(number)==n and int(words)==4096 and int(pc,16)==0xc0278130 and int(lr,16)==0xc0274874 and int(cpsr,16)&0x3f==0x13
  addr=int(addr,16);assert addr%4==0 and addr+16384<=2**32 and int(copied)==16384*(n-1) and int(queued)==int(copied)-int(decoded) and 0<=int(queued)<=65536 and int(pending)==int(active)==0 and int(phase)==5
  if n==4:assert (addr,int(cycles),int(ticks),int(queued),int(decoded),events,mask)==(0x14a8b000,9873476740,1756773987,48804,348,'00000600','00002000')
  assert f'ACCEPT later request{n} address{addr:08x} words4096 cycles{cycles}; actual live RAM DMA only' in trace,'capture was not admitted'
  data=(out/('fourth-input.bin' if n==4 else 'fifth-input.bin')).read_bytes();assert len(data)==16384
  file_at=locate(oracle['media'],data);stream_at=locate(oracle['stream'],data);assert len(file_at)==len(stream_at)==1,'actual later input lacks unique original provenance'
  prev=32768 if n==4 else provenance[-1]['streamOffsets'][0]
  provenance.append({'request':n,'address':f'{addr:08x}','bytes':len(data),'sha256':sha(data),'fileOffsets':file_at,'streamOffsets':stream_at,'gapAfterPrevious':stream_at[0]-(prev+16384)})
  captures[n]=(addr,data)
 assert trace.count('ACCEPT later request')==len(captures) and 'ACCEPT later request6' not in trace
 blob=(out/'dma-copies.bin').read_bytes();data=(out/'dma-bytes.bin').read_bytes();assert len(blob)%72==0 and len(blob)<=720 and len(data)<=32768
 rows=list(struct.iter_unpack(FMT,blob));logs=re.findall(r'^DMA COPY .*$',trace,re.M);assert len(rows)==len(logs)
 used=0;words={4:0,5:0};total=49152;last_cycle=0
 for row,log in zip(rows,logs):
  cycles,copied_before,decoded_before,decoded_after,request,address,bytes_,offset,before,after,qbefore,qafter,pending,active=row
  assert request in captures and request==4+int(words[4]==4096) and 0<bytes_<=4096 and bytes_%4==0
  assert copied_before==total and before==words[request] and after==before+bytes_//4<=4096 and offset==used and cycles>=last_cycle
  addr,full=captures[request];assert address==addr+4*before and data[offset:offset+bytes_]==full[4*before:4*after], 'actual copied bytes differ from capture'
  assert qbefore==total-decoded_before and 0<=qbefore<=65536 and bytes_==min((65536-qbefore)//4,1024,4096-before)*4
  assert decoded_before<=decoded_after<=total+bytes_ and qafter==total+bytes_-decoded_after<=65536
  assert pending==int(after==4096) and active==int(after!=4096),'DMA pending/active differs'
  expected=f'DMA COPY request{request} address{address:08x} bytes{bytes_} offset{offset} cycles{cycles} copiedBefore{copied_before} decodedBefore{decoded_before} decodedAfter{decoded_after} wordsBefore{before} wordsAfter{after} queueBefore{qbefore} queueAfter{qafter} pending{pending} active{active}'
  assert log==expected,'DMA record/log differs'
  words[request]=after;used+=bytes_;total+=bytes_;last_cycle=cycles
 assert used==len(data),'unowned ledger bytes'
 final=re.findall(r'^DMA FINAL accepted(\d+) start([0-9a-f]+) words(\d+) copiedWords(\d+) current([0-9a-f]+) copiedBytes(\d+) queueBytes(\d+) decodedBytes(\d+) pending(\d+) active(\d+) ledgerRecords(\d+) ledgerBytes(\d+)$',trace,re.M);assert len(final)==1
 accepted,start,count,copied_words,current,copied,queued,decoded,pending,active,records,size=final[0];accepted=int(accepted);cw=int(copied_words)
 assert accepted==3+len(captures) and int(start,16)==captures[accepted][0] and int(count)==4096 and cw==words[accepted] and int(current,16)==int(start,16)+4*cw,'DMA final current/ownership differs'
 assert int(copied)==total and int(queued)==total-int(decoded) and 0<=int(queued)<=65536 and int(records)==len(rows) and int(size)==len(data)
 assert int(active)==int(cw<4096) and (not int(pending) or cw==4096),'DMA final pending/active differs'
 # Every owned byte is real capture/copy history; never count an entire staged fifth transfer.
 real=(out/'first/input-final.bin').read_bytes()+(out/'second-input.bin').read_bytes()+(out/'third-input.bin').read_bytes()+data
 assert len(real)==total and (out/'owned-queue.bin').read_bytes()==real[int(decoded):], 'owned queue differs from actual copied inputs'
 report={'passed':True,'scope':'Exact Stage N prefix and actual bounded fourth/fifth capture/copy provenance, not playback.','requests':provenance,'copyRecords':len(rows),'laterCopiedBytes':len(data),'copiedBytes':total,'decodedBytes':int(decoded),'queueBytes':int(queued),'currentAddress':current,'active':bool(int(active)),'pending':bool(int(pending)),'currentCopiedWords':cw,'ledgerSHA256':sha(blob),'copiedSHA256':sha(data)}
 (out/'later-audit.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':run(sys.argv[1])
