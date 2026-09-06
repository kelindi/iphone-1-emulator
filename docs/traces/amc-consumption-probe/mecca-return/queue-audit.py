"""Final real DMA ownership audit, also valid at a preserved earlier stop."""
from pathlib import Path
import json,re,sys,hashlib,struct
out=Path(sys.argv[1]);trace=(out/'trace.log').read_text();assert json.loads((out/'third-request-audit.json').read_text())['passed']
rows=re.findall(r'^ENGINE state configured(\d+) copiedBytes(\d+) copiedWords(\d+) queueBytes(\d+) decodedBytes(\d+) decodedFrames(\d+) publishedFrames(\d+) sampleUnits(\d+) events([0-9a-f]+) mask([0-9a-f]+) inputPending(\d+)$',trace,re.M);assert rows
configured,copied,words,queued,decoded,frames,pubs,samples=map(int,rows[-1][:8]);assert configured==1 and 0<=words<=4096 and copied==32768+4*words and pubs in (1,2)
real=(out/'first/input-final.bin').read_bytes()+(out/'second-input.bin').read_bytes()+(out/'third-input.bin').read_bytes()[:4*words]
assert len(real)==copied and 0<=decoded<=copied and queued==copied-decoded
queue=(out/'owned-queue.bin').read_bytes();assert queue==real[decoded:], 'owned queue differs from actual copied inputs'
assert trace.count('ACCEPT input request')==trace.count('ACCEPT second request')==trace.count('ACCEPT third request')==1
assert 'ACCEPT fourth' not in trace
assert frames==pubs+1 and samples==pubs*2048
metadata=(out/'first/packet-data.bin').read_bytes();sizes=[struct.unpack_from('<II',metadata,4+8*i)[1] for i in range(frames)];assert decoded==sum(sizes)
golden=Path('.context/amc-first/stage-e/framing/run-1/packetized.pcm').read_bytes();assert hashlib.sha256(golden).hexdigest()=='44feeb01ec9697c2f620d2262d606a8c3cf0bd657ed746ad8c0eacee8d3c077f'
sram=(out/'sram.bin').read_bytes();original=(out/'first/upload/sram.bin').read_bytes();init=(out/'init-written.bin').read_bytes();pcm=(out/'pcm-written.bin').read_bytes();host=(out/'host-written.bin').read_bytes();assert len(sram)==len(init)==len(pcm)==len(host)==180224
assert init==(out/'first/init-written.bin').read_bytes();expected=bytearray(180224)
for slot in range(pubs):
 start=(0xf220,0x11220)[slot];assert sram[start:start+4096]==golden[4096*slot:4096*(slot+1)];expected[start:start+4096]=b'\1'*4096;expected[0xf208+2*slot:0xf20a+2*slot]=b'\1'*2;expected[0xf20c+2*slot:0xf20e+2*slot]=b'\1'*2
assert pcm==expected
allowed=set(range(0xf208,0xf20c))|set(range(0x2a000,0x2a006))|set(range(0x2a00a,0x2a00c));assert all(v in (0,1) and (not v or i in allowed) for i,v in enumerate(host))
assert all(sram[i]==original[i] for i in range(len(sram)) if not(init[i] or pcm[i] or host[i]))
report={'passed':True,'copiedBytes':copied,'thirdCopiedBytes':4*words,'queueBytes':queued,'decodedBytes':decoded,'queueSHA256':hashlib.sha256(queue).hexdigest(),'publications':pubs,'consumptionClaim':False,'scope':'All owned bytes equal actual captured inputs minus decoded bytes; separate consumption audit required.'}
(out/'queue-audit.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
