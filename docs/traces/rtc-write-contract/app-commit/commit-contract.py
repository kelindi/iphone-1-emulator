"""Verify original Settings commit metadata and the kernel calendar call path."""
from pathlib import Path
import struct,json,hashlib,capstone,shutil
out=Path('docs/traces/rtc-write-contract/app-commit');out.mkdir(exist_ok=True)
def image(path,expected):
 b=Path(path).read_bytes();assert hashlib.sha256(b).hexdigest()==expected
 segs=[];p=28
 for _ in range(struct.unpack_from('<I',b,16)[0]):
  c,n=struct.unpack_from('<II',b,p);assert n>=8 and p+n<=len(b)
  if c==1:
   va,vs,fo,fs=struct.unpack_from('<4I',b,p+24);assert fo+fs<=len(b);segs.append((va,fo,fs))
  p+=n
 def raw(va,n):
  hits=[b[f+va-v:f+va-v+n] for v,f,s in segs if v<=va and va+n<=v+s];assert len(hits)==1;return hits[0]
 return raw
app=image('.context/rtc-contract/Preferences.app.bin','5efb9f552131284d4ca63ce5b369e953d5aa6906106fa0d94dc241637577d2a0')
kernel=image('web/firmware/kernel_1A543a.macho','42e9f3c18d4a4d5c956acdcded0c3f8f247fe3e7fc96d14cfa3aedcd77c292c9')
def word(raw,a):return struct.unpack('<I',raw(a,4))[0]
def string(raw,a):return raw(a,100).split(b'\0')[0].decode('ascii')
assert string(app,word(app,0x1996c+8))=='DateController'
assert word(app,0x1996c+28)==0x1a448 and word(app,0x1a448+4)==15
methods=[]
for i in range(15):
 n,t,f=struct.unpack('<III',app(0x1a448+8+12*i,12));methods.append({'selector':string(app,n),'types':string(app,t),'implementation':hex(f)})
assert next(x for x in methods if x['selector']=='datePickerChanged:')['implementation']=='0x7bf4'
selectors={hex(a):string(app,word(app,a)) for a in [0x194a4,0x194a8,0x19494,0x194ac]}
assert list(selectors.values())==['date','timeIntervalSince1970','update:','startTimer']
stubs=[]
for address,target,name in [(0x162d4,0x30e980a4,'objc_msgSend'),(0x16514,0x300679a0,'settimeofday'),(0x16538,0x30040c60,'lrint')]:
 assert app(address,8)==bytes.fromhex('00c09fe500f09ce5');p=word(app,address+8);assert word(app,p)==target;stubs.append({'stub':hex(address),'pointer':hex(p),'target':hex(target),'symbol':name})
ranges=[]
for raw,name,address,size,thumb in [(app,'datePickerChanged',0x7bf4,0x98,False),(kernel,'settimeofday',0xc01031f0,0x86,True),(kernel,'set-calendar-wrapper',0xc01031e0,0x10,True),(kernel,'clock-set-calendar',0xc001898c,0xca,True),(kernel,'platform-set-gmt',0xc014c674,0x18,True),(kernel,'arm-platform-set-gmt',0xc015c490,0x5a,True),(kernel,'rtc-platform-dispatch',0xc015de04,0x86,True)]:
 code=raw(address,size);md=capstone.Cs(capstone.CS_ARCH_ARM,capstone.CS_MODE_THUMB if thumb else capstone.CS_MODE_ARM);ins=list(md.disasm(code,address));assert sum(i.size for i in ins)==size
 (out/(name+'.txt')).write_text('\n'.join(f'{i.address:08x} {i.bytes.hex():8} {i.mnemonic:8} {i.op_str}' for i in ins)+'\n')
 ranges.append({'name':name,'address':hex(address),'bytes':size,'thumb':thumb,'sha256':hashlib.sha256(code).hexdigest()})
for a,v in [(0xc0018a60,0xc01c85dc),(0xc0018a64,0xc01c865c),(0xc014c68c,0xc01d52d0)]:assert word(kernel,a)==v
r={'passed':True,'scope':'Static original metadata/code proof plus separately decoded immutable snapshots. No new guest execution or RTC transaction claim.','appSHA256':'5efb9f552131284d4ca63ce5b369e953d5aa6906106fa0d94dc241637577d2a0','classAddress':'0x1996c','methodListAddress':'0x1a448','methods':methods,'selectors':selectors,'stubs':stubs,'ranges':ranges}
(out/'report.json').write_text(json.dumps(r,indent=2)+'\n')
for name in ['commit-contract.py','inspect-calendar.c','inspect-calendar-build.log','calendar-snapshots.jsonl']:
 shutil.copy2(Path('.context/rtc-contract')/name,out/name)
for name in ['peek.h','peek.c']:shutil.copy2(Path('.context/amc-first/stage-h/source')/name,out/name)
print(json.dumps({'passed':True,'methods':len(methods),'ranges':len(ranges)}))
