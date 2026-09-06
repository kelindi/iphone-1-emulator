from pathlib import Path
import json,struct
paths=sorted(Path('.context/save-worker/quiet').glob('*/*.png'));paths=[p for p in paths if p.parent.name in ['1-baseline','2-worker','3-worker','4-baseline']]
w,h=struct.unpack('>II',paths[0].read_bytes()[16:24]);raw=Path('.context/save-worker/quiet/screens.rgba').read_bytes();assert len(raw)==len(paths)*w*h*4,(len(raw),len(paths),w,h)
frames=[memoryview(raw)[i*w*h*4:(i+1)*w*h*4] for i in range(len(paths))];reference={};results=[]
for p,f in zip(paths,frames):
 kind=p.stem.split('-')[1];reference.setdefault(kind,(p,f));ref,r=reference[kind];maximum=0;bad=[]
 for y in range((30*h+479)//480,h):
  for x in range(w):
   i=(y*w+x)*4;d=max(abs(f[i+c]-r[i+c]) for c in range(3));maximum=max(maximum,d)
   if d>3:bad.append((x,y,d))
 results.append({'file':str(p),'reference':str(ref),'width':w,'height':h,'maxRGBDelta':maximum,'pixelsOver3':len(bad),'boundsOver3':None if not bad else [min(t[0] for t in bad),min(t[1] for t in bad),max(t[0] for t in bad),max(t[1] for t in bad)],'passed':not bad})
report={'method':'All16 original screenshot stages manually reviewed in contact sheet: genuine complete Settings and complete16-icon Home+dock. Compare every actual screenshot against first-baseline matching-kind reference below scaled guest status bar, unchanged RGB<=3 tolerance. No pixels silently omitted except predefined status bar.','results':results,'passed':all(r['passed'] for r in results)}
Path('.context/save-worker/quiet/review.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
