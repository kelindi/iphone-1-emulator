"""Archive the failed/incomplete genuine date-picker diagnostic as observed."""
from pathlib import Path
import hashlib,json,shutil

base=Path('.context/rtc-contract');out=Path('docs/traces/rtc-write-contract/browser-diagnostic');out.mkdir(parents=True,exist_ok=True)
for name in ['inspect.c','inspect-build.log','inspect-build-2.log','pmu-snapshots.jsonl','archive.py']:
    shutil.copyfile(base/name,out/name)
shutil.copyfile('web/tests/organizer.mjs',out/'organizer.mjs')
pins={}
for run in ['browser-1','browser-2']:
    dest=out/run;dest.mkdir(exist_ok=True)
    for source in (base/run).iterdir():
        if source.suffix in ('.png','.json'):
            shutil.copyfile(source,dest/source.name)
        if source.is_file():
            pins[str(source)]={'bytes':source.stat().st_size,'sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'copied':source.suffix in ('.png','.json')}
report=json.loads((base/'browser-2/report.json').read_text())
assert report['passed'] is False and len(report['errors'])==1
assert 'verifyJit' in report['errors'][0]
pmu=[json.loads(s) for s in (base/'pmu-snapshots.jsonl').read_text().splitlines()]
assert len(pmu)==2 and all(r['unknownWrites']==0 for r in pmu)
review={'acceptedWorkflow':False,'method':'Manual review of actual original Settings screens and native read-only decoding of both raw exports. Browser reload image gate samples18000RGB pixels; its equality does not override failed final generated-execution gate.','observations':['Set Automatically can be switched off and the original manual picker appears.','The minute wheel visibly changes from13to14 after a genuine tap; committing a hardware clock change is unverified.','Final generated-execution check times out with restored phone left paused. All original errors/screens/recording are retained.','Both actual exports report0unsupported PMU writes; registerWrites222to231 alone cannot identify any RTC transaction.','No native guest instructions ran during export inspection.','First launch used noninteractive closed stdin and collected Home only; separate interactive discovery is browser2.','Initial native inspector assumed128MiB and failed geometry; actual export is256MiB. Corrected inspector uses exact fixture geometry and uint64counter formats.'],
 'pmu':pmu,'nativeArchiveSHA256':hashlib.sha256(Path('.context/build-native/core/libemucore.a').read_bytes()).hexdigest(),'originalError':report['errors'][0],
 'video':'../../../../tweets/videos/manual_time_diagnostic.mp4','thumbnail':'../../../../tweets/thumbnails/manual_time_diagnostic.jpg'}
(out/'review.json').write_text(json.dumps(review,indent=2)+'\n');(out/'artifact-pins.json').write_text(json.dumps(pins,indent=2)+'\n')
print(json.dumps({'acceptedWorkflow':False,'filesPinned':len(pins),'unknownWrites':[r['unknownWrites'] for r in pmu]}))
