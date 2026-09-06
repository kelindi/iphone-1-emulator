"""Preserve completed comparison without modifying its original reports."""
from pathlib import Path
import gzip, hashlib, json, shutil

base=Path('.context/jit-lazy-groups')
source=base/'quiet-first-use'
dest=Path('docs/traces/jit-lazy-groups/quiet-first-use')
dest.mkdir(exist_ok=True)
report=json.loads((source/'comparison.json').read_text())
assert report['passed'] and len(report['runs'])==4
pins={}
def copy(src, dst):
    dst.parent.mkdir(parents=True,exist_ok=True)
    shutil.copyfile(src,dst)
    pins[str(dst.relative_to(dest))]={'bytes':dst.stat().st_size,'sha256':hashlib.sha256(dst.read_bytes()).hexdigest()}
for name in ('comparison.json','screen-comparison.json','contact-sheet.png'):
    copy(source/name,dest/name)
videos=[]
rows=[]
for run in report['runs']:
    label=f"{run['index']}-{run['variant']}"
    directory=source/label
    for src in directory.rglob('*'):
        if not src.is_file() or 'video' in src.parts:continue
        target=dest/src.relative_to(source)
        if src.name=='trace.json':
            target=target.with_suffix('.json.gz')
            target.parent.mkdir(parents=True,exist_ok=True)
            with target.open('wb') as out:
                with gzip.GzipFile(filename='',fileobj=out,mode='wb',mtime=0) as gz:gz.write(src.read_bytes())
            pins[str(target.relative_to(dest))]={'bytes':target.stat().st_size,'sha256':hashlib.sha256(target.read_bytes()).hexdigest(),'uncompressedSha256':hashlib.sha256(src.read_bytes()).hexdigest()}
        else:copy(src,target)
    r=json.loads((directory/'report.json').read_text())
    video=Path(r['video'])
    videos.append({'run':label,'localPath':str(video),'bytes':video.stat().st_size,'sha256':hashlib.sha256(video.read_bytes()).hexdigest()})
    analysis=json.loads((directory/'analysis.json').read_text())
    rows.append({'run':label,'stages':[{k:s[k] for k in ('label','readyMs','retired','generated','calls','compileThreadCpuLowerMs','compileThreadCpuUpperMs','compileGlobalWallUnionMs')} for s in analysis['stages']]})
(dest/'artifact-pins.json').write_text(json.dumps({'files':pins,'localOriginalVideos':videos},indent=2)+'\n')
(dest/'decision.json').write_text(json.dumps({'decision':'parked; no production promotion','method':'Four fresh browser profiles, original/candidate/candidate/original. Diagnostic C0 first use, H1 repeat, C1 same-worker guest restore. Two observations per configuration/stage, not p95 or a release gate.','screensPassed':24,'rootManualReview':'All 24 screens show correct Settings or Home content. Contact sheet rows are run1/2/3/4; columns C0 Settings/Home, H1 Settings/Home, C1 Settings/Home.','reason':'Cold Settings regresses in both paired directions; warm Settings also regresses. Smaller functions do not lower observed seed baseline compilation CPU.','runs':rows},indent=2)+'\n')
# Keep the original pre-browser identity file as historical evidence.
old=dest.parent/'local-oracle-pins.json'
historical=dest.parent/'pre-browser-local-oracle-pins.json'
if not historical.exists():shutil.copyfile(old,historical)
names=list(json.loads(old.read_text()))
names+=['run-first-use.py','review-first-use.py','bind-lazy.py','archive-first-use.py']
current={n:{'size':(base/n).stat().st_size,'sha256':hashlib.sha256((base/n).read_bytes()).hexdigest()} for n in names}
old.write_text(json.dumps(current,indent=2)+'\n')
for n in names:
    if n.endswith(('.py','.mjs','.cjs','.c')):shutil.copyfile(base/n,dest.parent/'source'/n)
print(json.dumps({'archivedFiles':len(pins),'runs':len(rows),'screens':24,'decision':'parked'}))
