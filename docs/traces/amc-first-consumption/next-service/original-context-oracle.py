"""Read-only original-file oracle for the six mapped instruction windows."""
from pathlib import Path
import struct,hashlib,json,re
PINS={'web/firmware/kernel_1A543a.macho':'42e9f3c18d4a4d5c956acdcded0c3f8f247fe3e7fc96d14cfa3aedcd77c292c9','.context/amc-first/stage-m/MeCCA.original':'15f8f049239e64f88660aeb1ce99aa07273ccd19ca636e624ff19e9a68e7f4ec'}
PCS=(0xc01533c2,0x3123f7f0,0x3123cdc8,0x3123ce18,0x3123f95c,0xc0272224)
def originals():
 images=[]
 for path,digest in PINS.items():
  data=Path(path).read_bytes();assert hashlib.sha256(data).hexdigest()==digest;segs=[];o=28
  for _ in range(struct.unpack_from('<I',data,16)[0]):
   c,n=struct.unpack_from('<II',data,o)
   if c==1:segs.append(struct.unpack_from('<4I',data,o+24))
   o+=n
  images.append((data,segs))
 result={}
 for pc in PCS:
  data,segs=images[0 if pc>=0xc0000000 else 1]
  result[pc]=next(data[o+pc-v:o+pc-v+32] for v,_,o,n in segs if v<=pc and pc+32<=v+n)
 return result
if __name__=='__main__':
 b=Path(__file__).parent;rows=json.loads((b/'original-contexts.json').read_text());expected=originals();assert {r['pc']:bytes.fromhex(r['hex']) for r in rows}==expected
 header=(b/'source/original-contexts.h').read_text()
 for pc,data in expected.items():assert '{0x%08xu,{%s}}'%(pc,','.join('0x%02x'%v for v in data)) in header
 (b/'original-context-oracle.json').write_text(json.dumps({'passed':True,'scope':'Original file context bytes, not a runtime address-space observation.','files':PINS,'contexts':rows},indent=2)+'\n');print('six original instruction contexts and generated C bytes match')
