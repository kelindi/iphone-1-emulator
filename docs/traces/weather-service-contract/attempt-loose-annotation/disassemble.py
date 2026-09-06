import struct,sys,json,capstone
from pathlib import Path
b=Path('.context/service-discovery/Weather.original').read_bytes();segments=[];sections={};pos=28;symbol_table=indirect=None
for _ in range(struct.unpack_from('<I',b,16)[0]):
 cmd,size=struct.unpack_from('<II',b,pos)
 if cmd==1:
  va,vs,fo,fs=struct.unpack_from('<4I',b,pos+24);segments.append((va,fo,fs))
  for i in range(struct.unpack_from('<I',b,pos+48)[0]):
   off=pos+56+i*68;sections[b[off:off+16].split(b'\0')[0].decode()]=struct.unpack_from('<9I',b,off+32)
 if cmd==2:symbol_table=struct.unpack_from('<4I',b,pos+8)
 if cmd==11:indirect=struct.unpack_from('<2I',b,pos+56)
 pos+=size
def read(va,n):
 for v,f,z in segments:
  if v<=va and va+n<=v+z:return b[f+va-v:f+va-v+n]
 return b''
def cstr(va):
 out=b''
 for i in range(180):
  c=read(va+i,1)
  if not c:return None
  if c==b'\0':return out.decode('ascii','replace')
  if c[0]<32 or c[0]>126:return None
  out+=c
 return None
so,n,st,sz=symbol_table;symbols=[]
for i in range(n):
 si=struct.unpack_from('<I',b,so+12*i)[0];symbols.append(b[st+si:b.find(b'\0',st+si)].decode())
lookup={}
for name in ['__picsymbolstub4','__symbol_stub4','__nl_symbol_ptr','__la_symbol_ptr']:
 va,size,offset,align,reloff,nreloc,flags,first,stride=sections[name]
 if not stride:stride=4
 for i in range(size//stride):
  index=struct.unpack_from('<I',b,indirect[0]+4*(first+i))[0]
  if index<len(symbols):lookup[va+stride*i]=symbols[index]
methods=json.loads(Path('.context/service-discovery/weather-methods.json').read_text());labels={}
for cls in methods:
 for method in cls['methods']:labels[int(method['implementation'],16)]=cls['name']+'::'+method['selector']
def describe(va):
 if va in lookup:return lookup[va]
 if va in labels:return labels[va]
 text=cstr(va)
 if text and len(text)>2:return repr(text)
 raw=read(va,16)
 if len(raw)==16:
  words=struct.unpack('<4I',raw)
  for index in [0,2]:
   text=cstr(words[index])
   if text and len(text)>2:return ('pointer ' if index==0 else 'CFString ')+repr(text)
 return ''
md=capstone.Cs(capstone.CS_ARCH_ARM,capstone.CS_MODE_ARM);md.detail=True;md.skipdata=True
start,end=[int(x,16) for x in sys.argv[1:3]]
for i in md.disasm(read(start,end-start),start):
 extra=[]
 if i.mnemonic in ['bl','b','blx'] and i.operands and i.operands[0].type==capstone.arm.ARM_OP_IMM:
  text=describe(i.operands[0].imm)
  if text:extra.append(text)
 if i.mnemonic.startswith('ldr') and i.id:
  for operand in i.operands:
   if operand.type==capstone.arm.ARM_OP_MEM and operand.mem.base==capstone.arm.ARM_REG_PC:
    value=read(i.address+8+operand.mem.disp,4)
    if len(value)==4:
     number=struct.unpack('<I',value)[0];extra.append(hex(number)+' '+describe(number))
 if i.address in labels:print('\n'+labels[i.address])
 print('%08x %-8s %-35s %s'%(i.address,i.mnemonic,i.op_str,'; '+' | '.join(extra) if extra else ''))
