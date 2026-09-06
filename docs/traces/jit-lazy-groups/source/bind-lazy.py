import json,collections,hashlib,gzip,sys
from pathlib import Path
p=Path(sys.argv[1] if len(sys.argv)>1 else '.context/first-use/run-2')
def leb(b,i):
 n=shift=0
 while True:
  c=b[i];i+=1;n|=(c&127)<<shift
  if not c&128:return n,i
  shift+=7

def string(b,i):
 n,i=leb(b,i);return b[i:i+n].decode(),i+n

def limits(b,i):
 flags,i=leb(b,i);_,i=leb(b,i)
 if flags&1:_,i=leb(b,i)
 return i

def bodies(path):
 b=Path(path).read_bytes();assert b[:8]==b'\0asm\1\0\0\0';i=8;imports=0;out={}
 while i<len(b):
  kind=b[i];size,j=leb(b,i+1);data=b[j:j+size];i=j+size
  if kind==2:
   count,k=leb(data,0)
   for _ in range(count):
    _,k=string(data,k);_,k=string(data,k);typ=data[k];k+=1
    if typ==0:imports+=1;_,k=leb(data,k)
    elif typ==1:k+=1;k=limits(data,k)
    elif typ==2:k=limits(data,k)
    elif typ==3:k+=2
    else:raise Exception('Unexpected import '+str(typ))
   assert k==len(data)
  elif kind==10:
   count,k=leb(data,0)
   for index in range(imports,imports+count):
    n,k=leb(data,k);out[index]=n;k+=n
   assert k==len(data)
 return {'sha256':hashlib.sha256(b).hexdigest(),'bytes':len(b),'importFunctions':imports,'bodySizes':out}
r=json.loads((p/'report.json').read_text());variant=r['variant'];assert variant in ('original','candidate')
modules={'core':bodies('.context/jit-lazy-groups/runtime/emu.wasm'),'seed':bodies('.context/jit-lazy-groups/'+variant+'/seed.wasm')}
assert r['passed'] and not r['errors'] and not r['traceComplete'].get('dataLossOccurred',True)
assert all(any(a.get('sha256')==m['sha256'] for a in r['assets']) for m in modules.values())
es=json.loads((p/'trace.json').read_text() if (p/'trace.json').exists() else gzip.decompress((p/'trace.json.gz').read_bytes()))['traceEvents']
marks={e['args']['sync_id']:e['ts'] for e in es if e.get('name')=='clock_sync'};rows=[]
for e in es:
 if e.get('name')!='wasm.CompileBaseline':continue
 index=e['args']['funcIndex'];size=e['args']['bodySize'];matches=[name for name,m in modules.items() if m['bodySizes'].get(index)==size];label=next((name[:-6] for name,t in marks.items() if name.endswith('-start') and name[:-6]+'-end' in marks and t<=e['ts'] and e['ts']+e['dur']<=marks[name[:-6]+'-end']),'outside-action-or-boundary')
 rows.append({'stage':label,'index':index,'bodySize':size,'matchingModules':matches,'resolved':matches[0] if len(matches)==1 else 'unknown','threadCpuUs':e.get('tdur'),'wallUs':e['dur']})
counts=collections.Counter(x['resolved'] for x in rows)
out={'method':'Unique exact (function index, complete code-body byte length) match against both actual response-hash-bound WASM files; nonunique or absent pairs remain unknown. This binds baseline lazy bodies only, never anonymous top-tier scopes.','modules':modules,'counts':dict(counts),'events':rows}
(p/'lazy-binding.json').write_text(json.dumps(out,indent=2)+'\n')
print(counts)
for stage in ['C0-settings','H1-settings','C1-settings']:
 for module in modules:
  a=[x for x in rows if x['stage']==stage and x['resolved']==module]
  print(stage,module,len(a),sum(x['threadCpuUs'] for x in a)/1000,'bodybytes',sum(x['bodySize'] for x in a),'max',max((x['bodySize'] for x in a),default=0))
print('largest',sorted([x for x in rows if x['stage']=='C0-settings'],key=lambda x:x['threadCpuUs'],reverse=True)[:8])
