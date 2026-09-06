"""Original-file source evidence only. Never imported by the device or guest probe."""
from pathlib import Path
import hashlib,json,struct,sys
sys.path.insert(0,'.context/amc-first');import disassemble as kernel
sha=lambda b:hashlib.sha256(b).hexdigest()
assert sha(kernel.b)=='42e9f3c18d4a4d5c956acdcded0c3f8f247fe3e7fc96d14cfa3aedcd77c292c9'
mecca=Path('.context/amc-first/stage-m/MeCCA.original').read_bytes();assert sha(mecca)=='15f8f049239e64f88660aeb1ce99aa07273ccd19ca636e624ff19e9a68e7f4ec'
o=28;segments=[];sections=[]
for _ in range(struct.unpack_from('<I',mecca,16)[0]):
 cmd,size=struct.unpack_from('<II',mecca,o)
 if cmd==1:
  segments.append(struct.unpack_from('<4I',mecca,o+24))
  for i in range(struct.unpack_from('<I',mecca,o+48)[0]):sections.append(struct.unpack_from('<16s16s9I',mecca,o+56+68*i))
 if cmd==2:so,n,st,sz=struct.unpack_from('<4I',mecca,o+8)
 if cmd==11:ds=struct.unpack_from('<18I',mecca,o+8)
 o+=size
names={};syms=[]
for i in range(n):
 ix,typ,sec,desc,va=struct.unpack_from('<IBBHI',mecca,so+12*i);name=mecca[st+ix:mecca.find(b'\0',st+ix)].decode();syms.append(name)
 if va:names[name]=va

def read(va,size):
 for start,_,off,length in segments:
  if start<=va and va+size<=start+length:return mecca[off+va-start:off+va-start+size]
 raise AssertionError('original file span unavailable')
def branch(pc):
 w=struct.unpack('<I',read(pc,4))[0];assert w>>24==0xeb
 d=(w&0xffffff)*4;d-=0x4000000 if d&0x2000000 else 0
 return (pc+8+d)&0xffffffff
assert names['__ZN23AMC_IOKit_DecoderDriver12restartCodecEv']==0x3123cd18
assert names['__ZN18AMCDeviceInterface30initializeDecoderWithDMABufferEmm']==0x3123f6dc
assert names['__ZN18AMCDeviceInterface12startDecoderEv']==0x3123f90c
assert branch(0x3123cdc4)==0x3123f6dc and branch(0x3123ce14)==0x31240528 and branch(0x3123ce50)==0x3123f90c
assert struct.unpack('<I',read(0x3123f7c8,4))[0]==0xe3a01002
assert struct.unpack('<I',read(0x3123f958,4))[0]==0xe3a01003 and branch(0x3123f95c)==0x3123f2f8
stub=branch(0x3123f7ec);assert branch(0x3123f35c)==stub
stub_name=None
for sect,seg,addr,size,off,align,reloc,nreloc,flags,res1,res2 in sections:
 if flags&255==8 and res2 and addr<=stub<addr+size:
  index=struct.unpack_from('<I',mecca,ds[12]+4*(res1+(stub-addr)//res2))[0];stub_name=syms[index]
assert stub_name=='_IOConnectCallScalarMethod'
prefix=Path('.context/amc-first/stage-m/run-1/first/input-driver-prefix.bin').read_bytes();vtable,=struct.unpack_from('<I',prefix);client,=struct.unpack_from('<I',prefix,0x94)
assert vtable==0xc0281d78 and client==1
assert b'kAppleAMCDriverUserClientTypeMeCCA != mUserClientType' in kernel.readva(0xc027f964,120)
member=struct.unpack('<II',kernel.readva(0xc02300d0,8));assert member==(0x014c0000,0)
slot=(member[0]>>16)*4-4;assert slot==0x52c and struct.unpack('<I',kernel.readva(vtable+slot,4))[0]==0xc02732a0
assert struct.unpack_from('<I',kernel.readva(0xc0282b04,24),8)[0]==0xc0272224
spans={}
for name,va,size in [('restartSuccessPath',0x3123cdb8,0xa4),('initScalarCall',0x3123f7b0,0x44),('initOutputPopulation',0x3123f828,0x7c),('method3Producer',0x3123f954,0x10),('scalarNoArgCall',0x3123f34c,0x18),('outputInfoSuccess',0x31240610,0x2c),('refillRing',0x3123c974,0x28)]:
 data=read(va,size);spans[name]={'va':f'{va:08x}','bytes':size,'sha256':sha(data),'hex':data.hex()}
report={'passed':True,'scope':'Static original symbols, instructions, indirect import stub and actual previously captured client identity. No new guest execution or startup/consumption claim.','kernelSHA256':sha(kernel.b),'MeCCASHA256':sha(mecca),'actualDriverPrefixSHA256':sha(prefix),'actualClientType':client,'sourceClientTypeName':'MeCCA','actualDriverVtable':f'{vtable:08x}','separateIOAudio2Selector1':{'member':list(member),'slot':slot,'function':'c02732a0'},'MeCCAInitSelector':2,'MeCCAStartSelector':3,'AMCStartFunction':'c0272224','scalarImport':{'stub':f'{stub:08x}','name':stub_name},'originalSpans':spans}
Path('.context/amc-first/stage-m/start-path-source-oracle.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:v for k,v in report.items() if k!='originalSpans'}))
