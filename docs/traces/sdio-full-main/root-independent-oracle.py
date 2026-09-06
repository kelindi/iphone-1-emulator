from pathlib import Path
import struct,hashlib,json
p=Path('.context/sdio-first/main-full-plan');kernel=Path('web/firmware/kernel_1A543a.macho').read_bytes()
assert hashlib.sha256(kernel).hexdigest()=='42e9f3c18d4a4d5c956acdcded0c3f8f247fe3e7fc96d14cfa3aedcd77c292c9'
assert struct.unpack_from('<I',kernel)[0]==0xfeedface
pos=28;segments=[]
for _ in range(struct.unpack_from('<I',kernel,16)[0]):
 cmd,size=struct.unpack_from('<II',kernel,pos)
 if cmd==1:segments.append(struct.unpack_from('<IIII',kernel,pos+24))
 pos+=size
va=0xc0392d1c;length=122608
spans=[off+va-vm for vm,vsize,off,fsize in segments if vm<=va and va+length<=vm+fsize]
assert len(spans)==1
container=kernel[spans[0]:spans[0]+length]
assert hashlib.sha256(container).hexdigest()=='06a5d9d1f5cc4c5209cd072bb2434058be3d52b8bebb2b022153c325f3821150'
# Only first32 host-buffer bytes are known from the genuine helper terminator.
buffer=bytearray(512);known=bytearray(32*[1]+480*[0]);wire=bytearray();rows=[]
banks={'low':(0,bytearray(0x17000),bytearray(0x17000)),
       'high':(0xc0001000,bytearray(0x6000),bytearray(0x6000))}
pos=0;headers=bodies=copied=0

def send(logical):
 n=len(logical);physical=(n+31)//32*32
 assert 0<n<=512
 buffer[:n]=logical;known[:n]=bytes([1])*n
 assert all(known[:physical]),'Cannot invent initial wire padding'
 wire.extend(buffer[:physical]);return physical
while pos<len(container):
 start=pos;header=container[pos:pos+16];assert len(header)==16
 command,dest,size,ignored=struct.unpack('<4I',header);pos+=16;headers+=1
 physical_header=send(header)
 if command==4:
  assert pos==len(container) and dest==0 and size==0
  rows.append({'offset':start,'command':command,'destination':dest,'headerWire':physical_header});break
 assert command==1 and 4<=size<=512 and pos+size<=len(container)
 body=container[pos:pos+size];pos+=size;bodywire=send(body);n=size-4
 choices=[(name,base,memory,mask) for name,(base,memory,mask) in banks.items() if base<=dest and dest+n<=base+len(memory)]
 assert len(choices)==1
 name,base,memory,mask=choices[0];offset=dest-base
 assert not any(mask[offset:offset+n]),'Original container unexpectedly overlaps'
 memory[offset:offset+n]=body[:n];mask[offset:offset+n]=bytes([1])*n
 bodies+=1;copied+=n
 rows.append({'offset':start,'command':command,'destination':dest,'logicalBody':size,'headerWire':physical_header,'bodyWire':bodywire,'copied':n,'bank':name})
assert (headers,bodies,copied,len(wire))==(234,233,117932,126368)
assert wire==(p/'expected-main-wire.bin').read_bytes()
for name,(base,memory,mask) in banks.items():
 assert memory==(p/('expected-'+name+'.bin')).read_bytes()
 assert mask==(p/('expected-'+name+'-mask.bin')).read_bytes()
 assert sum(mask)==(93556 if name=='low' else 24376)
report={'passed':True,'scope':'Independent root extraction/parser directly from original Mach-O: complete main wire including retained padding and known-byte guards; exact two memory banks and actual-write masks agree with separate agent oracle. Static prediction only; full guest run remains a separate gate.',
 'headers':headers,'bodies':bodies,'copiedBytes':copied,'mainWireBytes':len(wire),'mainWireSha256':hashlib.sha256(wire).hexdigest(),
 'banks':{name:{'base':base,'bytes':len(memory),'written':sum(mask),'guardBytes':len(mask)-sum(mask),'sha256':hashlib.sha256(memory).hexdigest(),'maskSha256':hashlib.sha256(mask).hexdigest()} for name,(base,memory,mask) in banks.items()},'records':rows}
(p/'root-independent-oracle.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:v for k,v in report.items() if k!='records'}))
