import struct,re,json,hashlib
from pathlib import Path
import capstone
b=Path('.context/photo-discovery/PhotoLibrary').read_bytes(); segs=[]; sections={}; off=28
assert hashlib.sha256(b).hexdigest()=='194c2fe9d87d334777033756bd78ac4f47e4275915ae511b68b8504d5c6d8282'
for _ in range(struct.unpack_from('<I',b,16)[0]):
 cmd,n=struct.unpack_from('<II',b,off)
 if cmd==1:
  segs.append(struct.unpack_from('<4I',b,off+24))
  for i in range(struct.unpack_from('<I',b,off+48)[0]):
   s=off+56+68*i; sections[b[s:s+16].split(b'\0')[0].decode()]=struct.unpack_from('<II',b,s+32)
 off+=n

def read(a,n):
 for v,z,f,s in segs:
  if v<=a and a+n<=v+s:return b[f+a-v:f+a-v+n]
 return None

def word(a):
 d=read(a,4);return struct.unpack('<I',d)[0] if d else None

def describe(a):
 if a is None:return ''
 start,size=sections['__cstring']
 def string(p):
  if not start<=p<start+size:return None
  raw=read(p,min(180,start+size-p))
  if raw is None or b'\0' not in raw:return None
  raw=raw.split(b'\0')[0]
  return raw.decode() if all(32<=x<127 for x in raw) else None
 s=string(a)
 if s is not None:return repr(s)
 cf,cs=sections['__cfstring']
 if cf<=a and a+16<=cf+cs and (a-cf)%16==0:
  p=word(a+8); n=word(a+12); s=string(p)
  if s is not None and len(s)==n:return 'CFString '+repr(s)
 return ''
md=capstone.Cs(capstone.CS_ARCH_ARM,capstone.CS_MODE_ARM);md.skipdata=True
methods=[(0x32758da8,0x32758e00,'dcimPath'),(0x3275ab28,0x3275ac1c,'file paths'),(0x32755d14,0x32755d84,'thumbnailImage'),(0x32759458,0x3275988c,'fileGroups'),(0x32759b28,0x32759fa4,'dcfDirectories'),(0x32755298,0x327554a8,'CameraAlbum images')]
out=[]
for start,end,title in methods:
 out.append('\n'+title); regs={}
 for ins in md.disasm(read(start,end-start),start):
  s=ins.op_str; note='';m=re.fullmatch(r'(r\d+), \[pc, #(0x[0-9a-f]+|\d+)\]',s)
  if ins.mnemonic=='ldr' and m:
   val=word(ins.address+8+int(m[2],0));regs[m[1]]=val;note=f'literal {val:#x}' if val is not None else ''
  else:
   m=re.fullmatch(r'(r\d+), \[pc, (r\d+)\]',s)
   if ins.mnemonic=='ldr' and m and regs.get(m[2]) is not None:
    a=(ins.address+8+regs[m[2]])&0xffffffff;val=word(a);regs[m[1]]=val;note=f'[{a:#x}]={val} '+describe(val)
   else:
    m=re.fullmatch(r'(r\d+), pc, (r\d+)',s)
    if ins.mnemonic=='add' and m and regs.get(m[2]) is not None:
     val=(ins.address+8+regs[m[2]])&0xffffffff;regs[m[1]]=val;note=f'{val:#x} '+describe(val)
    elif ins.mnemonic.startswith('bl'):
     note='; '.join(k+' '+describe(regs.get(k)) for k in ['r0','r1','r2','r3'] if describe(regs.get(k)));[regs.pop(k,None) for k in ['r0','r1','r2','r3','r12']]
    elif ins.mnemonic not in ('cmp','tst','push','str','strb'):
     dest=s.split(',')[0];regs.pop(dest,None)
  out.append(f'{ins.address:08x} {ins.bytes.hex()} {ins.mnemonic:8} {s:35} {note}')
p=Path('.context/photo-discovery/annotated.txt');p.write_text('\n'.join(out)+'\n')
print(json.dumps({'sha256':hashlib.sha256(b).hexdigest(),'output':str(p)}))
