"""Freeze image predicates from retained real093 frames; no new browser/guest."""
from pathlib import Path
import ast,json,hashlib,struct,zlib
B=Path(__file__).resolve().parent;R=Path('.context/guest-http-default/browser-run-1');source=Path('.context/guest-safari/audit.py').read_text();tree=ast.parse(source);fn=next(n for n in tree.body if isinstance(n,ast.FunctionDef)and n.name=='png');exec(compile(ast.Module(body=[fn],type_ignores=[]),'<retained-png-decoder>','exec'))
images={}
for p in R.glob('*-guest.png'):
 w,h,rgb=png(p);assert w==320 and h in (480,481)
 # Retained screenshot has an extra top row, not a scaled canvas. The exact
 # native initial frame matches all460800 bytes after this crop. No resampling.
 data=rgb[(h-480)*320*3:];assert len(data)==460800
 images[p.name]=bytes(data)
out=B/'references';out.mkdir(exist_ok=True)
refs={'alert':'01-listener-restored-guest.png','home':'02-clean-home-guest.png','bookmarks':'06-safari-guest.png','address':'07-address-keyboard-guest.png','numbers':'08-address-numbers-guest.png','url':'10-address-settled-guest.png','landing':'11-local-home-guest.png','next':'12-local-next-guest.png'}
# Regions include actual page/keyboard text, exclude changing status clock.
regions={'alert':[35,180,250,170],'home':[0,35,320,400],'bookmarks':[0,75,320,335],'address':[0,270,320,200],'numbers':[0,270,320,200],'url':[20,74,112,27],'landing':[0,80,320,330],'next':[0,80,320,330]}
checks=[]
for key,name in refs.items():
 source_path=R/name;data=images[name]
 if key=='alert':
  source_path=Path('.context/guest-http-default/native-run-1/frame.17.png');w0,h0,data=png(source_path);assert(w0,h0)==(320,480)
 (out/(key+'.rgb')).write_bytes(data);x,y,w,h=regions[key]
 def error(other):
  total=0
  for yy in range(y,y+h):
   a=(yy*320+x)*3;b=a+w*3;total+=sum(abs(q-r)for q,r in zip(data[a:b],other[a:b]))
  return total/(w*h*3)
 distances={n:error(d)for n,d in images.items()};checks.append({'reference':key,'source':str(source_path),'sourceSHA256':hashlib.sha256(source_path.read_bytes()).hexdigest(),'projection':'native320x480 untouched'if key=='alert'else'drop one extra top row if481; no resampling','rawSHA256':hashlib.sha256(data).hexdigest(),'region':regions[key],'maeLimit':2.0,'selfMAE':error(data),'blankMAE':error(bytes(len(data))),'otherRetainedMAE':distances})
 assert error(data)==0 and error(bytes(len(data)))>2
 if key=='url':
  assert distances['07-address-keyboard-guest.png']>2 and distances['08-address-numbers-guest.png']>2 and distances['09-address-entered-guest.png']>2
# Actual input uses original coordinates. Address entry is a single bounded
# action group; no acceptance based only on supplied key sequence.
url=[(16,295),(48,295),(208,295),(107,458),(304,295),(107,458),(304,295),(107,458),(16,295),(132,404),(16,295),(240,295),(304,295),(240,295),(16,295)]
stages=[('dismiss','alert',[(160,300)],'home'),('launch-safari','home',[(199,430)],'bookmarks'),('open-address','bookmarks',[(290,43),(157,56)],'address'),('numeric-keyboard','address',[(39,458)],'numbers'),('enter-url','numbers',url,'url'),('load-local','url',[(282,458)],'landing'),('follow-next','landing',[(62,273)],'next'),('return-landing','next',[(55,289)],'landing')]
contract={'scope':'Source-only native diagnostic stages, no acceptance of later form result. Actual scanout gates before and after input.','holdTicksAt6MHz':1080000,'gapTicksAt6MHz':3900000,'perStageInstructionCap':1500000000,'perStageTickCap':180000000,'referenceMAELimit':2.0,'stableCaptures':2,'scanIntervalInstructions':2000000,'stages':[{'name':n,'before':a,'taps':t,'after':z}for n,a,t,z in stages],'finalAction':{'name':'focus-form','before':'landing','taps':[(110,310)],'stop':'first graphics failure/daemon exit-status witness or finite diagnostic tail','instructionCap':1000000000,'tickCap':180000000},'totalAdditionalInstructionCap':8000000000,'wallCapSeconds':450,'serializationCapSeconds':120,'noAutomaticRetry':True}
(B/'stages.json').write_text(json.dumps(contract,indent=2)+'\n');(B/'predicate-checks.json').write_text(json.dumps({'passed':True,'scope':'Retained positive/blank/cross-frame distances; not actual future native readiness proof','rows':checks},indent=2)+'\n');print([(x['reference'],sorted(x['otherRetainedMAE'].items(),key=lambda q:q[1])[:3])for x in checks])
