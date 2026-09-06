from pathlib import Path
import subprocess,json,hashlib,struct
root=Path.cwd();folder=root/'.context/jit-continuation/browser/quiet-navigation';ff=root/'node_modules/ffmpeg-static/ffmpeg';comparison=json.loads((folder/'comparison.json').read_text())
cache={}
def pixels(path):
 if path not in cache:
  width,height=struct.unpack_from('>II',path.read_bytes(),16)
  raw=subprocess.check_output([str(ff),'-loglevel','error','-i',str(path),'-vf','crop=iw:ih-20:0:20','-frames:v','1','-f','rawvideo','-pix_fmt','rgb24','pipe:1'])
  assert len(raw)==width*(height-20)*3
  cache[path]=(raw,width,height)
 return cache[path]
def diff(actual,reference):
 a,w,h=pixels(actual);b,rw,rh=pixels(reference);assert (w,h)==(rw,rh)
 different=over1=over3=largest=0;xs=[];ys=[]
 for i,(x,y) in enumerate(zip(a,b)):
  delta=abs(x-y);different+=delta!=0;over1+=delta>1;largest=max(largest,delta)
  if delta>3:over3+=1;xs.append((i//3)%w);ys.append((i//3)//w+20)
 return {'rgbPixels':len(a)//3,'differentChannels':different,'channelsDifferentByMoreThan1':over1,'channelsDifferentByMoreThan3':over3,'largestDifference':largest,'sha256':hashlib.sha256(a).hexdigest(),'referenceSha256':hashlib.sha256(b).hexdigest(),'overToleranceBounds':None if not xs else [min(xs),min(ys),max(xs),max(ys)],'passed':over3==0}
checks=[];baselineChecks=[]
for run in comparison['runs']:
 current=folder/f"{run['index']}-{run['arm']}"
 for kind in ['settings','home']:
  for round in range(11):
   image=current/f'{round}-{kind}.png';meta={'run':run['index'],'arm':run['arm'],'kind':kind,'round':round,'screenshot':str(image.relative_to(folder))}
   checks.append(dict(meta,**diff(image,current/f'0-{kind}.png')))
   baselineChecks.append(dict(meta,**diff(image,folder/'1-baseline'/f'0-{kind}.png')))
assert len(checks)==len(baselineChecks)==88
result={'method':'All88 displayed screenshots compared both with their same-session first reference and with frozen baseline run1 first Settings/Home references. Below20CSSpixels only, maximum RGB channel delta3, unchanged055 tolerance. Explicit failures retained; human review of8first references separate.','strictToleranceMaxRGBDelta':3,'excludedTopCssPixels':20,'checks':checks,'baselineChecks':baselineChecks,'sameRunFailures':[r for r in checks if not r['passed']],'strictFailures':[r for r in baselineChecks if not r['passed']]}
result['passed']=not result['strictFailures'];(folder/'screen-comparison.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({'screens':88,'baselineFailures':len(result['strictFailures']),'sameRunFailures':len(result['sameRunFailures']),'largestDifference':max(r['largestDifference'] for r in baselineChecks),'passed':result['passed']}))
