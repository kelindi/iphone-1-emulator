from pathlib import Path
import struct,hashlib,json
p=Path('.context/sdio-first/main-full');kernel=Path('web/firmware/kernel_1A543a.macho').read_bytes()
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
assert wire==(p/'full-request-wire.bin').read_bytes()[2336:]
for name,(base,memory,mask) in banks.items():
 assert memory==(p/('full-'+name+'.bin')).read_bytes()
 assert mask==(p/('full-'+name+'-mask.bin')).read_bytes()
 assert sum(mask)==(93556 if name=='low' else 24376)
report={'passed':True,'scope':'Independent root extraction/parser directly from original Mach-O compared to actual completed native run: complete wire including retained padding, two memory banks and actual-write masks, outer canaries and original-driver acknowledgement sequence. No device model or precomputed expected artifacts imported.',
 'headers':headers,'bodies':bodies,'copiedBytes':copied,'mainWireBytes':len(wire),'mainWireSha256':hashlib.sha256(wire).hexdigest(),
 'banks':{name:{'base':base,'bytes':len(memory),'written':sum(mask),'guardBytes':len(mask)-sum(mask),'sha256':hashlib.sha256(memory).hexdigest(),'maskSha256':hashlib.sha256(mask).hexdigest()} for name,(base,memory,mask) in banks.items()},'records':rows}

# Rebuild the preceding helper upload, including reused-buffer tail bytes.
helper_va=0xc03b0c0c;helper_size=2140
helper_spans=[off+helper_va-vm for vm,vsize,off,fsize in segments if vm<=helper_va and helper_va+helper_size<=vm+fsize]
assert len(helper_spans)==1
helper=kernel[helper_spans[0]:helper_spans[0]+helper_size]
assert hashlib.sha256(helper).hexdigest()=='f1404eaae9b12a4858ac7440567de187de78dc0b8704bd693ee7de94dec74211'
helper_buffer=bytearray(64);helper_wire=bytearray()
for start in range(0,len(helper),60):
 chunk=helper[start:start+60]
 helper_buffer[:4]=struct.pack('<I',len(chunk))
 helper_buffer[4:4+len(chunk)]=chunk
 helper_wire.extend(helper_buffer)
helper_wire.extend(bytes(32))
assert len(helper_wire)==2336
full_wire=helper_wire+wire
assert full_wire==(p/'full-source-wire.bin').read_bytes()==(p/'full-request-wire.bin').read_bytes()
assert helper==(p/'full-received-helper.bin').read_bytes()
assert wire[-32:]==(p/'full-handoff-frame.bin').read_bytes()
assert struct.unpack('<4I',wire[-32:-16])==(4,0,0,0x188cdb1f)
for name in banks:
 for side in ['before','after']:
  assert (p/f'full-{name}-{side}.bin').read_bytes()==bytes([0xa5])*16
assert sum(len(mask)-sum(mask) for _,_,mask in banks.values())==852
# Independently parse sequential observed command status transitions.
import re,collections
lines=(p/'full-mmio.log').read_text().splitlines()
trigger=None;observations=[];acks=[];returns=[]
for line in lines:
 match=re.search(r'ticks=(\d+).* W4 38d00008=([0-9a-f]+).* triggers=(\d+)',line)
 if match and int(match[2],16)&0x80000000:
  assert trigger is None
  trigger=(int(match[1]),int(match[3]))
 match=re.search(r'ticks=(\d+).* R4 38d00018=([0-9a-f]+).* busy=0 triggers=(\d+) complete=(\d+)',line)
 if match and trigger is not None and int(match[2],16)&1:
  assert trigger[1]==int(match[3])==int(match[4])
  elapsed=int(match[1])-trigger[0]
  assert elapsed>=600
  observations.append({'command':trigger[1],'ticksUntilFirstObservedComplete':elapsed})
  trigger=None
 if 'pc=c0432130 W4 38d00038=00000001' in line:acks.append('irq')
 if 'pc=c0431c34 W4 38d00014=00000021' in line:acks.append('status')
 match=re.search(r'genuine completeDMA consumed return pc=c0379684 r0=1 returns=(\d+).* good=1',line)
 if match:
  assert acks==['irq','status'];acks=[];returns.append(int(match[1]))
assert trigger is None and not acks
assert [o['command'] for o in observations]==list(range(1,3415))
assert returns==list(range(1,505))
histogram=collections.Counter(o['ticksUntilFirstObservedComplete'] for o in observations)
assert histogram=={600:3413,616:1}
assert [o for o in observations if o['ticksUntilFirstObservedComplete']!=600]==[{'command':663,'ticksUntilFirstObservedComplete':616}]
assert any('full main container handoff request consumed pc=c0379684 gate=1 headers=234 bodies=233 copied=117932 entry=00000000 finalCompleteDMAReturn=1' in line for line in lines)
report.update(fullWireBytes=len(full_wire),fullWireSha256=hashlib.sha256(full_wire).hexdigest(),helperBytes=len(helper),outerCanaryBytes=64,unwrittenBankBytes=852,commandResponses=len(observations),observedCompletionTicks=dict(histogram),completeDMAAcknowledgements=len(returns),timingScope='First observed completion is not a timestamp of an unobserved device transition; no early observations. One command observed at616 ticks, others600. No cause assigned to polling delay.')
(p/'root-byte-audit.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:v for k,v in report.items() if k!='records'}))
