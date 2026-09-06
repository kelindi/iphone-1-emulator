"""Independent file/sample-table, original-kernel symbol, and captured-buffer oracle.
No decoder, guest execution, firmware mutation, or modeled readiness.
"""
from pathlib import Path
import hashlib,json,re,struct,sys
run=Path(sys.argv[1]); media=Path(sys.argv[2]).read_bytes()
kernel=Path('web/firmware/kernel_1A543a.macho').read_bytes()
sha=lambda b:hashlib.sha256(b).hexdigest()
assert sha(kernel)=='42e9f3c18d4a4d5c956acdcded0c3f8f247fe3e7fc96d14cfa3aedcd77c292c9'
assert sha(media)=='56dece4e074bf622e0eb815331c65fb6c4e653e33b827e9a1ef30c4e0063136e'
be=lambda b,o:struct.unpack_from('>I',b,o)[0]
le=lambda b,o:struct.unpack_from('<I',b,o)[0]
segments=[]; symbols={}; pos=28
for _ in range(le(kernel,16)):
 cmd,size=struct.unpack_from('<II',kernel,pos)
 if cmd==1:
  va,vs,fo,fs=struct.unpack_from('<IIII',kernel,pos+24);segments.append((va,fo,fs))
 if cmd==2:
  so,n,st,sz=struct.unpack_from('<IIII',kernel,pos+8); strings=kernel[st:st+sz]
  for j in range(n):
   si,typ,sect,desc,val=struct.unpack_from('<IBBHI',kernel,so+12*j)
   symbols[val]=strings[si:strings.find(b'\0',si)].decode('ascii','replace')
 pos+=size
assert symbols[0xc01929b4]=='__ZTV24IOBufferMemoryDescriptor'
assert symbols[0xc0149b08]=='__ZN18IOMemoryDescriptor18getPhysicalAddressEv'
assert symbols[0xc0147bfe]=='__ZN25IOGeneralMemoryDescriptor18getPhysicalSegmentEmPm'
def original(va,n):
 for base,offset,size in segments:
  if base<=va and va+n<=base+size:return kernel[offset+va-base:offset+va-base+n]
 raise AssertionError('non-file kernel span')
# Source field accessors independently pin getBytesNoCopy / getCapacity behavior.
assert original(0xc0143398,4)==bytes.fromhex('c06e7047')
assert symbols[0xc01433f0]=='__ZN24IOBufferMemoryDescriptor14getBytesNoCopyEv'
def table_map(b):
 found={}
 def visit(begin,end):
  p=begin
  while p<end:
   assert p+8<=end; size=be(b,p);typ=b[p+4:p+8];assert size>=8 and p+size<=end
   if typ in [b'moov',b'trak',b'mdia',b'minf',b'stbl']:visit(p+8,p+size)
   else:
    if typ in [b'stsz',b'stsc',b'stco',b'stsd',b'mdat']:assert typ not in found;found[typ]=(p,size)
   p+=size
 visit(0,len(b));return found
atoms=table_map(media)
p,size=atoms[b'stsz'];assert be(media,p+12)==0
sample_sizes=[be(media,p+20+4*i) for i in range(be(media,p+16))]
assert p+20+4*len(sample_sizes)==p+size
p,size=atoms[b'stco'];chunks=[be(media,p+16+4*i) for i in range(be(media,p+12))]
p,size=atoms[b'stsc'];entries=[struct.unpack_from('>III',media,p+16+12*i) for i in range(be(media,p+12))]
assert entries[0][0]==1 and all(a[0]<b[0] for a,b in zip(entries,entries[1:]))
locations=[];index=0;mp,ms=atoms[b'mdat']
for chunk,address in enumerate(chunks,1):
 entry=next(e for e in reversed(entries) if e[0]<=chunk);assert entry[2]==1
 for _ in range(entry[1]):
  n=sample_sizes[index];assert mp+8<=address and address+n<=mp+ms
  locations.append(address);index+=1;address+=n
assert index==len(sample_sizes)
stsd,_=atoms[b'stsd'];assert media[stsd+20:stsd+24]==b'mp4a'
files=['input-rounded.bin','input-virtual.bin','input-capacity.bin','input-final.bin','packet-data.bin','input-driver-prefix.bin','input-descriptor-prefix.bin','packet-descriptor-prefix.bin','input-vtable-prefix.bin']
data={n:(run/n).read_bytes() for n in files};trace=(run/'trace.log').read_text()
def validate(d,t):
 m=re.search(r'INPUT driver([0-9a-f]+) index(\d+) descriptor([0-9a-f]+) vtable([0-9a-f]+) addressMethod([0-9a-f]+) packetDescriptor([0-9a-f]+) physical([0-9a-f]+) bytes(\d+) words(\d+) encrypted(\d+)',t);assert m
 driver,index,desc,vt,method,packet,pa=[int(m.group(i),10 if i==2 else 16) for i in range(1,8)]
 n,w,encrypted=map(int,m.group(8,9,10));assert 0<=index<3 and not encrypted and n>0 and w==(n+3)//4
 obj=d['input-driver-prefix.bin'];assert le(obj,0x110+index*4)==desc and le(obj,0x128+index*4)==packet
 descriptor=d['input-descriptor-prefix.bin'];pd=d['packet-descriptor-prefix.bin'];assert le(descriptor,0)==vt==0xc01929b4
 assert d['input-vtable-prefix.bin']==original(vt,0x104) and le(d['input-vtable-prefix.bin'],0x100)==method==0xc0147bff
 assert le(pd,0)==vt
 assert le(descriptor,0x1c)==le(descriptor,0x6c)==len(d['input-capacity.bin'])==0x10000
 assert le(pd,0x1c)==le(pd,0x6c)==len(d['packet-data.bin'])==0x492c
 va=le(descriptor,0x68 if le(descriptor,0x10)&0xf0==0x20 else 0x34)
 maps=re.findall(r'INPUTMAP input va([0-9a-f]+) pa([0-9a-f]+) offset(\d+)',t)
 assert [(int(a,16),int(b,16),int(o)) for a,b,o in maps]==[(va+o,pa+o,o) for o in range(0,4*w,4096)]
 payload=d['input-rounded.bin'];assert len(payload)==4*w and payload==d['input-virtual.bin']==d['input-final.bin']==d['input-capacity.bin'][:len(payload)]
 md=d['packet-data.bin'];count=le(md,0);assert count and count<=(len(md)-4)//8 and count<=len(sample_sizes)
 cursor=0;complete=0;partial=[]
 for i in range(count):
  off,size=struct.unpack_from('<II',md,4+8*i);assert off==cursor and size==sample_sizes[i]
  assert off+size<=len(d['input-capacity.bin'])
  present=max(0,min(size,len(payload)-off))
  assert payload[off:off+present]==media[locations[i]:locations[i]+present]
  if present==size:complete+=1
  elif present:partial.append({'packetIndex1':i+1,'offset':off,'packetBytes':size,'presentBytes':present,'absentBytes':size-present})
  cursor+=size
 assert not any(md[4+8*count:])
 assert locations[0]==51240 and payload==media[locations[0]:locations[0]+len(payload)]
 assert not any(d['input-capacity.bin'][len(payload):])
 assert 'PASS unaccepted input request physical%08x bytes%d words%d'%(pa,n,w) in t
 assert 'pcc02780e4 status00000100 state0 prefix6' in t and 'FINAL result4 ' in t
 return {'physicalAddress':hex(pa),'virtualAddress':hex(va),'requestBytes':n,'roundedWords':w,'fileOffset':locations[0],'metadataPacketCount':count,'metadataBytesDescribed':cursor,'completePacketsInRequest':complete,'partialPackets':partial,'bufferRemainder':'All bytes after this request are zero; no52-byte continuation is present','payloadSHA256':sha(payload),'packetMetadataSHA256':sha(md),'inputDescriptor':hex(desc),'packetDescriptor':hex(packet),'inputMappings':[{'va':a,'pa':b,'offset':int(o)} for a,b,o in maps]}
report=validate(data,trace);negative=[]
for name,offset in [('input-rounded.bin',0),('input-final.bin',100),('input-virtual.bin',8192),('packet-data.bin',0),('packet-data.bin',4),('packet-data.bin',8),('input-descriptor-prefix.bin',0),('input-descriptor-prefix.bin',0x6c),('input-driver-prefix.bin',0x110),('input-vtable-prefix.bin',0x100),('input-capacity.bin',16384)]:
 bad=dict(data);b=bytearray(bad[name]);b[offset]^=1;bad[name]=bytes(b)
 try:validate(bad,trace)
 except (AssertionError,IndexError,struct.error):negative.append(name+':'+str(offset))
 else:raise AssertionError('negative accepted '+name)
for old,new in [('INPUTMAP input vaedc4f000 pa14a8c000','INPUTMAP input vaedc4f000 pa14a8d000'),('status00000100 state0 prefix6','status00000300 state0 prefix6')]:
 assert old in trace
 try:validate(data,trace.replace(old,new))
 except AssertionError:negative.append(old)
 else:raise AssertionError('negative trace accepted')
report.update({'pass':True,'scope':'Captured original guest input only; transfer remains unaccepted, no PCM','originalMediaSHA256':sha(media),'originalKernelSHA256':sha(kernel),'mediaFileBytes':len(media),'mediaSampleCount':len(sample_sizes),'negativeControls':negative,'artifacts':{n:sha(b) for n,b in data.items()}})
(run/'input-oracle.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
