"""Strict raw guest pixel comparisons. No resize, tolerance adaptation or normalization."""
from pathlib import Path
import hashlib
import json

root=Path(__file__).resolve().parent/'quiet-first-use'
report=json.loads((root/'comparison.json').read_text())
assert report['passed'] and len(report['runs'])==4
rows=[]
def compare(a,b):
    assert len(a)==len(b)==320*480*4
    pixels=channels=alpha=largest=0
    for i in range(30*320*4,len(a),4):
        delta=[abs(a[i+c]-b[i+c]) for c in range(3)]
        largest=max(largest,*delta)
        pixels+=max(delta)>2
        channels+=sum(d>2 for d in delta)
        alpha+=a[i+3]!=b[i+3]
    return {'passed':pixels==0 and alpha==0,'pixelsCompared':320*450,
            'pixelsOver2':pixels,'channelsOver2':channels,'maxRGBDifference':largest,
            'alphaDifferences':alpha,'actualSha256':hashlib.sha256(b[30*320*4:]).hexdigest()}
for run in report['runs']:
    folder=root/f"{run['index']}-{run['variant']}"
    for label in ['C0','H1','C1']:
        for kind in ['settings','home']:
            name=f'{label}-{kind}.rgba'
            result=compare((root/'1-original'/name).read_bytes(),(folder/name).read_bytes())
            rows.append({'run':run['index'],'variant':run['variant'],'label':label,'kind':kind,**result})
# Cold and warm UI may differ at the fading scrollbar. Retain this stricter
# diagnostic separately rather than substitute the cold screen for H1's
# matched original-state control or claim all six stages are visually equal.
within=[]
for run in report['runs']:
    folder=root/f"{run['index']}-{run['variant']}"
    for label in ['H1','C1']:
        for kind in ['settings','home']:
            within.append({'run':run['index'],'label':label,'kind':kind,
                **compare((folder/f'C0-{kind}.rgba').read_bytes(),(folder/f'{label}-{kind}.rgba').read_bytes())})
out={'method':'Every variant/stage versus the same named C0/H1/C1 stage in original run1, <=2 RGB below guest row30, exact alpha; same-run cold/warm comparison separately retained. No threshold widening.',
     'passed':all(r['passed'] for r in rows),'checks':rows,
     'failed':[r for r in rows if not r['passed']],
     'sameRunColdComparisons':within}
(root/'screen-comparison.json').write_text(json.dumps(out,indent=2)+'\n')
print(json.dumps({'passed':out['passed'],'screens':len(rows),'failed':len(out['failed'])}))
