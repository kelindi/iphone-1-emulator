"""Independent actual second request audit, valid even if later continuation stops."""
from pathlib import Path
import hashlib,json,runpy,sys
out=Path(sys.argv[1]);old=Path('.context/amc-first/stage-i/run-1');trace=''.join(line for line in (out/'trace.log').read_text().splitlines(True) if not line.startswith('ORDER '));baseline=(old/'trace.log').read_text()
assert json.loads((out/'first/functional-audit.json').read_text())['passed']
assert trace.count('ACCEPT second request')==1
assert trace.split('ACCEPT second request',1)[0]==baseline.split('STOP unexpected first DMA request',1)[0], 'preadmission trace differs'
files={};sha=lambda data:hashlib.sha256(data).hexdigest()
for name in ('sram.bin','written.bin','init-written.bin','pcm-written.bin','host-written.bin','owned-queue.bin','first-pcm.bin','registers.bin','read-coverage.bin','read-bytes.bin','read-ledger.bin'):
 data=(out/'second-request'/name).read_bytes();assert data==(old/name).read_bytes(), 'preadmission state differs: '+name;files[name]=sha(data)
actual=(out/'second-input.bin').read_bytes();assert actual==(old/'second-input.bin').read_bytes(), 'actual second input differs'
oracle=runpy.run_path(str(Path(__file__).with_name('mp4_oracle.py')))
assert len(actual)==16384 and actual==oracle['stream'][16384:32768]==oracle['media'][67624:84008]
assert 'ACCEPT second request physical14ac1000 words4096 cycles9873247421; actual live RAM DMA only' in trace
report={'passed':True,'scope':'Exact Stage I request/preadmission trace and state, actual original second input, accepted through unchanged engine. Later consumption independently gated.','fileOffset':67624,'streamOffset':16384,'bytes':len(actual),'inputSHA256':sha(actual),'preadmissionArtifacts':files}
(out/'second-request-audit.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
