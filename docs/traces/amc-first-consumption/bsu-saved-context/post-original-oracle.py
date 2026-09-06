from pathlib import Path
import struct,json,hashlib,re
base=Path(__file__).parent;data=Path('.context/amc-first/stage-m/MeCCA.original').read_bytes();digest=hashlib.sha256(data).hexdigest();assert digest=='15f8f049239e64f88660aeb1ce99aa07273ccd19ca636e624ff19e9a68e7f4ec';off=28;segs=[]
for _ in range(struct.unpack_from('<I',data,16)[0]):
 cmd,n=struct.unpack_from('<II',data,off)
 if cmd==1:segs.append(struct.unpack_from('<4I',data,off+24))
 off+=n
rows=json.loads((base/'post-original.json').read_text());assert len(rows)==6
header=(base/'source/post-original.h').read_text();parsed=re.findall(r'\{0x([0-9a-f]+),\{([^}]+)\}\}',header);assert len(parsed)==6
for row,(pc,bytewords) in zip(rows,parsed):
 original=next(data[f+row['pc']-v:f+row['pc']-v+32] for v,z,f,n in segs if v<=row['pc'] and row['pc']+32<=v+n)
 assert original==bytes.fromhex(row['hex'])==bytes(int(x,16) for x in bytewords.split(',')) and int(pc,16)==row['pc']
report={'passed':True,'originalSHA256':digest,'windows':rows,'scope':'Six original MeCCA code windows agree independently with native header and external auditor fixture.'}
(base/'post-original-oracle.json').write_text(json.dumps(report,indent=2)+'\n');print('six original MeCCA post windows PASS')
