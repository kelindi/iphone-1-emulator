from pathlib import Path
import json,struct,bisect,hashlib,re,collections
root=Path.cwd();out=root/'.context/jit-loop-evidence'
paths=['.context/organizer-debug/libSystem.B.dylib','web/firmware/kernel_1A543a.macho']
want={'_memcpy','_memmove','_memset','_bcopy','_bzero','_ovbcopy','_bcopy_phys','_bzero_phys','_bzero_phys_nc','_wmemcpy','_wmemmove','_wmemset'}
ranges=[];inputs={}
for path in paths:
 b=(root/path).read_bytes();inputs[path]=hashlib.sha256(b).hexdigest();pos=28;segs=[];so=n=st=0
 for _ in range(struct.unpack_from('<I',b,16)[0]):
  cmd,size=struct.unpack_from('<II',b,pos)
  if cmd==1:
   name=b[pos+8:pos+24].split(b'\0')[0];vm,vs,fo,fs=struct.unpack_from('<4I',b,pos+24)
   if name==b'__TEXT':segs.append((vm,vm+vs))
  if cmd==2:so,n,st,_=struct.unpack_from('<4I',b,pos+8)
  pos+=size
 groups=collections.defaultdict(list)
 for i in range(n):
  si,ty,sec,desc,va=struct.unpack_from('<IBBHI',b,so+12*i);va&=~1
  if ty&0xe0 or ty&0xe!=0xe or not any(a<=va<z for a,z in segs):continue
  name=b[st+si:b.index(b'\0',st+si)].decode('latin1');groups[va].append(name)
 addresses=sorted(groups)
 for i,addr in enumerate(addresses):
  names=groups[addr]
  if set(names)&want:ranges.append({'image':Path(path).name,'start':addr,'end':addresses[i+1] if i+1<len(addresses) else next(z for a,z in segs if a<=addr<z),'symbols':names})
regions=json.loads((root/'web/jit/regions.json').read_text());inputs['web/jit/regions.json']=hashlib.sha256((root/'web/jit/regions.json').read_bytes()).hexdigest()
census=json.loads((root/'.context/jit-register-layout/census.json').read_text())
# Parse actual export indices; do not assume the number/order of imports.
b=(root/'web/jit/seed.wasm').read_bytes();inputs['web/jit/seed.wasm']=hashlib.sha256(b).hexdigest()
def uleb(pos):
 val=shift=0
 while True:
  byte=b[pos];pos+=1;val|=(byte&127)<<shift
  if byte<128:return val,pos
  shift+=7
pos=8;exports={}
while pos<len(b):
 kind=b[pos];size,p=uleb(pos+1);end=p+size
 if kind==7:
  count,p=uleb(p)
  for _ in range(count):
   n,p=uleb(p);name=b[p:p+n].decode();p+=n;tag=b[p];index,p=uleb(p+1)
   if tag==0 and re.fullmatch('b[0-9]+',name):exports[index]=int(name[1:])
 pos=end
assert len(exports)==512 and inputs['web/jit/seed.wasm']=='d9c661556ab17bb8433ed9b0c3ecfd52e2fcafe58221b4cb84a920e4976b3881'
def touching(pc):return [r for r in ranges if pc<r['end'] and r['start']<pc+1024]
touchRegions=[]
for row in census['weighted']:
 hits=touching(row['pc'])
 if hits:touchRegions.append({'index':row['index'],'pc':row['pc'],'thumb':row['thumb'],'calls':row['calls'],'overlappingSymbols':hits})
profile=json.loads((root/'docs/traces/browser-cpu-attribution/analysis.json').read_text());profiles=[]
for stage in profile['stages']:
 if stage['stage'] not in ['0-settings','1-settings']:continue
 p=next(p for p in stage['profiles'] if p['role']=='worker');rows=[]
 for f in p['functions']:
  if f['category']!='generated_module_inferred':continue
  index=int(re.fullmatch(r'wasm-function\[(\d+)\]',f['functionName'])[1]);region=regions[exports[index]];assert region['index']==exports[index]
  hits=touching(region['pc'])
  if hits:rows.append({'functionIndex':index,'regionIndex':region['index'],'pc':region['pc'],'thumb':region['thumb'],'selfUs':f['selfUs'],'symbols':hits})
 total=sum(x['selfUs'] for x in rows);profiles.append({'stage':stage['stage'],'touchingRegions':rows,'selfSampledMsOfAllTouchingRegions':total/1000,'percentOfWorkerResidencyAllTouchingRegions':100*total/(p['sampledResidencyMs']*1000)})
report={'scope':'Source and existing-data only. Named copy/fill symbol extents use next distinct text symbol, not verified function sizes. Entire overlapping1KiB generated region counts/time are upper bounds for the named routines inside those regions, not function-call or guest-retirement counts. Browser generated-module identity retains accepted profile inference limits. No new emulation, profiling or timing.','inputs':inputs,'symbolRanges':ranges,'mbx100MRegionCalls':touchRegions,'sumTouchingRegionCalls':sum(x['calls'] for x in touchRegions),'allRegionCalls':census['runs'][1]['calls'],'browserProfiles':profiles}
(out/'existing.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({'symbolRanges':[(hex(x['start']),hex(x['end']),x['symbols']) for x in ranges],'touchingRegionCalls':report['sumTouchingRegionCalls'],'allRegionCalls':report['allRegionCalls'],'profiles':[{k:v for k,v in p.items() if k!='touchingRegions'} for p in profiles]},indent=2))
