"""Read-only original Mach-O symbols and bounded disassembly; no guest runs."""
from pathlib import Path
import struct,sys,hashlib
sys.path.insert(0,str(Path('.context/graphics-speed/python').resolve()))
from capstone import *
b=Path(sys.argv[1]).read_bytes();segments=[];off=28;sym=None;sections=[];dysym=None
for _ in range(struct.unpack_from('<I',b,16)[0]):
 cmd,n=struct.unpack_from('<II',b,off)
 if cmd==1:
  segments.append(struct.unpack_from('<4I',b,off+24))
  for i in range(struct.unpack_from('<I',b,off+48)[0]):sections.append(struct.unpack_from('<16s16s9I',b,off+56+68*i))
 if cmd==11:dysym=struct.unpack_from('<18I',b,off+8)
 if cmd==2:sym=struct.unpack_from('<4I',b,off+8)
 off+=n
names={}
so,num,st,sz=sym
for i in range(num):
 ix,ty,se,desc,val=struct.unpack_from('<IBBHI',b,so+12*i)
 if val:names[val]=(b[st+ix:b.find(b'\0',st+ix)].decode(errors='replace'),desc)
if dysym:
 indirect,ni=dysym[12:14]
 for section in sections:
  sect,seg,addr,size,offset,align,reloff,nreloc,flags,res1,res2=section
  if flags&255!=8 or not res2:continue
  for i in range(size//res2):
   index=struct.unpack_from('<I',b,indirect+4*(res1+i))[0]
   if index>=num:continue
   ix=struct.unpack_from('<I',b,so+12*index)[0];names[addr+res2*i]=(b[st+ix:b.find(b'\0',st+ix)].decode(errors='replace')+' [stub]',0)
def read(va,size):
 for base,_,offset,count in segments:
  if base<=va and va+size<=base+count:return b[offset+va-base:offset+va-base+size]
 raise ValueError(hex(va))
start=int(sys.argv[2],0);count=int(sys.argv[3],0);thumb=len(sys.argv)>4 and sys.argv[4]=='thumb';md=Cs(CS_ARCH_ARM,CS_MODE_THUMB if thumb else CS_MODE_ARM);md.detail=True
print('original SHA256',hashlib.sha256(b).hexdigest())
for ins in md.disasm(read(start,count),start):
 if ins.address in names:print('\n'+names[ins.address][0])
 text=f'{ins.address:08x} {ins.mnemonic:8} {ins.op_str}'
 if ins.mnemonic in ('bl','blx') and ins.operands[0].type==CS_OP_IMM:text+=' '+str(names.get(ins.operands[0].imm,''))
 if ins.mnemonic.startswith('ldr') and len(ins.operands)>1 and ins.operands[1].type==CS_OP_MEM and ins.reg_name(ins.operands[1].mem.base)=='pc' and not ins.operands[1].mem.index:
  at=((ins.address+4)&~3 if thumb else ins.address+8)+ins.operands[1].mem.disp
  try:
   value=struct.unpack('<I',read(at,4))[0];text+=f' literal {at:08x}={value:08x} '+str(names.get(value,''))
  except ValueError:pass
 print(text)
