"""Read-only classic Mach-O import/relocation audit; never executes guest code."""
import hashlib,json,struct
from pathlib import Path
B=Path(__file__).resolve().parent

def parse(path):
 b=path.read_bytes();h=struct.unpack_from('<7I',b);assert h[0]==0xfeedface
 pos=28;cmds=[];symbols=[];sections=[];segments=[];dyld=None;deps=[];entry=None;ds=None
 for _ in range(h[4]):
  cmd,size=struct.unpack_from('<2I',b,pos);assert size>=8 and pos+size<=28+h[5];cmds.append(cmd)
  if cmd==1:
   n,va,vs,off,fs,ma,ini,ns,flags=struct.unpack_from('<16s8I',b,pos+8);assert off+fs<=len(b);segments.append((n.rstrip(b'\0').decode(),va,vs,off,fs,ini))
   for i in range(ns):
    a=struct.unpack_from('<16s16s9I',b,pos+56+i*68);sections.append({'name':a[0].rstrip(b'\0').decode(),'segment':a[1].rstrip(b'\0').decode(),'address':a[2],'size':a[3],'fileOffset':a[4]})
  if cmd==2:
   so,count,st,ss=struct.unpack_from('<4I',b,pos+8);assert so+count*12<=len(b) and st+ss<=len(b);strings=b[st:st+ss]
   for i in range(count):
    index,typ,sec,desc,value=struct.unpack_from('<IBBHI',b,so+i*12);assert index<ss;name=strings[index:].split(b'\0')[0].decode();symbols.append({'name':name,'type':typ,'section':sec,'description':desc,'value':value})
  if cmd==0xb:ds=struct.unpack_from('<18I',b,pos+8)
  if cmd in (0xc,0xe):
   off=struct.unpack_from('<I',b,pos+8)[0];value=b[pos+off:pos+size].split(b'\0')[0].decode()
   if cmd==0xe:dyld=value
   else:deps.append(value)
  if cmd==5:
   flavor,count=struct.unpack_from('<2I',b,pos+8);assert flavor==1 and count==17
   regs=struct.unpack_from('<17I',b,pos+16);entry=regs[15];assert regs[16]&0x20==0
  pos+=size
 assert pos==28+h[5]
 return b,h,cmds,symbols,sections,segments,dyld,deps,entry,ds

p=B/'iphone1-own-program';b,h,commands,symbols,sections,segments,dyld,deps,entry,ds=parse(p)
assert h[1:4]==(12,6,2) and h[6]==0x85
assert commands==[1,1,1,1,2,0xb,0xe,5,0xc]
assert dyld=='/usr/lib/dyld' and deps==['/usr/lib/libSystem.B.dylib']
assert ds[4]==3 and ds[13]==0 and ds[15]==3 and ds[17]==0
undefined=[s for s in symbols if s['type']==1];assert [s['name'] for s in undefined]==['__exit','_getpid','_write'];assert all(s['description']==0x100 for s in undefined)
lib=parse(B/'original-readonly/usr__lib__libSystem.B.dylib');exports={s['name']:s for s in lib[3] if s['type']==0xf};assert lib[7]==[]
text=next(s for s in sections if s['name']=='__text');assert text['address']<=entry<text['address']+text['size']
data=next(s for s in sections if s['name']=='__data');assert data['size']==12
base=next(s for s in segments if s[0]=='__TEXT')[1]
relocations=[]
for i in range(ds[15]):
 address,info=struct.unpack_from('<II',b,ds[14]+i*8);index=info&0xffffff;assert (info>>24)&1==0 and (info>>25)&3==2 and (info>>27)&1==1 and info>>28==0
 symbol=symbols[index];assert symbol['name'] in exports
 target=base+address;assert data['address']<=target<=data['address']+data['size']-4;off=data['fileOffset']+target-data['address'];assert b[off:off+4]==bytes(4)
 relocations.append({'address':hex(target),'symbol':symbol['name'],'originalExportVA':hex(exports[symbol['name']]['value'])})
assert len({r['address'] for r in relocations})==3 and {r['symbol'] for r in relocations}=={s['name'] for s in undefined}
report={'passed':True,'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest(),'entry':hex(entry),'commands':[hex(c) for c in commands],'dyld':dyld,'libraries':deps,'eagerRelocations':relocations,'sourceLibrarySHA256':hashlib.sha256(lib[0]).hexdigest(),'sourceLibraryDependencies':lib[7],'signed':False,'scope':'Own code only, classic ARMv6 executable and three real libSystem eager imports. Static linker/format proof, not original dyld execution or kernel admission.'}
(B/'binary-audit.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
