"""Independent original-file oracle; never reads emulated guest RAM as source.

Reconstruct descriptor spans from the supplied original Mach-O mapping, using
only captured call arguments/destination mapping to identify the selected tables.
"""
from pathlib import Path
import hashlib,json,re,struct,sys
root=Path('.context/amc-first/stage-b')
run=Path(sys.argv[1]) if len(sys.argv)>1 else root/'prefix-1'
kernel=Path('web/firmware/kernel_1A543a.macho').read_bytes()
assert hashlib.sha256(kernel).hexdigest()=='42e9f3c18d4a4d5c956acdcded0c3f8f247fe3e7fc96d14cfa3aedcd77c292c9'
segments=[];off=28
for _ in range(struct.unpack_from('<I',kernel,16)[0]):
 cmd,size=struct.unpack_from('<II',kernel,off)
 if cmd==1:
  va,vlen,fo,flen=struct.unpack_from('<IIII',kernel,off+24)
  assert fo+flen<=len(kernel)
  segments.append((va,fo,flen))
 off+=size
reads={}
def raw(va,n):
 for base,fo,length in segments:
  if base<=va and va+n<=base+length:
   b=kernel[fo+va-base:fo+va-base+n];reads[va]=b.hex();return b
 raise AssertionError('Source is not original file-backed memory: '+hex(va))
def u32(va):return struct.unpack('<I',raw(va,4))[0]
trace=(run/'trace.log').read_text()
rows=re.findall(r'DESCRIPTOR (\d+) pc([0-9a-f]+) cell([0-9a-f]+) descriptor([0-9a-f]+) baseVA([0-9a-f]+) basePA([0-9a-f]+)',trace)
assert len(rows)==3
expected=bytearray(0x2c000);mask=bytearray(len(expected));footprint=bytearray(len(expected));report=[]
for index,pc,cell,desc,baseVA,basePA in rows:
 pc,cell,desc,baseVA,basePA=[int(v,16) for v in (pc,cell,desc,baseVA,basePA)]
 bank,width,banklen={0xc0277908:(0x1c000,4,0xc000),0xc0277930:(0x28000,2,0x4000),0xc027795c:(0x14000,4,0x8000)}[pc]
 assert basePA==0x22000000+bank
 data,maprec=u32(desc),u32(desc+4)
 lanes,dlist=u32(data),u32(data+4)
 maps,mlist=u32(maprec),u32(maprec+4)
 assert 0<lanes<=32 and maps==lanes
 lane_results=[];writes=0
 for lane in range(lanes):
  length,source=u32(dlist+8*lane),u32(dlist+8*lane+4)
  count,spans=u32(mlist+8*lane),u32(mlist+8*lane+4)
  assert count<=1024
  cursor=0;span_results=[]
  for seg in range(count):
   start,n=u32(spans+8*seg),u32(spans+8*seg+4)
   assert cursor+n<=length
   span_results.append({'start':start,'words':n,'sourceVA':hex(source+4*cursor)})
   for j in range(n):
    address=bank+width*((start+j)*lanes+lane) if width==4 else bank+2*(start+j)
    assert bank<=address and address+width<=bank+banklen
    assert not any(mask[address:address+width]),'Unexpected descriptor overlap'
    value=raw(source+4*cursor,4)[:width];cursor+=1;writes+=1
    expected[address:address+width]=value;mask[address:address+width]=b'\1'*width
    aligned=address & ~3
    footprint[aligned:aligned+4]=b'\1'*4
  assert cursor==length
  lane_results.append({'lane':lane,'declaredWords':length,'sourceVA':hex(source),'spans':span_results})
 report.append({'index':int(index),'descriptorVA':hex(desc),'bank':hex(bank),'lanes':lanes,'width':width,'originalGuestWrites':writes,'lanesDetail':lane_results})
actual=(run/'sram.bin').read_bytes();predicted=(run/'predicted.bin').read_bytes();written=(run/'written.bin').read_bytes()
assert len(actual)==len(expected)
assert actual==expected,'Full actual SRAM differs from original-file reconstruction'
assert (run/'expected.bin').read_bytes()==expected,'In-probe reconstruction differs'
assert predicted==mask
assert written==footprint,'Actual guest write footprint differs from descriptor store/RMW footprint'
assert sum(mask)==59622 and sum(r['originalGuestWrites'] for r in report)==17687
assert 'HOOK pcc02778b8 lrc02778b8 r000000001 ' in trace
assert 'AMC W 38500340 width4 value00000001 pcc0277998' in trace
# Explicit negative controls: each corruption must fail the entire-byte oracle.
negative=0
for offset in [0,0x14000,0x14000+4*4597,0x1c000,0x28000,0x2bfff]:
 bad=bytearray(actual);bad[offset]^=1
 assert bad!=expected;negative+=1
# Swapping two non-identical pmem lane words catches a stride/layout error.
bad=bytearray(actual);a,b=0x1c004,0x1c008
assert bad[a:a+4]!=bad[b:b+4]
bad[a:a+4],bad[b:b+4]=bad[b:b+4],bad[a:a+4]
assert bad!=expected;negative+=1
zero_offset=next(i for i in range(len(mask)) if mask[i] and actual[i]==0)
bad_coverage=bytearray(written);bad_coverage[zero_offset]=0
assert bad_coverage!=footprint;negative+=1
result={'pass':True,'scope':'Original kernel file tables/source bytes, independent of emulated RAM and C oracle','kernelSHA256':hashlib.sha256(kernel).hexdigest(),'sramSHA256':hashlib.sha256(actual).hexdigest(),'fullSRAMBytesCompared':len(expected),'descriptorBytes':sum(mask),'guestWriteFootprintBytes':sum(written),'originalGuestWrites':17687,'selectedCodecIndex':1,'originalTableLabel':'Espico_aac','negativeControls':negative,'zeroByteCoverageNegativeOffset':hex(zero_offset),'retainedRMWNeighborOffsets':[hex(i) for i in range(len(mask)) if written[i] and not mask[i]],'descriptors':report}
(run/'kernel-oracle.json').write_text(json.dumps(result,indent=2)+'\n')
(run/'kernel-source-words.json').write_text(json.dumps({hex(k):v for k,v in sorted(reads.items())},indent=2)+'\n')
print(json.dumps({k:v for k,v in result.items() if k!='descriptors'},indent=2))
