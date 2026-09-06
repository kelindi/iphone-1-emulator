from pathlib import Path
import json,hashlib
root=Path('.context/jit-stream-cache');checks=[]
for i in [1,2]:
 for label in ['C0','H1']:
  for kind in ['settings','home']:
   name=f'{label}-{kind}.rgba';a=(root/'references'/name).read_bytes();b=(root/'mechanism'/f'{i}-persistent'/name).read_bytes();assert len(a)==len(b)==320*480*4
   pixels=alpha=largest=0;bounds=[]
   for j in range(30*320*4,len(a),4):
    delta=max(abs(a[j+c]-b[j+c]) for c in range(3));largest=max(largest,delta);alpha+=a[j+3]!=b[j+3]
    if delta>2:pixels+=1;bounds.append((j//4%320,j//4//320))
   checks.append(dict(session=i,label=label,kind=kind,passed=pixels==0 and alpha==0,pixelsOver2=pixels,alphaDifferences=alpha,maxRGBDifference=largest,bounds=None if not bounds else [min(x for x,y in bounds),min(y for x,y in bounds),max(x for x,y in bounds),max(y for x,y in bounds)],referenceSha256=hashlib.sha256(a).hexdigest(),actualSha256=hashlib.sha256(b).hexdigest()))
report=dict(passed=all(x['passed'] for x in checks),method='Fixed independent original-production same-label C0/H1 reference, RGB<=2 below rawrow30, exactalpha; no tolerance adaptation. Manual review remains separate.',checks=checks,failed=[x for x in checks if not x['passed']])
(root/'mechanism/screen-comparison.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
