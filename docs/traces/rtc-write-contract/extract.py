"""Read-only original OS1 RTC call/byte evidence; no emulator or Apple edits."""
from pathlib import Path
import hashlib,json,struct
import capstone
binary=Path('web/firmware/kernel_1A543a.macho').read_bytes()
assert hashlib.sha256(binary).hexdigest()=='42e9f3c18d4a4d5c956acdcded0c3f8f247fe3e7fc96d14cfa3aedcd77c292c9'
segments=[];offset=28
for _ in range(struct.unpack_from('<I',binary,16)[0]):
    command,size=struct.unpack_from('<II',binary,offset)
    assert size>=8 and offset+size<=len(binary)
    if command==1:
        va,vsize,fo,fsize=struct.unpack_from('<4I',binary,offset+24)
        assert fo+fsize<=len(binary)
        segments.append((va,fo,fsize))
    offset+=size
def raw(va,n):
    matches=[binary[fo+va-base:fo+va-base+n] for base,fo,size in segments if base<=va and va+n<=base+size]
    assert len(matches)==1
    return matches[0]
def word(va):return struct.unpack('<I',raw(va,4))[0]
class_name=b'ApplePCF50635PMURTC\0'
assert raw(0xc0400f30,len(class_name))==class_name
vtable=0xc0402348
assert word(vtable+0x4a8)==0xc03ff8a0
assert word(vtable+0x4ac)==0xc03ff978
assert word(vtable+0x4bc)==0xc03ff964
assert raw(0xc0402888,8)==struct.pack('<II',0xffff0000,0xc03ffa20)
assert word(0xc03ff81c)==vtable
ranges=[('rtc-constructor',0xc03ff7e0,0x34,False),
        ('base-get-gmt',0xc015dea4,0x20,True),
        ('base-set-gmt',0xc015dec4,0x20,True),
        ('read-current-date',0xc03ff8a0,0xc0,False),
        ('set-gmt-override',0xc03ff964,0x10,False),
        ('set-current-date',0xc03ff978,0x9c,False),
        ('queue-date',0xc03ffa20,0x3c,False),
        ('write-date',0xc03ffa60,0xd0,False)]
out=Path('docs/traces/rtc-write-contract');out.mkdir(parents=True,exist_ok=True)
rows=[]
for name,va,size,thumb in ranges:
    md=capstone.Cs(capstone.CS_ARCH_ARM,capstone.CS_MODE_THUMB if thumb else capstone.CS_MODE_ARM)
    ops=[{'address':hex(i.address),'bytes':i.bytes.hex(),'mnemonic':i.mnemonic,'operands':i.op_str} for i in md.disasm(raw(va,size),va)]
    assert sum(len(bytes.fromhex(i['bytes'])) for i in ops)==size
    rows.append({'name':name,'address':hex(va),'size':size,'thumb':thumb,'sha256':hashlib.sha256(raw(va,size)).hexdigest(),'instructions':ops})
    (out/(name+'.txt')).write_text('\n'.join(f"{i['address']} {i['bytes']:8} {i['mnemonic']:8} {i['operands']}" for i in ops)+'\n')
result={'passed':True,'scope':'Static original-file vtable and call-path evidence, not actual RTC-write execution or correct guest date','kernelSHA256':hashlib.sha256(binary).hexdigest(),'class':'ApplePCF50635PMURTC','vtable':hex(vtable),'getCurrentOffset':'0x4a8','setCurrentOffset':'0x4ac','queuedMemberWords':['0xffff0000','0xc03ffa20'],'ranges':rows}
(out/'report.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({k:v for k,v in result.items() if k!='ranges'}))
