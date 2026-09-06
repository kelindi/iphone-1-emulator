"""Four fixed-order fresh browser traces; execute only in an agreed quiet window."""
from pathlib import Path
import hashlib
import json
import os
import signal
import subprocess
import time

here=Path(__file__).resolve().parent
repo=here.parents[1]
out=here/'quiet-first-use'
out.mkdir(exist_ok=False)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
paths=[here/'first-use.mjs',here/'runtime/emu.js',here/'runtime/emu.wasm',
       repo/'.context/first-use/app.js',repo/'.context/first-use/emu-worker.js',
       repo/'docs/traces/browser-first-use/cdp.mjs']
for variant in ['original','candidate']:
    paths.extend(here/variant/name for name in ['seed.wasm','regions.json','abi.json'])
pins={str(p.relative_to(repo)):sha(p) for p in paths}
assert pins[str((here/'runtime/emu.wasm').relative_to(repo))]=='b3237ef3843412aea164d3db111299650ea8433bf16311417116799959ab6606'
report={'passed':False,'start':time.time(),'order':['original','candidate','candidate','original'],
        'sourcePins':pins,'runs':[],
        'method':'Four fresh profiles, diagnostic C0/H1/C1 protocol, fixed original runtime, variant seed only; video/profiling in every arm. No20-run p95 claim.'}
def save(): (out/'comparison.json').write_text(json.dumps(report,indent=2)+'\n')
save()
for index,variant in enumerate(report['order'],1):
    for name,expected in pins.items(): assert sha(repo/name)==expected,name
    folder=out/f'{index}-{variant}'
    folder.mkdir()
    env=dict(os.environ,SEED_VARIANT=variant,ARTIFACT_DIR=str(folder))
    started=time.time()
    with (folder/'run.log').open('w') as log:
        child=subprocess.Popen(['node',str(here/'first-use.mjs')],cwd=repo,env=env,
                               stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
        try: code=child.wait(timeout=240)
        except subprocess.TimeoutExpired:
            os.killpg(child.pid,signal.SIGKILL)
            code=child.wait()
            report['failure']='240-second browser arm wall cap'
    row={'index':index,'variant':variant,'exitCode':code,'elapsedSeconds':time.time()-started}
    if (folder/'report.json').exists(): row['report']=json.loads((folder/'report.json').read_text())
    report['runs'].append(row);save()
    assert code==0 and row.get('report',{}).get('passed') and not row['report']['errors'],row
    assert row['report']['traceComplete']['dataLossOccurred'] is False
    print(index,variant,'complete',flush=True)
report['passed']=True;report['end']=time.time();save()
