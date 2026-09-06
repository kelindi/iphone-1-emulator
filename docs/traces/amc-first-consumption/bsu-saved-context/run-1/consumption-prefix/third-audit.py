"""External original-byte proof for the newly admitted third physical request."""
from pathlib import Path
import hashlib,json,re,runpy,sys
out=Path(sys.argv[1]);baseline=Path('.context/amc-first/stage-j/run-1');trace=(out/'trace.log').read_text();clean=''.join(line for line in trace.splitlines(True) if not line.startswith('ORDER '))
assert json.loads((out/'second-request-audit.json').read_text())['passed']
assert clean.count('ACCEPT third request')==1
assert clean.split('CAPTURE third address',1)[0]==(baseline/'trace.log').read_text().split('STOP third input request cap',1)[0], 'third preadmission trace differs'
assert 'CAPTURE third-preadmission copiedBytes32768 copiedWords4096 queueBytes32420 decodedBytes348 decodedFrames2 published1 events00000600 mask00002000 inputPending0 dmaActive0 phase5' in trace
capture=re.search(r'^CAPTURE third address([0-9a-f]+) words(\d+) bytes(\d+) cycles(\d+) ticks(\d+) pc([0-9a-f]+) lr([0-9a-f]+) cpsr([0-9a-f]+)$',trace,re.M);assert capture
assert [int(capture[i],16) for i in (1,6,7)]==[0x14ae1000,0xc0278130,0xc0274874]
assert [int(capture[i]) for i in (2,3,4,5)]==[4096,16384,9873362515,1756772324] and int(capture[8],16)&0x3f==0x13
assert 'ACCEPT third request physical14ae1000 words4096 cycles9873362515; actual live RAM DMA only' in trace
sha=lambda data:hashlib.sha256(data).hexdigest();files={}
for name in ('sram.bin','written.bin','init-written.bin','pcm-written.bin','host-written.bin','owned-queue.bin','first-pcm.bin','registers.bin','read-coverage.bin','read-bytes.bin','read-ledger.bin'):
 data=(out/'third-request'/name).read_bytes();assert data==(baseline/name).read_bytes(), 'third preadmission state differs: '+name;files[name]=sha(data)
actual=(out/'third-input.bin').read_bytes();assert len(actual)==16384
oracle=runpy.run_path(str(Path(__file__).with_name('mp4_oracle.py')))
def locate(haystack,needle):
 positions=[];cursor=0
 while True:
  at=haystack.find(needle,cursor)
  if at<0:return positions
  positions.append(at);cursor=at+1
files_at=locate(oracle['media'],actual);stream_at=locate(oracle['stream'],actual)
assert len(files_at)==len(stream_at)==1, 'actual third input lacks unique original provenance'
# Continuity is reported, not assumed or used to supply device bytes.
report={'passed':True,'scope':'Actual third live DMA input and preserved preadmission state. No fourth admission or sound claim.','inputSHA256':sha(actual),'bytes':len(actual),'fileOffsets':files_at,'streamOffsets':stream_at,'contiguousAfterPrevious':stream_at[0]==32768,'gapBytes':stream_at[0]-32768,'preadmissionArtifacts':files}
(out/'third-request-audit.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
