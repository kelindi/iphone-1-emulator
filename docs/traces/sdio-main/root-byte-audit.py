from pathlib import Path
import struct,hashlib,json
p=Path('.context/sdio-first/main-first');kernel=Path('web/firmware/kernel_1A543a.macho').read_bytes()
assert hashlib.sha256(kernel).hexdigest()=='42e9f3c18d4a4d5c956acdcded0c3f8f247fe3e7fc96d14cfa3aedcd77c292c9'
assert struct.unpack_from('<I',kernel)[0]==0xfeedface
segments=[];pos=28
for _ in range(struct.unpack_from('<I',kernel,16)[0]):
 cmd,size=struct.unpack_from('<II',kernel,pos);assert size>=8
 if cmd==1:
  vm,length,offset,filesize=struct.unpack_from('<IIII',kernel,pos+24)
  segments.append((vm,length,offset,filesize))
 pos+=size

def read_va(va,size):
 matches=[(off+va-vm,size) for vm,length,off,filesize in segments if va>=vm and va+size<=vm+filesize]
 assert len(matches)==1
 off,n=matches[0];assert off+n<=len(kernel);return kernel[off:off+n]
helper=read_va(0xc03b0c0c,2140);main=read_va(0xc0392d1c,122608)
assert hashlib.sha256(helper).hexdigest()=='f1404eaae9b12a4858ac7440567de187de78dc0b8704bd693ee7de94dec74211'
assert hashlib.sha256(main).hexdigest()=='06a5d9d1f5cc4c5209cd072bb2434058be3d52b8bebb2b022153c325f3821150'
wire=(p/'positive-request-wire.bin').read_bytes();source=(p/'positive-source-wire.bin').read_bytes();ram=(p/'positive-card-ram.bin').read_bytes()
assert wire==source and len(wire)==2880
# Reconstruct helper only from its received frame length fields, independently
# of the device model or precomputed expected-wire fixtures.
reassembled=bytearray()
for offset in range(0,2304,64):
 frame=wire[offset:offset+64];n=struct.unpack_from('<I',frame)[0]
 assert 0<n<=60;reassembled.extend(frame[4:4+n])
assert reassembled==helper==(p/'positive-received-helper.bin').read_bytes()
assert wire[2304:2336]==bytes(32)
assert wire[2336:2352]==main[:16] and wire[2352:2368]==bytes(16)
assert struct.unpack('<4I',wire[2336:2352])==(1,0xc0001000,512,0x26486be9)
assert wire[2368:]==main[16:528]
assert len(ram)==512 and ram[:508]==main[16:524] and ram[508:]==bytes(4)
assert wire[-4:]==bytes.fromhex('946e44e6')
report={'passed':True,'scope':'Root independent extraction directly from original Mach-O segments; no model or precomputed expected-wire values used for byte comparison.',
 'wireBytes':len(wire),'helperBytes':len(helper),'mainHeaderWords':list(struct.unpack('<4I',main[:16])),
 'bodyBytes':512,'copiedBytes':508,'untouchedTailBytes':4,'skippedContainerTrailerHex':wire[-4:].hex(),
 'wireSha256':hashlib.sha256(wire).hexdigest(),'cardRamSha256':hashlib.sha256(ram).hexdigest()}
(p/'root-byte-audit.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
