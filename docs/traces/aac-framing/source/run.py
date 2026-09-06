"""Run only after a coordinated CPU window release. Preserve every mode output."""
from pathlib import Path
import hashlib, json, subprocess, time, sys

root=Path(__file__).resolve().parent
sha=lambda b:hashlib.sha256(b).hexdigest()
out=root / (sys.argv[1] if len(sys.argv)>1 else 'run-1')
out.mkdir(exist_ok=False)
fixture=root/'fixture'
source=json.loads((fixture/'source.json').read_text())
report={'scope':'Standalone raw AAC decoder feasibility; original file ASC is test configuration, not device input. No MMIO, ready, IRQ, guest continuation or audible playback.',
        'source':source,'limits':{'addressSpaceMiB':256,'cpuSecondsPerProcess':120,'wallSecondsPerProcess':180,'queueBytes':65536,'maxStreamingAttempts':2200},
        'binarySHA256':sha((root/'probe').read_bytes()),'probeSourceSHA256':sha((root/'probe.c').read_bytes()),'modes':[],'pass':False}
def save(): (out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
save()
for mode in ('packetized','raw','cut','truncate'):
    data=fixture/('packet50.aac' if mode=='truncate' else 'stream.aac')
    cmd=[str(root/'probe'),mode,str(data),str(fixture/'config.asc'),str(out/(mode+'.pcm')),str(out/(mode+'.csv'))]
    if mode=='packetized':cmd.append(str(fixture/'sizes.oracle'))
    start=time.monotonic()
    with (out/(mode+'.log')).open('w') as log:
        result=subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,timeout=185)
    record={'mode':mode,'command':cmd,'returncode':result.returncode,'wallSeconds':time.monotonic()-start}
    report['modes'].append(record);save()
    if result.returncode:print((out/(mode+'.log')).read_text());sys.exit(result.returncode)

rows={}
for mode in ('packetized','raw','cut'):
    text=(out/(mode+'.csv')).read_text().splitlines()
    rows[mode]=[list(map(int,line.split(','))) for line in text[1:]]
    assert len(rows[mode])==1069
    cursor=0
    for row,size in zip(rows[mode],source['sizes']):
        assert row[1]==cursor and row[2]==size
        assert row[4:]==[48000,2,2,0,0]
        cursor+=size
    assert cursor==source['bytes']
assert rows['packetized']==rows['raw']==rows['cut']
control=(out/'packetized.pcm').read_bytes()
assert control==(out/'raw.pcm').read_bytes()==(out/'cut.pcm').read_bytes()
cutlog=(out/'cut.log').read_text()
assert 'UNRESOLVED offset16065 available319' in cutlog
assert 'committed49' in cutlog and 'CONTINUATION boundary16436 committed50' in cutlog
trunc=[list(map(int,line.split(','))) for line in (out/'truncate.csv').read_text().splitlines()[1:]]
assert len(trunc)==372 and all(row[1]==0 for row in trunc[:-1]) and trunc[-1][1]==371
report.update({'pass':True,'all1069FrameBoundariesMatchIndependentPacketOracle':True,'rawAndCutPCMMatchPacketizedControl':True,
 'pcmBytes':len(control),'pcmSHA256':sha(control),'pcmFrames':len(control)//4,
 'firstFrameSamples':rows['raw'][0][3],'partialFrame50CommittedBefore52ByteAppend':False,
 'all371TruncatedPrefixesRejected':True,'truncationProbeCount':len(trunc),
 'artifacts':{p.name:sha(p.read_bytes()) for p in out.iterdir() if p.name!='report.json'}})
save();print(json.dumps({k:v for k,v in report.items() if k not in ('source','modes','artifacts')},indent=2))
