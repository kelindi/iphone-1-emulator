"""Original-file proof for the actual saved return/window; no guest execution."""
from pathlib import Path
import hashlib,json,struct,sys
out=Path(sys.argv[1]);data=Path('.context/amc-first/stage-m/MeCCA.original').read_bytes();digest=hashlib.sha256(data).hexdigest();assert digest=='15f8f049239e64f88660aeb1ce99aa07273ccd19ca636e624ff19e9a68e7f4ec'
d=json.loads((out/'destination.json').read_text());assert json.loads((out/'destination-audit.json').read_text())['passed'] and d['callerWindowAvailable'];pc=d['callerWindowAddress'];lr=d['registers'][14];assert pc==lr-4
segments=[];sections=[];off=28
for _ in range(struct.unpack_from('<I',data,16)[0]):
 cmd,n=struct.unpack_from('<II',data,off)
 if cmd==1:
  segments.append(struct.unpack_from('<4I',data,off+24))
  for j in range(struct.unpack_from('<I',data,off+48)[0]):sections.append(struct.unpack_from('<16s16s9I',data,off+56+68*j))
 if cmd==2:so,num,st,sz=struct.unpack_from('<4I',data,off+8)
 if cmd==11:dysym=struct.unpack_from('<18I',data,off+8)
 off+=n
original=next(data[f+pc-v:f+pc-v+32] for v,z,f,n in segments if v<=pc and pc+32<=v+n)
assert (out/'destination-caller.bin').read_bytes()==original,'original caller window differs'
ins=struct.unpack_from('<I',original)[0];assert ins>>24==0xeb
imm=ins&0xffffff;imm=imm-(1<<24) if imm&(1<<23) else imm;target=(pc+8+4*imm)&0xffffffff
symbols=[]
for i in range(num):
 ix,ty,se,de,v=struct.unpack_from('<IBBHI',data,so+12*i);name=data[st+ix:data.find(b'\0',st+ix)].decode(errors='replace')
 symbols.append((name,v,ty,de))
functions=[(v,name) for name,v,ty,de in symbols if ty&0xe==0xe and v<=pc];start,name=max(functions)
assert (start,name)==(0x3122b188,'__ZN24AudioOutputBufferManager12writeSamplesEN24MainChannelBufferManager27MainChannelBufferDescriptorE')
stub=None
for row in sections:
 sect,seg,addr,size,offset,align,reloff,nreloc,flags,res1,res2=row
 if flags&255==8 and res2 and addr<=target<addr+size:
  assert (target-addr)%res2==0
  idx=struct.unpack_from('<I',data,dysym[12]+4*(res1+(target-addr)//res2))[0];stub=symbols[idx][0]
assert stub=='_memcpy' and target==0x312cdc04
report={'passed':True,'scope':'Actual saved-return/frame and mapped caller window match original MeCCA writeSamples memcpy call. Caller BL retirement was not separately traced.','originalSHA256':digest,'function':name,'functionStart':hex(start),'savedReturn':hex(lr),'window':hex(pc),'windowSHA256':hashlib.sha256(original).hexdigest(),'staticCallTarget':hex(target),'indirectStubSymbol':stub,'callerBLRetirementObserved':False,'destinationVirtual':hex(d['registers'][0]),'destinationPhysicalStart':hex(d['destinationMappings'][0][0]),'authoritativeTTBR0':hex(d['ttbr0']),'authoritativeTTBR1':hex(d['ttbr1'])}
(out/'caller-audit.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
