"""Independent header-derived field inventory; codec fields are not its source."""
from pathlib import Path
import re,json
p=Path(__file__).resolve().parent
h=re.sub(r'/\*.*?\*/','', (p/'sdio.h').read_text(),flags=re.S)
structs={}
for body,typename in re.findall(r'typedef struct\s*\{(.*?)\}\s*(\w+);',h,re.S):
 members=[]
 for decl in body.split(';'):
  decl=decl.strip()
  if not decl:continue
  typ,names=re.match(r'(bool|uint(?:8|16|32|64)_t|unsigned|void|sdio_\w+_t)\s+(.+)$',decl,re.S).groups()
  members.extend((typ,n.strip()) for n in names.split(','))
 structs[typename]=members
leaves=[]
def walk(typ,prefix=''):
 for t,n in structs[typ]:
  n=n.lstrip('*');base=n.split('[')[0];path=prefix+base
  if t in structs:walk(t,path+'.')
  else:leaves.append((path,t,n))
walk('sdio_t')
excluded={'dma_reader','dma_ctx'}
fields=re.findall(r'(BOOL|U8|U16|U32|U64|ENUM|BYTES)\(([^)]+)\)',(p/'fields.inc').read_text())
covered={re.sub(r'\[\d+\]','',name) for kind,name in fields}
assert covered=={name for name,t,n in leaves}-excluded,(covered,{name for name,t,n in leaves})
checks=[]
for name,t,n in leaves:
 if name in excluded:continue
 if '[' in n:
  checks.append('CHECK(!memcmp(a->device.%s,b->device.%s,sizeof a->device.%s));'%(name,name,name))
 else:checks.append('CHECK(a->device.%s==b->device.%s);'%(name,name))
(p/'independent_fields.inc').write_text('/* Generated from sdio.h, independently of the codec schema. */\n'+'\n'.join(checks)+'\n')
(p/'field-coverage.json').write_text(json.dumps({'serializedLeaves':len(checks),'excludedHostPointers':sorted(excluded),'passed':True},indent=2)+'\n')
print('PASS independent header inventory:',len(checks),'serialized leaves; only host callback/context excluded')

# Explicit wire offsets for negative mutation tests; nested v1 header48 plus outer envelope24 bytes.
size_by_kind={'BOOL':1,'U8':1,'U16':2,'U32':4,'U64':8,'ENUM':4}
by_name={name:n for name,t,n in leaves};offset=72;offsets={}
for kind,name in fields:
 if kind=='BYTES':
  dim=re.search(r'\[([^]]+)\]',by_name[name])[1]
  size={'SDIO_ENDPOINT_CAPACITY':1024,'SDIO_HELPER_CAPACITY':4096}.get(dim)
  if size is None:size=int(dim,0)
 else:size=size_by_kind[kind]
 offsets[name]={'offset':offset,'size':size};offset+=size
(p/'field-offsets.json').write_text(json.dumps({'totalBytes':offset+1490+8,'nestedV1Bytes':offset-24,'fields':offsets},indent=2)+'\n')
