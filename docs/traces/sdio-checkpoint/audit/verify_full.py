"""Actual full-container audit; reuse only command parsing from control verifier."""
from pathlib import Path
import re,json,hashlib,collections
p=Path('.context/sdio-first/checkpoint/full-attempt-1');plan=Path('.context/sdio-first/main-full-plan')
# Definitions only: do not execute controls again or import semantic code.
namespace={}
exec(Path('.context/sdio-first/checkpoint/audit/verify_controls.py').read_text().split('runs={}')[0],namespace)
commands=namespace['commands'];digest=namespace['digest'];obj=namespace['obj']
oracle=json.loads((plan/'source-oracle.json').read_text())
independent=json.loads((plan/'root-independent-oracle.json').read_text())
assert independent['passed']
report=obj('full-report.json');data=obj('full-data.json');h=obj('full-helper.json')
model=obj('full-main-model.json');budget=obj('full-budget.json')
assert report['gateReached'] and report['pc']=='c0379684'
assert report['triggers']==report['completedResponses']==3414
assert report['cardErrorResponses']==report['transportErrors']==report['rejectedWrites']==0
assert data['dataStarts']==data['dataCompletions']==data['dmaSourceLookups']==504
assert data['dataErrors']==0 and data['irqPending']==0
assert h['helperBytes']==2140 and h['helperFrames']==37 and h['terminated']
assert h['completionReturns']==504 and h['sourceWireBytes']==128704
assert h['wireOK'] and h['allTransfersOK'] and h['protocolErrors']==0
assert model['semanticModelOnly'] and model['phase']==7 and model['requested']==0
assert model['headers']==234 and model['bodies']==233 and model['copied']==117932
assert model['errors']==0 and model['publications']==468
assert model['mainCallReturns']==model['mainLengthPairs']==466
assert model['mainSequenceOK'] and model['mainCallsOK']
assert not budget['exhausted'] and budget['instructionLimit']==22000000000 and budget['wallLimitSeconds']==900
assert report['instructions']<22000000000 and budget['wallElapsedSeconds']<900
helper_wire=Path('.context/sdio-first/helper-plan/expected-helper-wire.bin').read_bytes()
main_wire=(plan/'expected-main-wire.bin').read_bytes()
source=(p/'full-source-wire.bin').read_bytes();requests=(p/'full-request-wire.bin').read_bytes()
assert source==requests==helper_wire+main_wire
assert len(source)==128704 and len(main_wire)==126368
assert (p/'full-received-helper.bin').read_bytes()==Path('.context/sdio-first/enable-plan/original-helper.bin').read_bytes()
assert (p/'full-handoff-frame.bin').read_bytes()==main_wire[-32:]
assert main_wire[-32:-16]==bytes.fromhex('0400000000000000000000001fdb8c18')
banks={}
for name in ['low','high']:
    actual=(p/('full-'+name+'.bin')).read_bytes()
    mask=(p/('full-'+name+'-mask.bin')).read_bytes()
    expected=(plan/('expected-'+name+'.bin')).read_bytes()
    expected_mask=(plan/('expected-'+name+'-mask.bin')).read_bytes()
    assert actual==expected and mask==expected_mask
    assert all(x in (0,1) for x in mask)
    assert all(actual[i]==0 for i,v in enumerate(mask) if not v)
    for side in ['before','after']:
        assert (p/('full-'+name+'-'+side+'.bin')).read_bytes()==bytes([0xa5])*16
    banks[name]=dict(bytes=len(actual),written=sum(mask),guardBytes=len(mask)-sum(mask),
                     sha256=hashlib.sha256(actual).hexdigest(),coverageSha256=hashlib.sha256(mask).hexdigest())
assert sum(b['written'] for b in banks.values())==117932
assert sum(b['guardBytes'] for b in banks.values())==852
cmds=commands('full',exact_observation=False);assert len(cmds)==3414
observations=collections.Counter(c['observationElapsedTicks'] for c in cmds)
assert observations=={600:3413,616:1} # exact actual-run audit, not a silicon latency claim
transfers=[c for c in cmds if c['index']==53]
expected_args=[0x9a000002]*36+[0x9a000001]+[0x9a000000|f['physical']//32 for f in oracle['frames']]
assert len(expected_args)==504 and [c['argument'] for c in transfers]==expected_args
assert all(c['descriptor']==0x501f5 and c['response']==[0x1000,0,0,0] for c in transfers)
length_reads=[c for c in cmds if c['index']==52 and c['argument'] in (0x10002000,0x10002200)]
before=[c for c in length_reads if c['triggerTicks']<transfers[36]['triggerTicks']]
assert [c['argument'] for c in before]==[0x10002000,0x10002200]
assert all(c['response']==[0x1000,0,0,0] for c in before)
after=[c for c in length_reads if c['triggerTicks']>transfers[36]['triggerTicks']]
expected_lengths=[16]
for record in oracle['records']:
    if record['command']==4:break
    expected_lengths.extend([record['logicalBody'],16])
assert len(expected_lengths)==467
assert [c['argument'] for c in after]==[0x10002000,0x10002200]*467
actual_lengths=[]
for i,expected in enumerate(expected_lengths):
    lo,hi=after[2*i:2*i+2]
    assert lo['response']==[0x1000|(expected&255),0,0,0]
    assert hi['response']==[0x1000|(expected>>8),0,0,0]
    actual_lengths.append((lo['response'][0]&255)|((hi['response'][0]&255)<<8))
assert actual_lengths==expected_lengths
text=(p/'full-mmio.log').read_text()
returns=re.findall(r'genuine completeDMA consumed return pc=c0379684 r0=1 returns=(\d+).* good=1',text)
assert [int(x) for x in returns]==list(range(1,505))
assert text.count('pc=c0432130 W4 38d00038=00000001')==504
assert text.count('pc=c0431c34 W4 38d00014=00000021')==504
assert 'full main container handoff request consumed pc=c0379684 gate=1 headers=234 bodies=233 copied=117932 entry=00000000 finalCompleteDMAReturn=1' in text
# Reconstruct only the exact previously approved loaded-DT changes.
dt=bytearray(Path('web/firmware/devicetree_m68ap.bin').read_bytes())
rem=obj('full-dt-removal.json');off=rem['recordOffset'];del dt[off:off+rem['removedRecordBytes']]
dt[rem['nodeOffset']:rem['nodeOffset']+4]=rem['newPropertyCount'].to_bytes(4,'little')
mac=obj('full-dt-property.json');off=mac['offset'];assert dt[off:off+6]==bytes(6);dt[off:off+6]=bytes.fromhex('020000000101')
assert dt==(p/'full-loaded-dt.bin').read_bytes()
inputs={};sources={}
for filename,destination in [('inputs-before.sha256',inputs),('sources-before.sha256',sources)]:
    for line in (p/filename).read_text().splitlines():
        expected,name=line.split(None,1);assert digest(name)==expected;destination[name]=expected
assert '1012343 total checks, 0 failures' in Path('.context/sdio-first/checkpoint/retained-contracts.log').read_text()
controls=json.loads(Path('.context/sdio-first/main-full/controls-verification.json').read_text());assert controls['passed']
out=dict(passed=True,scope='Original complete main container received/stored; final command4 physical completion acknowledged; no helper/main CPU execution or firmware readiness',
    report=report,data=data,helper=h,model=model,budget=budget,banks=banks,
    wireBytes=len(source),mainWireBytes=len(main_wire),wireSha256=hashlib.sha256(source).hexdigest(),
    mainWireSha256=hashlib.sha256(main_wire).hexdigest(),checks=1012343,preservedChecks=549969,newChecks=462374,
    commandResponses=len(cmds),completeDMAAcknowledgements=504,coherentHostLengthPairs=467,
    commandCompletionObservations=dict(observations),
    delayedObservationScope='600tick modeled deadline; one first-observed completion at616ticks. No early observation; exact cause of16tick delay not established by suppressed-poll trace.',
    hostReadZeroAfterHandoff=False,originalMainCallReturnsBeforeFinalIRQ=466,
    inputsUnchanged=inputs,sourceHashes=sources,controlsPassed=True,
    dtAudit='Only approved missing calibration property/count and local-MAC bytes in private loaded copy')
(p/'verification.json').write_text(json.dumps(out,indent=2)+'\n')
print('PASS: full original wire/banks/write coverage/guards,504 real IRQ/DMA acknowledgements,467coherent host pairs, final recordedhandoff, budgets/controls/input/source audit')
