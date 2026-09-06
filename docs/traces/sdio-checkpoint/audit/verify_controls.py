"""Independent actual-guest, byte, IRQ and source audit for the bounded gate."""
from pathlib import Path
import hashlib,json,re
p=Path('.context/sdio-first/checkpoint/full-attempt-1')
def digest(path):
    h=hashlib.sha256()
    with open(path,'rb') as f:
        for block in iter(lambda:f.read(1048576),b''):h.update(block)
    return h.hexdigest()
def obj(name):return json.loads((p/name).read_text())
helper_wire=Path('.context/sdio-first/helper-plan/expected-helper-wire.bin').read_bytes()
helper=Path('.context/sdio-first/enable-plan/original-helper.bin').read_bytes()
protocol=Path('.context/sdio-first/helper-protocol')
header=(protocol/'expected-main-header-wire.bin').read_bytes()
body=(protocol/'expected-main-first-payload.bin').read_bytes()
copied=(protocol/'expected-main-first-copied.bin').read_bytes()
rx=re.compile(r'^(\d+) ticks=(\d+) pc=([0-9a-f]+) ([RW])4 ([0-9a-f]+)=([0-9a-f]+) accepted=(\d+)')
def commands(prefix, exact_observation=True):
    out=[];active=None;arg=None
    for line in (p/(prefix+'-mmio.log')).read_text().splitlines():
        m=rx.match(line)
        if not m:continue
        ins,ticks,pc,op,address,value,accepted=m.groups()
        ticks=int(ticks);address=int(address,16);value=int(value,16)
        assert accepted=='1'
        if op=='W' and address==0x38d0000c:arg=value
        if op=='W' and address==0x38d00008 and value&0x80000000:
            assert active is None
            active=dict(index=value&63,descriptor=value&0x7fffffff,argument=arg,
                        triggerTicks=ticks,response=[])
        if active is not None and op=='R' and address==0x38d00018 and value&0x10:
            elapsed=ticks-active['triggerTicks']
            assert elapsed>=600
            if exact_observation:assert elapsed==600
            active['observationElapsedTicks']=elapsed
            active['completeTicks']=ticks
        if active is not None and op=='W' and address==0x38d00014:
            assert value==0x15 and 'completeTicks' in active
            active['ackTicks']=ticks
        if active is not None and op=='R' and 0x38d00020<=address<=0x38d0002c:
            assert 'ackTicks' in active
            active['response'].append(value)
            if address==0x38d0002c:out.append(active);active=None
    assert active is None
    return out
runs={}
for prefix,n,stop in [('positive',39,'c038a8c8'),('main-negative',37,'c038a788')]:
    report=obj(prefix+'-report.json');data=obj(prefix+'-data.json');h=obj(prefix+'-helper.json')
    assert report['gateReached'] and report['pc']==stop
    assert report['cardErrorResponses']==report['rejectedWrites']==report['transportErrors']==0
    assert data['dataStarts']==data['dataCompletions']==data['dmaSourceLookups']==n
    assert data['dataErrors']==0
    assert h['helperBytes']==2140 and h['helperFrames']==37 and h['terminated']
    assert h['completionReturns']==n and h['allTransfersOK'] and h['wireOK'] and not h['protocolErrors']
    wire=helper_wire+(header+body if prefix=='positive' else b'')
    assert (p/(prefix+'-request-wire.bin')).read_bytes()==wire
    assert (p/(prefix+'-source-wire.bin')).read_bytes()==wire
    assert (p/(prefix+'-received-helper.bin')).read_bytes()==helper
    cmds=commands(prefix)
    assert len(cmds)==report['triggers']==report['completedResponses']
    transfers=[c for c in cmds if c['index']==53]
    expected_args=[0x9a000002]*36+[0x9a000001]
    if prefix=='positive':expected_args += [0x9a000001,0x9a000010]
    assert [c['argument'] for c in transfers]==expected_args
    assert all(c['descriptor']==0x501f5 and c['response']==[0x1000,0,0,0] for c in transfers)
    text=(p/(prefix+'-mmio.log')).read_text()
    returns=re.findall(r'genuine completeDMA consumed return pc=c0379684 r0=1 returns=(\d+).* good=1',text)
    assert [int(x) for x in returns]==list(range(1,n+1))
    assert text.count('pc=c0432130 W4 38d00038=00000001')==n
    assert text.count('pc=c0431c34 W4 38d00014=00000021')==n
    all_length_reads=[c for c in cmds if c['index']==52 and c['argument'] in (0x10002000,0x10002200)]
    # Original start path also reads this pair before the helper upload.
    # Partition at its actual terminator command, not by guessed total count.
    cutoff=transfers[36]['triggerTicks']
    initial_reads=[c for c in all_length_reads if c['triggerTicks']<cutoff]
    assert [c['argument'] for c in initial_reads]==[0x10002000,0x10002200]
    assert all(c['response']==[0x1000,0,0,0] for c in initial_reads)
    length_reads=[c for c in all_length_reads if c['triggerTicks']>cutoff]
    assert [c['argument'] for c in length_reads]==[0x10002000,0x10002200]*(3 if prefix=='positive' else 100)
    if prefix=='positive':
        assert [c['response'][0] for c in length_reads]==[0x1010,0x1000,0x1000,0x1002,0x1010,0x1000]
        assert (p/'positive-card-ram.bin').read_bytes()==copied+bytes(4)
        model=obj('positive-main-model.json')
        assert model['semanticModelOnly'] and model['headers']==model['bodies']==1
        assert model['copied']==508 and model['requested']==16 and model['errors']==0
        assert model['publications']==3 and model['mainCallReturns']==model['mainLengthPairs']==2
        assert model['mainSequenceOK'] and model['mainCallsOK']
        assert 'genuine next main request pair consumed pc=c038a8c8 offset=528 requested=16 pairs=2 good=1' in text
        assert len(wire)==2880
    else:
        assert all(c['response']==[0x1000,0,0,0] for c in length_reads)
        assert 'genuine main-loader no-requested-length failure pc=c038a788 polls=100' in text
    dt=bytearray(Path('web/firmware/devicetree_m68ap.bin').read_bytes())
    rem=obj(prefix+'-dt-removal.json');o=rem['recordOffset'];del dt[o:o+rem['removedRecordBytes']]
    dt[rem['nodeOffset']:rem['nodeOffset']+4]=rem['newPropertyCount'].to_bytes(4,'little')
    mac=obj(prefix+'-dt-property.json');o=mac['offset'];assert dt[o:o+6]==bytes(6)
    dt[o:o+6]=bytes.fromhex('020000000101')
    assert dt==(p/(prefix+'-loaded-dt.bin')).read_bytes()
    runs[prefix]=dict(report=report,data=data,helper=h,wireBytes=len(wire),
        wireSha256=digest(p/(prefix+'-source-wire.bin')),lengthReads=length_reads,
        completeDMAAcknowledgements=n,loadedDTSha256=digest(p/(prefix+'-loaded-dt.bin')))
inputs={}
for line in (p/'inputs-before.sha256').read_text().splitlines():
    expected,name=line.split(None,1);assert digest(name)==expected;inputs[name]=expected
sources={}
for line in (p/'sources-before.sha256').read_text().splitlines():
    expected,name=line.split(None,1);assert digest(name)==expected;sources[name]=expected
assert '1012343 total checks, 0 failures' in (p/'contracts.log').read_text()
out=dict(passed=True,scope='first modeled helper header/body consumed by original Apple host; no helper CPU or firmware-ready claim',
    checks=1012343,preservedChecks=549969,newChecks=462374,inputsUnchanged=inputs,
    sourceHashes=sources,runs=runs,model=obj('positive-main-model.json'),
    cardRamSha256=digest(p/'positive-card-ram.bin'),copied508Sha256=hashlib.sha256(copied).hexdigest(),
    requestCoherence='Actual three low/high pairs are16,512,16; independent command-completion samples, no atomic latch',
    dtAudit='Exact approved calibration record removal/count change and six-byte local MAC; no other input-file changes')
(p/'controls-verification.json').write_text(json.dumps(out,indent=2)+'\n')
print('PASS: original header/body wire, actual request16→512→16,508stored bytes/canaries,39IRQ/DMA acknowledgements, unchanged100-poll negative, source/input/DT audit')
