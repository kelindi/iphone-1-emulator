from pathlib import Path
import hashlib,json,runpy,struct
base=Path('.context/amc-first/stage-j');out=base/'run-1';first=out/'first';trace=(out/'trace.log').read_text()
assert json.loads((out/'second-request-audit.json').read_text())['passed']
assert 'STOP third input request cap cycles9873362515 ticks1756772324 pcc0278130 lrc0274874 address3850100c width4 value00000001' in trace
assert 'ENGINE state configured1 copiedBytes32768 copiedWords4096 queueBytes32420 decodedBytes348 decodedFrames2 publishedFrames1 sampleUnits2048 events00000600 mask00002000 inputPending0' in trace
actual=(first/'input-final.bin').read_bytes()+(out/'second-input.bin').read_bytes();queue=(out/'owned-queue.bin').read_bytes()
assert len(actual)==32768 and len(queue)==32420 and queue==actual[348:]
oracle=runpy.run_path(str(base/'mp4_oracle.py'));assert actual==oracle['stream'][:32768]
sha=lambda x:hashlib.sha256(x).hexdigest();unchanged={}
for name in ('sram.bin','written.bin','init-written.bin','pcm-written.bin','host-written.bin','first-pcm.bin'):
 data=(out/name).read_bytes();assert data==(first/name).read_bytes();unchanged[name]=sha(data)
assert not any((out/'read-coverage.bin').read_bytes()) and not any((out/'read-bytes.bin').read_bytes()) and not (out/'read-ledger.bin').read_bytes()
regs=struct.unpack('<16I',(out/'registers.bin').read_bytes());assert regs[14:]==(0xc0274874,0xc0278130)
result={'passed':True,'scope':'Second input fully copied, first PCM and all SRAM unchanged, third input request rejected before retirement. No PCM consumption.','inputBytes':len(actual),'inputSHA256':sha(actual),'queuedBytes':len(queue),'queueSHA256':sha(queue),'decodedBytes':348,'retiredAfterFirstPublication':9873362515-9873234585,'retiredAfterSecondRequest':9873362515-9873247421,'unchangedArtifacts':unchanged,'thirdInputCaptured':False,'thirdRequestPhysical':'14ae1000','thirdRequestWords':4096,'actualRegisters':[format(r,'08x') for r in regs]}
(out/'boundary-audit.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
