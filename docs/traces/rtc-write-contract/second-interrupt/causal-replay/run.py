from pathlib import Path
import hashlib,json,subprocess,resource,time
base=Path('.context/rtc-second')
build=json.loads((base/'build-report.json').read_text());assert build['passed']
for name,pin in build['pins'].items():
    p=Path(name);assert p.stat().st_size==pin['bytes']
    assert hashlib.sha256(p.read_bytes()).hexdigest()==pin['sha256'],name
out=base/'run-1';out.mkdir(exist_ok=False)
cmd=[str(base/'probe'),'.context/rtc-contract/browser-2/before-manual-change.snap']
report={'command':cmd,'instructionLimit':400000000,'wallLimit':60,'cpuLimit':50,'traceByteLimit':4194304,'automaticRetry':False}
def limits():
    resource.setrlimit(resource.RLIMIT_CPU,(50,50))
    resource.setrlimit(resource.RLIMIT_FSIZE,(4194304,4194304))
    resource.setrlimit(resource.RLIMIT_CORE,(0,0))
start=time.monotonic()
with (out/'trace.log').open('w') as log:
    try:
        r=subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,timeout=60,preexec_fn=limits)
        report.update(exitCode=r.returncode,timedOut=False)
    except subprocess.TimeoutExpired:
        report.update(exitCode=None,timedOut=True)
report['wallSeconds']=time.monotonic()-start
report['launcherSHA256']=hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
(out/'run.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report))
