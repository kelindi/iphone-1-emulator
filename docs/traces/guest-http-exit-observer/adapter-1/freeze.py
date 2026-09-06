from pathlib import Path
import hashlib,json,shutil
B=Path('.context/guest-http-default/exit-adapter-1')
def sha(p):
 h=hashlib.sha256()
 with Path(p).open('rb')as f:
  for b in iter(lambda:f.read(1048576),b''):h.update(b)
 return h.hexdigest()
assert 'PASS 461 ' in (B/'test.log').read_text()
assert 'PASS 20 ' in (B/'ui-test.log').read_text()
p=json.loads((B/'preflight-3/preflight.json').read_text());assert p['sourceBindings'] and p['initialFrameMAEMilli']==0 and p['cyclesBefore']==p['cyclesAfter']==9197000000
assert 'svc-before-binding' in (B/'before-shared-process-filter/negative.log').read_text()
assert json.loads((B/'geometry-audit.json').read_text())['fullCroppedBrowserRGBBytesExact']==460800
fixed={
 '.context/build-rtc-release/core/libemucore.a':'0ff5d8769671915486d609135394fa5b5ccb9e03b40717adb9e643676ddd75ec',
 '.context/safari-default-mbx/baseline.c':'37f384c1d9715aa745c9d2b8b8aef71343e8c486c8675b71aa37704e3e8c9589',
 '.context/guest-http-default/native-run-1/listener.snap.gz':'b9d40b5ed9942b7d21237ebb6e9ca5867bceb5c37cabe6529f8946d5fb37880f',
 **json.loads((B/'code-pins.json').read_text())['inputs']}
assert all(sha(k)==v for k,v in fixed.items())
local=[p for p in B.iterdir()if p.is_file()and p.suffix in ['.c','.h','.py','.json','.md'] and p.name not in ['pins.json','check-report.json']]
local += list((B/'references').glob('*.rgb'))+[B/'runner',B/'test_adapter',B/'test_ui',B.parent/'test_snapshot_transport']
for name in ['core/include/soc.h','core/include/arm.h','core/include/board.h','core/include/snapshot.h']:local.append(Path(name))
files={**fixed,**{str(p):sha(p)for p in local}}
(B/'pins.json').write_text(json.dumps({'scope':'Frozen isolated native exit diagnostic; zero actual continuation instructions at preparation','files':files},indent=2)+'\n')
report={'passed':True,'guestContinuationExecuted':False,'newTouchInput':False,'adapterChecks':461,'uiChecks':20,'oldAdapterForeignProcessNegative':'fails svc-before-binding, retained','preflight':p,'geometry':json.loads((B/'geometry-audit.json').read_text()),'pinsSHA256':sha(B/'pins.json'),'runnerSHA256':sha(B/'runner'),'requiresParentReviewAndSchedule':True,'availableDiskBytes':shutil.disk_usage(B).free}
(B/'check-report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
