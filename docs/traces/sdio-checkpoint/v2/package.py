"""Private experiment package. No production snapshot/Import integration."""
from pathlib import Path
import hashlib,json,os,re,stat,uuid
MAX_MACHINE=300*1024*1024
MAX_SIDE=300000

def digest(data):return hashlib.sha256(data).hexdigest()
def canonical(data):return (json.dumps(data,sort_keys=True,separators=(',',':'))+'\n').encode()
def _unique(pairs):
 result={}
 for k,v in pairs:
  if k in result:raise ValueError('duplicate JSON key')
  result[k]=v
 return result

def _read(path,limit,exact=None):
 fd=os.open(path,os.O_RDONLY|os.O_NOFOLLOW)
 with os.fdopen(fd,'rb') as f:
  st=os.fstat(f.fileno())
  if not stat.S_ISREG(st.st_mode) or st.st_size>limit or (exact is not None and st.st_size!=exact):raise ValueError('file bounds')
  b=f.read(limit+1)
 if len(b)>limit or (exact is not None and len(b)!=exact):raise ValueError('changed file length')
 return b

def _write_new(path,data):
 fd=os.open(path,os.O_WRONLY|os.O_CREAT|os.O_EXCL,0o600)
 with os.fdopen(fd,'wb') as f:f.write(data);f.flush();os.fsync(f.fileno())

def _sync_dir(path):
 fd=os.open(path,os.O_RDONLY|os.O_DIRECTORY)
 try:os.fsync(fd)
 finally:os.close(fd)

def publish(directory,machine,side,identity,validate,fail_at=None):
 """validate receives immutable byte strings and must run the real C pair validator.
 Failed validation/publication leaves any earlier manifest and generation intact.
 Orphan immutable generation files after a simulated crash are harmless."""
 d=Path(directory);d.mkdir(parents=True,exist_ok=True)
 if not isinstance(machine,bytes) or not isinstance(side,bytes) or not 0<len(machine)<=MAX_MACHINE or not 0<len(side)<=MAX_SIDE:raise ValueError('size/type')
 if not isinstance(identity,dict) or not identity or any(not isinstance(k,str) or not isinstance(v,str) or not v for k,v in identity.items()):raise ValueError('identity')
 validate(machine,side)
 generation=uuid.uuid4().hex
 manifest={'format':'private-sdio-pair','version':1,'identity':dict(identity),'generation':generation}
 for name,data,suffix in [('machine',machine,'snap'),('sidecar',side,'side')]:
  filename=generation+'.'+suffix
  _write_new(d/filename,data)
  manifest[name]={'file':filename,'size':len(data),'sha256':digest(data)}
  if fail_at==name:raise RuntimeError('simulated crash after '+name)
 _sync_dir(d)
 tmp=d/(generation+'.manifest.tmp');_write_new(tmp,canonical(manifest))
 if fail_at=='manifest':raise RuntimeError('simulated crash before manifest publication')
 os.replace(tmp,d/'manifest.json');_sync_dir(d)
 return manifest

def load(directory,expected_identity,validate):
 d=Path(directory);manifest=json.loads(_read(d/'manifest.json',65536),object_pairs_hook=_unique)
 if not isinstance(manifest,dict) or set(manifest)!={'format','version','identity','generation','machine','sidecar'} or manifest['format']!='private-sdio-pair' or type(manifest['version']) is not int or manifest['version']!=1:raise ValueError('manifest version/schema')
 if manifest['identity']!=expected_identity:raise ValueError('incompatible experiment identity')
 generation=manifest['generation']
 if not isinstance(generation,str) or not re.fullmatch('[0-9a-f]{32}',generation):raise ValueError('generation')
 result=[]
 for name,suffix,cap in [('machine','snap',MAX_MACHINE),('sidecar','side',MAX_SIDE)]:
  record=manifest[name]
  if not isinstance(record,dict) or set(record)!={'file','size','sha256'}:raise ValueError('record schema')
  if record['file']!=generation+'.'+suffix or type(record['size']) is not int or not 0<record['size']<=cap or not isinstance(record['sha256'],str) or not re.fullmatch('[0-9a-f]{64}',record['sha256']):raise ValueError('record bounds/path')
  data=_read(d/record['file'],cap,record['size'])
  if digest(data)!=record['sha256']:raise ValueError('hash mismatch')
  result.append(data)
 validate(*result)
 return (*result,manifest)
