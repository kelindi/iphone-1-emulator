"""Publish ONLY an independently audited genuine gate with verified continuation."""
from pathlib import Path
import json,hashlib,struct,subprocess
import package
here=Path(__file__).resolve().parent
p=here/'full-attempt-1'
obj=lambda name:json.loads((p/name).read_text())
sha=lambda b:hashlib.sha256(b).hexdigest()
pins=json.loads((here/'source-pins.json').read_text())
for path,record in pins.items():
 data=Path(path).read_bytes()
 assert len(data)==record['size'] and sha(data)==record['sha256'],path
assert obj('verification.json')['passed'] and obj('root-byte-audit.json')['passed']
r=obj('full-continuation-comparison.json');gate=obj('full-checkpoint.json')
assert gate['normalRunExit'] and gate['pc']=='c0379684' and not gate['published']
assert r['passed'] and r['stopReached'] and r['pc']=='c038a8ec' and r['r4']==1000
assert 0<r['additionalInstructions']<=100000000 and r['instructionLimit']==100000000
assert r['commandCompletions']==4414 and r['dmaCompletions']==504 and r['mainPhase']==7 and r['requested']==0
assert r['architecturalStateComparisons']==5
# Binary trace parser independent of C observer; all transactions (not filtered polls).
a=(p/'full-continuation-original-transactions.bin').read_bytes();b=(p/'full-continuation-restored-transactions.bin').read_bytes()
assert a==b and len(a)%32==0 and len(a)//32==r['transactionsCompared']
events=list(struct.iter_unpack('<QQIIII',a));triggers=[];arguments=[];responses=[]
for cycles,ticks,pc,address,value,flags in events:
 assert gate['instructions']<=cycles<=r['totalInstructions'] and gate['ticks']<=ticks<=r['timerTicks']
 assert flags&256 and ((flags>>1)&127)==4
 if flags&1 and address==0x38d00008 and value&0x80000000:triggers.append(value)
 if flags&1 and address==0x38d0000c:arguments.append(value)
 if not(flags&1) and address==0x38d00020:responses.append(value)
assert triggers==[0x80050074]*1000
assert arguments==[0x10004000]*1000
assert responses==[0x1008]*1000
rows=[json.loads(line) for line in (p/'full-continuation-cache-counters.jsonl').read_text().splitlines()]
assert len(rows)==5 and rows[0]['initialRaw'] and rows[0]['original']==rows[0]['restored']
assert all(row['passed'] for row in rows)
for row in rows:row['restoredMinusOriginal']=[b-a for a,b in zip(row['original'],row['restored'])]
(p/'continuation-independent-audit.json').write_text(json.dumps({'passed':True,'transactions':len(events),'sha256':sha(a),'statusPolls':len(triggers),'returnedStatus':8,'cacheCounters':rows,'scope':'original warm machine versus fresh loaded machine; every SDIO transaction, each instruction PC/time/IRQ; complete serialized architecture/device at five prefixes, excluding only independently checksum-validated CPU cache diagnostics'},indent=2)+'\n')
m=(p/'full-machine.snap').read_bytes();s=(p/'full-sdio.side').read_bytes()
assert len(m)==gate['machineBytes'] and len(s)==gate['sidecarBytes']
def validate(m,s):
 result=subprocess.run([str(here/'validate')],input=struct.pack('<II',len(m),len(s))+m+s,capture_output=True,timeout=120)
 assert result.returncode==0,result.stderr.decode()
 with (p/'canonical-validation.log').open('ab') as f:f.write(result.stdout+result.stderr)
identity={path:record['sha256'] for path,record in pins.items()}
identity.update(format='standard-machine-v39/private-sdio-z1-v2',scope='private frozen069 handoff; no firmware ready or networking',pc=gate['pc'],instructions=str(gate['instructions']),ticks=str(gate['ticks']),loadedDeviceTreeSha256=sha((p/'full-loaded-dt.bin').read_bytes()))
# manifest-last publisher itself performs the canonical fresh validation again.
manifest=package.publish(p/'private-package',m,s,identity,validate)
mm,ss,reloaded=package.load(p/'private-package',identity,validate)
assert mm==m and ss==s and reloaded==manifest
(p/'publication-report.json').write_text(json.dumps({'passed':True,'manifest':'private-package/manifest.json','generation':manifest['generation'],'machineSha256':sha(m),'sidecarSha256':sha(s),'gate':gate,'continuation':r,'sourcePinsVerified':len(pins),'scope':'private checkpoint only; no production snapshot/model change or firmware readiness'},indent=2)+'\n')
print('PASS: genuine gate, two original-file audits, complete continuation traces, fresh canonical load, manifest-last publication and actual package reload')
