"""Independent original-MachO oracle; no endpoint/model source is imported."""
from pathlib import Path
import struct,hashlib,json
p=Path('.context/sdio-first/main-full-plan')
kernel=Path('web/firmware/kernel_1A543a.macho').read_bytes()
assert hashlib.sha256(kernel).hexdigest()=='42e9f3c18d4a4d5c956acdcded0c3f8f247fe3e7fc96d14cfa3aedcd77c292c9'
segments=[];pos=28
for _ in range(struct.unpack_from('<I',kernel,16)[0]):
    command,size=struct.unpack_from('<II',kernel,pos)
    if command==1:
        va,vs,fo,fs=struct.unpack_from('<4I',kernel,pos+24);segments.append((va,fo,fs))
    pos+=size
def read(va,size):
    matches=[kernel[o+va-v:o+va-v+size] for v,o,n in segments if v<=va and va+size<=v+n]
    assert len(matches)==1 and len(matches[0])==size
    return matches[0]
main=read(0xc0392d1c,122608);helper=read(0xc03b0c0c,2140)
scratch=bytearray(512);known=bytearray(512);known[:32]=bytes([1])*32
wire=bytearray();records=[];frames=[]
banks={0:bytearray(0x17000),0xc0001000:bytearray(0x6000)}
masks={a:bytearray(len(b)) for a,b in banks.items()}
offset=0
def frame(logical,kind,source_offset):
    n=len(logical);physical=(n+31)&~31
    scratch[:n]=logical;known[:n]=bytes([1])*n
    assert all(known[:physical]),'unknown reused buffer byte would be transmitted'
    frames.append(dict(kind=kind,sourceOffset=source_offset,logical=n,physical=physical,
                       wireOffset=len(wire),paddingHex=scratch[n:physical].hex()))
    wire.extend(scratch[:physical])
while offset<len(main):
    header=main[offset:offset+16];command,destination,length,opaque=struct.unpack('<4I',header)
    frame(header,'header',offset)
    record=dict(sourceOffset=offset,command=command,destination=hex(destination),logicalBody=length)
    offset+=16
    if command==4:
        assert offset==len(main) and destination==length==0
        records.append(record);break
    assert command==1 and 4<=length<=512
    body=main[offset:offset+length];assert len(body)==length
    frame(body,'body',offset)
    n=length-4
    valid=[base for base,b in banks.items() if base<=destination and destination+n<=base+len(b)]
    assert len(valid)==1
    base=valid[0];start=destination-base
    assert not any(masks[base][start:start+n]),'original records unexpectedly overlap'
    banks[base][start:start+n]=body[:n];masks[base][start:start+n]=bytes([1])*n
    record.update(copied=n,ignoredTrailerHex=body[-4:].hex());records.append(record)
    offset+=length
assert len(records)==234 and len(frames)==467 and len(wire)==126368
assert sum(map(sum,masks.values()))==117932
assert sum(masks[0])==93556 and sum(masks[0xc0001000])==24376
assert masks[0]==bytes([1])*0x16d74+bytes(0x17000-0x16d74)
assert masks[0xc0001000]==bytes([1])*0x5f38+bytes(0x6000-0x5f38)
for name,data in [('expected-main-wire.bin',wire),('original-main.bin',main),
                  ('expected-low.bin',banks[0]),('expected-high.bin',banks[0xc0001000]),
                  ('expected-low-mask.bin',masks[0]),('expected-high-mask.bin',masks[0xc0001000])]:
    (p/name).write_bytes(data)
report=dict(scope='Original MachO bytes + original memcpy/rounding flow; independent of semantic implementation',
            mainSha256=hashlib.sha256(main).hexdigest(),helperSha256=hashlib.sha256(helper).hexdigest(),
            mainBytes=len(main),mainWireBytes=len(wire),mainWireSha256=hashlib.sha256(wire).hexdigest(),
            copiedBytes=117932,unwrittenMappedBytes=852,records=records,frames=frames,
            banks={hex(a):dict(bytes=len(b),written=sum(masks[a]),sha256=hashlib.sha256(b).hexdigest()) for a,b in banks.items()})
(p/'source-oracle.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS:234original records/467frames/126368wire/117932stored bytes/852mapped guards; no unknown padding')
