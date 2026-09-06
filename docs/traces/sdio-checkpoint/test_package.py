from pathlib import Path
import package,json,subprocess,struct,tempfile,copy,hashlib
here=Path(__file__).resolve().parent
m=(here/'fixture/machine.snap').read_bytes();s=(here/'fixture/sdio.side').read_bytes()
identity={'model':'069-frozen','core':hashlib.sha256(Path('.context/build-native/core/libemucore.a').read_bytes()).hexdigest(),'schema':'private-sidecar-1','input':'synthetic-no-Apple-image'}
checks=0
def check(b):
 global checks
 checks+=1
 assert b

def validate(m,s):
 r=subprocess.run([str(here/'validate')],input=struct.pack('<II',len(m),len(s))+m+s,capture_output=True)
 if r.returncode:raise ValueError('C fresh pair rejected: '+str(r.returncode))

def rejected(fn):
 try:fn()
 except (ValueError,OSError,RuntimeError):check(True)
 else:check(False)

with tempfile.TemporaryDirectory(prefix='sdio-checkpoint-') as td:
 d=Path(td);a=package.publish(d,m,s,identity,validate)
 mm,ss,manifest=package.load(d,identity,validate);check(mm==m and ss==s and manifest==a)
 original=(d/'manifest.json').read_bytes()
 for failure in ['machine','sidecar','manifest']:
  rejected(lambda:package.publish(d,m,s,identity,validate,fail_at=failure))
  check((d/'manifest.json').read_bytes()==original);check(package.load(d,identity,validate)[:2]==(m,s))
 for broken in [dict(identity,model='unreviewed-model'),dict(identity,core='changed'),{}]:rejected(lambda:package.load(d,broken,validate))
 variants=[]
 for field,value in [('version',2),('version',True),('format','ordinary-user-import'),('generation','../escape')]:
  x=copy.deepcopy(a);x[field]=value;variants.append(x)
 for part in ['machine','sidecar']:
  for field,value in [('file','../../real-profile'),('size',0),('size',True),('size',1<<40),('sha256','0'*64)]:
   x=copy.deepcopy(a);x[part][field]=value;variants.append(x)
 for x in variants:
  (d/'manifest.json').write_bytes(package.canonical(x));rejected(lambda:package.load(d,identity,validate))
 for malformed_root in [b'[]',b'null',b'42']:
  (d/'manifest.json').write_bytes(malformed_root);rejected(lambda:package.load(d,identity,validate))
 (d/'manifest.json').write_bytes(b'{"version":1,"version":2}')
 rejected(lambda:package.load(d,identity,validate));(d/'manifest.json').write_bytes(original)
 for part,data in [('machine',m),('sidecar',s)]:
  file=d/a[part]['file']
  for broken in [data[:-1],data+b'x',data[:len(data)//2]+bytes([data[len(data)//2]^1])+data[len(data)//2+1:]]:
   file.write_bytes(broken);rejected(lambda:package.load(d,identity,validate))
  file.write_bytes(data)
  file.unlink();file.symlink_to(d/'manifest.json');rejected(lambda:package.load(d,identity,validate));file.unlink();file.write_bytes(data)
 # Correctly rehashed but structurally impossible sidecar is rejected by C.
 bad=bytearray(s);bad[48]=2
 x=copy.deepcopy(a);x['sidecar']['sha256']=package.digest(bad)
 (d/x['sidecar']['file']).write_bytes(bad);(d/'manifest.json').write_bytes(package.canonical(x))
 rejected(lambda:package.load(d,identity,validate));(d/a['sidecar']['file']).write_bytes(s);(d/'manifest.json').write_bytes(original)
 rejected(lambda:package.publish(d,m,bytes(bad),identity,validate));check((d/'manifest.json').read_bytes()==original)
 offsets=json.loads((here/'field-offsets.json').read_text());check(offsets['totalBytes']==len(s))
 malformed={'card.state':4,'endpoint.capacity':1025,'endpoint.used':513,'helper.used':4097,'data.length':513,'data.armed':1,'main_model.phase':8,'main_model.pending_length':513,'main_model.requested':513,'main_model.diagnostic':4,'main_model.low_coverage':2,'config.complete_main':1,'irq_pending':1}
 for field,value in malformed.items():
  bad=bytearray(s);r=offsets['fields'][field];n=min(r['size'],8)
  bad[r['offset']:r['offset']+n]=value.to_bytes(n,'little')
  x=copy.deepcopy(a);x['sidecar']['sha256']=package.digest(bad)
  (d/x['sidecar']['file']).write_bytes(bad);(d/'manifest.json').write_bytes(package.canonical(x))
  rejected(lambda:package.load(d,identity,validate))
 (d/a['sidecar']['file']).write_bytes(s);(d/'manifest.json').write_bytes(original)
 # Sidecar machine instant and IRQ disagreement must not load even with new hashes.
 for pos in [16,24,32,36,40]:
  bad=bytearray(s);bad[pos]^=1
  x=copy.deepcopy(a);x['sidecar']['sha256']=package.digest(bad)
  (d/x['sidecar']['file']).write_bytes(bad);(d/'manifest.json').write_bytes(package.canonical(x))
  rejected(lambda:package.load(d,identity,validate))
 (d/a['sidecar']['file']).write_bytes(s);(d/'manifest.json').write_bytes(original)
 b=package.publish(d,m,s,identity,validate);check(a['generation']!=b['generation']);check((d/a['machine']['file']).read_bytes()==m);check(package.load(d,identity,validate)[:2]==(m,s))
 check(subprocess.run([str(here/'validate')],input=struct.pack('<II',len(m),len(s))+m+s+b'extra',capture_output=True).returncode!=0)
print('Private package:',checks,'checks PASS; canonical real C fresh-machine validator used')
(here/'package-report.json').write_text(json.dumps({'checks':checks,'passed':True,'machineBytes':len(m),'sidecarBytes':len(s),'identity':identity},indent=2)+'\n')
