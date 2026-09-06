"""Independent generation/read/retirement and actual-copy conservation oracle."""
from pathlib import Path
import json,struct,re,hashlib,sys
sha=lambda b:hashlib.sha256(b).hexdigest()
def run(p):
 p=Path(p);trace=(p/'trace.log').read_text();s=json.loads((p/'stream.json').read_text());gs=s['generations'];assert 2<=len(gs)<=64 and s['published']==len(gs),'generation count differs'
 baseline=Path('.context/amc-first/stage-t/run-1');marker='STOP third publication work cap'
 old=(baseline/'trace.log').read_text().split(marker)[0];assert trace.startswith(old),'fresh Stage T trace differs'
 assert s['gateSaved'] and 'STREAM GATE cycles9873752646 pcc0277fbc publications2 releases1 queued65533 copied66212 decoded679 service1;' in trace,'fresh T gate differs'
 for a in (p/'stream-gate').glob('*.bin'):assert a.read_bytes()==(baseline/a.name).read_bytes(),'fresh T state differs: '+a.name
 assert len(list((p/'stream-gate').glob('*.bin')))==8,'fresh T artifacts absent'
 oracle=Path('.context/amc-first/stage-e/framing/run-1/packetized.pcm').read_bytes();raw=Path('.context/amc-first/stage-e/framing/fixture/stream.aac').read_bytes()
 assert sha(oracle)=='44feeb01ec9697c2f620d2262d606a8c3cf0bd657ed746ad8c0eacee8d3c077f','PCM oracle identity'
 # Independently retained MP4 parser establishes raw stream from original file.
 from mp4_oracle import stream as original_stream
 assert raw==original_stream,'raw stream differs from independently parsed original MP4'
 seen=[bytearray(4096) for g in gs];cover=[bytearray(4096) for g in gs];reads=[0]*len(gs)
 ledger=(p/'stream-reads.bin').read_bytes();assert len(ledger)==s['readRecords']*40<=65536*40,'read ledger size'
 previous=0
 for row in struct.iter_unpack('<Q8I',ledger):
  cycle,n,slot,a,w,v,pc,cpsr,owner=row;assert 1<=n<=len(gs) and slot==(n-1)%2 and w in (1,2,4) and owner==0,'read generation/width/owner invalid'
  g=gs[n-1];assert g['slot']==slot and cycle>=g['published'] and cycle>=previous,'read time invalid';previous=cycle
  if g['released']:assert cycle<=g['releaseCallback'],'read after release'
  if n+2<=len(gs):assert cycle<gs[n+1]['published'],'read assigned to overwritten generation'
  base=0x2200f220+8192*slot;lo=max(a,base);hi=min(a+w,base+4096);assert lo<hi,'read span absent'
  actual=v.to_bytes(4,'little');pcm=(p/f'stream-pcm-{n}.bin').read_bytes();assert len(pcm)==4096 and pcm==oracle[(n-1)*4096:n*4096],'actual decoded PCM differs'
  for address in range(lo,hi):
   i=address-base;assert actual[address-a]==pcm[i],'observed generation bytes differ';seen[n-1][i]=actual[address-a];cover[n-1][i]=1
  reads[n-1]+=1
 released=0
 for i,g in enumerate(gs):
  n=i+1;assert g['ordinal']==n and g['slot']==i%2 and g['samples']==2048*n and g['events']&0x400,'publication schema differs'
  assert g['copied']>=g['decoded'] and g['copied']-g['decoded']<=65536,'publication conservation differs'
  assert (p/f'stream-pcm-{n}.bin').read_bytes()==oracle[i*4096:n*4096],'actual decoded PCM differs'
  assert (p/f'stream-seen-{n}.bin').read_bytes()==seen[i] and (p/f'stream-coverage-{n}.bin').read_bytes()==cover[i],'generation read receipt differs'
  assert g['covered']==sum(cover[i]) and g['reads']==reads[i],'generation coverage count differs'
  if i>=2:assert gs[i-2]['released'] and gs[i-2]['releaseEnd']<g['published'],'slot overwritten before genuine release'
  if g['released']:
   released+=1;assert all(cover[i]) and g['ipc'] and g['ack'] and not g['pending'],'released generation lacks coverage/IPC/ack'
   assert g['releaseStart']+1==g['releaseCallback']==g['releaseEnd'] and g['afterPC']==g['releasePC']+4 and g['afterCPSR']==g['releaseCPSR'] and g['releaseCPSR']&63==0x13,'generation retirement differs'
   assert g['releaseAddress']==0x2200f208+2*g['slot'] and g['index']==g['slot'] and g['driver'],'generation owner slot differs'
   assert (g['releasePC'] in (0xc02726a8,0xc02726c4) and g['releaseOpcode']==0xe1c030b0) or (g['releasePC']==0xc0272690 and g['releaseOpcode']==0x01c120b0 and g['releaseCPSR']&0x40000000),'generation original store differs'
   assert g['published']<=g['ackCycles']<=g['releaseCallback'] and g['published']<=g['ipcCycles']<=g['releaseCallback'],'generation event order differs'
   store=f"ENGINE SRAM W {g['releaseAddress']:08x} width2 value00000001 pc{g['releasePC']:08x}"
   assert store in trace,'generation actual owner store missing'
   if n>1:
    receipt=f"STREAM RELEASE generation{n} slot{g['slot']} start{g['releaseStart']} callback{g['releaseCallback']} end{g['releaseEnd']} pc{g['releasePC']:08x} cpsr{g['releaseCPSR']:08x} opcode{g['releaseOpcode']:08x} afterPC{g['afterPC']:08x} afterCPSR{g['afterCPSR']:08x} address{g['releaseAddress']:08x} driver{g['driver']:08x} index{g['index']} emuRunReturned1"
    assert receipt in trace,'generation actual retirement receipt missing'
    assert f"STREAM IPC generation{n} cycles{g['ipcCycles']} return0" in trace,'generation actual IPC missing'
    # Generation2 can have its ack before the frozen first-owner release.
    if g['ackCycles']>9873752634:assert f"STREAM ACK generation{n} cycles{g['ackCycles']} actualB04" in trace,'generation actual ack missing'
 assert released==s['released'],'release total differs'
 captures={1:(p/'input-final.bin').read_bytes(),2:(p/'second-input.bin').read_bytes(),3:(p/'third-input.bin').read_bytes()};offsets={}
 for n,name in [(4,'fourth-input.bin'),(5,'fifth-input.bin'),(6,'input-6.bin'),(7,'input-7.bin'),(8,'input-8.bin')]:
  if (p/name).exists():captures[n]=(p/name).read_bytes()
 for n,data in captures.items():
  at=raw.find(data);assert len(data)==16384 and at>=0 and raw.find(data,at+1)<0,'actual input lacks unique original provenance';offsets[n]=at
 rows=(p/'dma-copies.bin').read_bytes();data=(p/'dma-bytes.bin').read_bytes();assert len(rows)%72==0 and len(rows)<=1024*72 and len(data)<=81920,'copy ledger bounds'
 copied=49152;used=0;request_words={};priorcycle=0
 for row in struct.iter_unpack('<4Q10I',rows):
  cycle,before,db,da,request,a,size,off,wb,wa,qb,qa,pending,active=row
  assert 4<=request<=8 and request in captures and size>0 and size<=4096 and size%4==0 and off==used and before==copied and cycle>=priorcycle,'actual copy ledger ordering'
  assert wb==request_words.get(request,0) and wa==wb+size//4 and wa<=4096,'actual copied word bounds'
  assert data[off:off+size]==captures[request][4*wb:4*wa],'actual copied bytes differ from capture'
  cap=re.search(rf'^CAPTURE later request{request} address([0-9a-f]+) words4096 ',trace,re.M);assert cap and a==int(cap[1],16)+4*wb,'actual DMA address differs'
  assert qb==before-db and qa==before+size-da and 0<=qa<=65536 and db<=da<=before+size,'actual queue conservation differs'
  assert (pending,active)==((1,0) if wa==4096 else (0,1)),'actual transfer completion differs'
  copied+=size;used+=size;request_words[request]=wa;priorcycle=cycle
 assert used==len(data),'unused copied ledger bytes'
 final=re.findall(r'^DMA FINAL accepted(\d+) start([0-9a-f]+) words(\d+) copiedWords(\d+) current([0-9a-f]+) copiedBytes(\d+) queueBytes(\d+) decodedBytes(\d+) pending(\d+) active(\d+) ledgerRecords(\d+) ledgerBytes(\d+)$',trace,re.M)[-1]
 accepted,start,words,cw,current,cb,qb,db,pending,active,nrows,nbytes=[int(x,16 if i in (1,4) else 10) for i,x in enumerate(final)]
 assert 5<=accepted<=8 and current==start+4*cw and cb==copied and qb==cb-db and nrows==len(rows)//72 and nbytes==used,'final actual DMA conservation differs'
 submitted=b''.join(captures[i] for i in (1,2,3))+data;assert len(submitted)==cb and submitted[db:]==(p/'owned-queue.bin').read_bytes(),'final queue differs from actual copied bytes'
 actual=(p/'sram.bin').read_bytes();initial=(p/'upload/sram.bin').read_bytes();masks=[(p/n).read_bytes() for n in ('init-written.bin','pcm-written.bin','host-written.bin')]
 assert len(actual)==len(initial)==180224 and all(len(m)==180224 for m in masks)
 for i,(x,y) in enumerate(zip(actual,initial)):
  assert x==y or any(m[i] for m in masks),'unattributed SRAM change'
 for slot in (0,1):
  g=next(g for g in reversed(gs) if g['slot']==slot);base=0xf220+8192*slot;assert actual[base:base+4096]==(p/f"stream-pcm-{g['ordinal']}.bin").read_bytes(),'latest slot PCM differs'
 report={'passed':True,'publishedGenerations':len(gs),'releasedGenerations':released,'actualReadRecords':s['readRecords'],'acceptedInputs':accepted,'actualInputRawOffsets':offsets,'copiedBytes':cb,'decodedBytes':db,'queueBytes':qb,'scope':'Actual isolated guest generation ownership/copy conservation; no I2S or audible sound claim.'}
 (p/'stream-audit.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':run(sys.argv[1])
