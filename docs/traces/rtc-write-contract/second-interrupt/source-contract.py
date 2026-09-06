from pathlib import Path
import struct,hashlib,re,base64,json,xml.etree.ElementTree as ET
b=Path('web/firmware/kernel_1A543a.macho').read_bytes();assert hashlib.sha256(b).hexdigest()=='42e9f3c18d4a4d5c956acdcded0c3f8f247fe3e7fc96d14cfa3aedcd77c292c9'
segs=[];p=28
for _ in range(struct.unpack_from('<I',b,16)[0]):
 c,n=struct.unpack_from('<II',b,p)
 if c==1:
  va,vs,fo,fs=struct.unpack_from('<4I',b,p+24);segs.append((va,fo,fs))
 p+=n
def raw(va,n):
 a=[b[f+va-v:f+va-v+n] for v,f,s in segs if v<=va and va+n<=v+s];assert len(a)==1;return a[0]
def word(va):return struct.unpack('<I',raw(va,4))[0]
assert word(0xc014093c)==0xc0191d6c
assert word(0xc0191d6c+0xb0)==0xc0140a9d and word(0xc0191d6c+0xb4)==0xc0140ac5
assert raw(0xc0402898,8)==struct.pack('<II',0xffff0000,0xc03ffa60)
assert word(0xc040001c)==0xc0402898 and word(0xc0400018)==0xc0140a21
assert raw(0xc03fff80,24)==bytes.fromhex('0600a0e10620a0e10130a0e334ff2fe1000050e3900086e5')
# XML proof comes directly from the original file, including its numeric IRQ data.
m=re.search(rb'<key>ApplePCF50635PMURTC</key>(<dict ID="822">.*?</dict>)',b);assert m
node=ET.fromstring(m[1]);children=list(node);values={children[i].text:children[i+1] for i in range(0,len(children),2)}
assert values['IOClass'].text=='ApplePCF50635PMURTC'
specifiers=[struct.unpack('<I',base64.b64decode(v.text))[0] for v in values['IOInterruptSpecifiers']];assert specifiers==[6,7]
log=Path('.context/rtc-contract/event-snapshots.log').read_text();lines=[s for s in log.splitlines() if s.startswith('RTC_OBJECT')];assert len(lines)==2
records=[]
for line in lines:
 data=re.search(r'vtable([0-9a-f]+) fields88=([0-9a-f]+) secondSource([0-9a-f]+) bytes=([0-9a-f]+) INT1([0-9a-f]+) MASK1([0-9a-f]+)',line);assert data
 vt,fields,source,event,irq,mask=data.groups();fields=bytes.fromhex(fields);event=bytes.fromhex(event)
 assert int(vt,16)==0xc0402348 and struct.unpack_from('<I',event)[0]==0xc0191d6c and struct.unpack_from('<I',event,16)[0]==0xc03ffa60
 records.append({'secondSource':source,'pendingFlag':fields[0x9c-0x88],'dateBytes':fields[12:20].hex(),'INT1':int(irq,16),'INT1MASK':int(mask,16)})
assert records[0]['secondSource']==records[1]['secondSource'] and [r['pendingFlag'] for r in records]==[0,1]
assert [r['INT1MASK'] for r in records]==[0xf0,0x70] and all(r['INT1']==0 for r in records)
r={'passed':True,'scope':'Original construction/vtable/IRQ mapping plus actual immutable export state. Causal periodic-interrupt candidate not yet run.','originalIRQSpecifiers':specifiers,'dateCallbackProviderIndex':1,'dateCallbackProviderIRQ':7,'enableMethod':'c0140a9d','disableMethod':'c0140ac5','actualExports':records,'personalityXMLFileOffset':m.start(1),'personalityXMLSHA256':hashlib.sha256(m[1]).hexdigest(),'secondaryFamilyRegisterSource':'https://raw.githubusercontent.com/torvalds/linux/v6.12/include/linux/mfd/pcf50633/core.h'}
Path('.context/rtc-second/source-contract.json').write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r))
