"""Require exact prior guest path/state, plus explicit actual instruction receipt."""
from pathlib import Path
import runpy,json,hashlib,re,sys
out=Path(sys.argv[1]);baseline=Path('.context/amc-first/stage-p/run-1');trace=(out/'trace.log').read_text();old=(baseline/'trace.log').read_text()
clean=lambda text:''.join(x for x in text.splitlines(True) if not x.startswith(('RELEASE STEP ','DESTINATION ')))
assert clean(trace.split('RELEASE RETIRED',1)[0])==old.split('STOP release store not retired',1)[0], 'Stage P through owner-store trace differs'
names=('sram.bin','expected.bin','predicted.bin','written.bin','init-written.bin','pcm-written.bin','host-written.bin','owned-queue.bin','registers.bin','first-pcm.bin','second-pcm.bin','read-coverage.bin','read-bytes.bin','read-ledger.bin','ordering.bin','dma-copies.bin','dma-bytes.bin')
for name in names:assert (out/name).read_bytes()==(baseline/name).read_bytes(), 'Stage P final state differs: '+name
m=re.search(r'CONSUMPTION .* releaseCycles(\d+) retiredCycles(\d+) releasePC([0-9a-f]+) releaseCPSR([0-9a-f]+)',trace);assert m
callback,end=map(int,m.groups()[:2]);pc,cpsr=map(lambda x:int(x,16),m.groups()[2:]);a=runpy.run_path(str(Path(__file__).with_name('audit.py')))
assert a['audit_step'](trace,callback,end,pc,cpsr)
report={'passed':True,'scope':'Exact Stage P path and17 final artifacts, with new explicit instruction-start/callback/returned-successor evidence. No work after ownership return.','callbackCycles':callback,'completedCycles':end,'pc':hex(pc),'artifactSHA256':{n:hashlib.sha256((out/n).read_bytes()).hexdigest() for n in names}}
(out/'retirement-audit.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
