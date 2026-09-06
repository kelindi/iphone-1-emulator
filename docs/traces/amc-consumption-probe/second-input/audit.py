"""Capture-only independent oracle; no bytes from this file enter the device."""
from pathlib import Path
import hashlib,json,re,runpy,sys
out=Path(sys.argv[1]);sha=lambda b:hashlib.sha256(b).hexdigest()
trace=(out/'trace.log').read_text();old=Path('.context/amc-first/stage-h/run-1')
assert json.loads((out/'first/functional-audit.json').read_text())['passed']
assert ''.join(line for line in trace.splitlines(True) if not line.startswith('CAPTURE '))==(old/'trace.log').read_text(), 'original stop/prefix changed'
assert 'STOP unexpected first DMA request cycles9873247421 ticks1756770648 pcc0278130 lrc0274874 address3850100c width4 value00000001' in trace
capture=re.search(r'^CAPTURE second address([0-9a-f]+) words(\d+) bytes(\d+) cycles(\d+) ticks(\d+) pc([0-9a-f]+) lr([0-9a-f]+) cpsr([0-9a-f]+) firstPublication(\d+)$',trace,re.M);assert capture
assert [int(capture[i],16) for i in (1,6,7)]==[0x14ac1000,0xc0278130,0xc0274874]
assert [int(capture[i]) for i in (2,3,4,5,9)]==[4096,16384,9873247421,1756770648,9873234585]
assert int(capture[8],16)&0x3f==0x13
state=re.search(r'^CAPTURE preadmission (.*)$',trace,re.M);assert state
assert state[1]=='initialized1 configured1 dmaActive0 dmaStart14a8b000 dmaWords4096 copiedWords4096 copiedBytes16384 queueBytes16036 inputPending0 inputPhase5 transport1 events00000600 mask00002000 decodedBytes348 published1', 'preadmission state differs'
unchanged={}
for name in ('sram.bin','written.bin','init-written.bin','pcm-written.bin','host-written.bin','owned-queue.bin','first-pcm.bin','registers.bin','read-coverage.bin','read-bytes.bin','read-ledger.bin'):
 data=(out/name).read_bytes();assert data==(old/name).read_bytes(), 'state changed: '+name
 unchanged[name]=sha(data)
second=(out/'second-input.bin').read_bytes();assert len(second)==16384
oracle=runpy.run_path(str(Path(__file__).with_name('mp4_oracle.py')));media=oracle['media'];stream=oracle['stream']
def locations(haystack,needle):
 result=[];pos=0
 while True:
  at=haystack.find(needle,pos)
  if at<0:return result
  result.append(at);pos=at+1
file_matches=locations(media,second);stream_matches=locations(stream,second)
assert len(file_matches)==len(stream_matches)==1, 'captured bytes lack unique original provenance'
first=(out/'first/input-final.bin').read_bytes();first_matches=locations(stream,first);assert len(first_matches)==1
continuity=stream_matches[0]==first_matches[0]+len(first)
# Report actual observed offset, including any gap/overlap. Continuity is a result, not admission.
report={'passed':True,'scope':'Observation of second input only; transfer store unretired and unaccepted. No PCM consumption or sound.','capturedSHA256':sha(second),'bytes':len(second),'physicalAddress':capture[1],'fileOffsets':file_matches,'streamOffsets':stream_matches,'firstStreamOffsets':first_matches,'contiguous':continuity,'gapBytes':stream_matches[0]-(first_matches[0]+len(first)),'retiredAfterFirstPublication':12836,'unchangedArtifacts':unchanged,'preadmission':state[1]}
(out/'capture-audit.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
