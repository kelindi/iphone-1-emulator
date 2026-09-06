"""Pin actual original UserClient table and complete helper prologue/epilogue."""
from pathlib import Path
import struct,hashlib,json,sys
b=Path('web/firmware/kernel_1A543a.macho').read_bytes();sha=lambda data:hashlib.sha256(data).hexdigest()
assert sha(b)=='42e9f3c18d4a4d5c956acdcded0c3f8f247fe3e7fc96d14cfa3aedcd77c292c9'
segments=[];off=28
for _ in range(struct.unpack_from('<I',b,16)[0]):
 cmd,size=struct.unpack_from('<II',b,off)
 if cmd==1:segments.append(struct.unpack_from('<IIII',b,off+24))
 off+=size

def read(va,size):
 for base,_,offset,count in segments:
  if base<=va and va+size<=base+count:return b[offset+va-base:offset+va-base+size]
 raise AssertionError('original VA unavailable')
items={}
for label,va,count in [('helperPrologue',0xc0275e70,20),('sessionReturnEpilogue',0xc027602c,36),('userClientMethodSelector',0xc0276c90,24),('method2',0xc0282aec,24),('method3',0xc0282b04,24),('explicitStartMethod',0xc0272224,64),('serviceWrapper',0xc0277284,52)]:
 data=read(va,count);items[label]={'va':f'{va:08x}','bytes':count,'hex':data.hex(),'sha256':sha(data),'words':[f'{v:08x}' for v in struct.unpack('<'+'I'*(count//4),data)]}
assert items['helperPrologue']['words']==['e92d40f0','e3510002','e28d700c','e92d0d00','e24dd004']
assert items['sessionReturnEpilogue']['words'][1]=='0a000002' #6030 BEQ6040
assert items['sessionReturnEpilogue']['words'][-1]=='e8bd80f0' #604c POP saved PC
assert struct.unpack_from('<I',read(0xc0282aec,24),8)[0]==0xc0275e70
assert struct.unpack_from('<I',read(0xc0282b04,24),8)[0]==0xc0272224
if __name__=='__main__':
 out=Path(sys.argv[1]) if len(sys.argv)>1 else Path('.context/amc-first/stage-m/original-return-oracle.json');out.write_text(json.dumps({'passed':True,'kernelSHA256':sha(b),'items':items,'savedReturnOffsetFromR7':4,'scope':'Original bytes establish available methods and saved-frame layout, not the actual observed caller.'},indent=2)+'\n');print(out)
