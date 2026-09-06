"""Static original-byte inventory and loop address derivation. No guest/model."""
from pathlib import Path
import struct,json,hashlib
b=Path('web/firmware/kernel_1A543a.macho').read_bytes();sha=hashlib.sha256(b).hexdigest();assert sha=='42e9f3c18d4a4d5c956acdcded0c3f8f247fe3e7fc96d14cfa3aedcd77c292c9'
segments=[];off=28
for _ in range(struct.unpack_from('<I',b,16)[0]):
 cmd,n=struct.unpack_from('<II',b,off)
 if cmd==1:segments.append(struct.unpack_from('<4I',b,off+24))
 off+=n
def va(file):return next((v+file-f for v,z,f,n in segments if f<=file<f+n),None)
def read(a,n):return next(b[f+a-v:f+a-v+n] for v,z,f,s in segments if v<=a and a+n<=v+s)
helpers={0xc02779cc:'clear-context',0xc0277a0c:'restore-locked',0xc0277a9c:'swap-context',0xc0277b9c:'save-lock-unlock',0xc0277c38:'restore-lock-unlock',0xc0277cd0:'lock',0xc0277d58:'unlock',0xc0277ddc:'save-locked'}
refs={}
for address,name in helpers.items():
 needle=struct.pack('<I',address);found=[];cursor=0
 while True:
  cursor=b.find(needle,cursor)
  if cursor<0:break
  found.append({'fileOffset':cursor,'mappedVA':va(cursor),'aligned':cursor%4==0});cursor+=1
 refs[name]={'address':address,'literalOccurrences':found}
# Pin original instructions that make the initial conditional blocks execute
# before the first FIFO body. Address calculations below follow those blocks.
windows={}
for pc,n in [(0xc0277a30,48),(0xc0277e18,48),(0xc02779cc,64),(0xc0277b30,32),(0xc0277c64,36)]:windows[hex(pc)]=read(pc,n).hex()
rows=[]
for count in range(10):
 # Save b7e3c enters CMP + ADD first; then body stores [advanced,#18].
 ptr=0;index=0;save=[]
 while True:
  take=index<count;ptr+=4
  if not take:break
  index+=1;save.append(ptr+0x18)
 # Restore b7a4c similarly increments r4 in its loop condition before LDR.
 ptr=0;index=0;restore=[]
 while True:
  ptr+=4
  if not index<count:break
  restore.append(ptr+0x18);index+=1
 assert save==restore==[0x1c+4*i for i in range(count)]
 rows.append({'fifoWords':count,'saveOffsets':save,'restoreOffsets':restore,'preservesCountAt3c':all(x<0x3c for x in save),'withinDeclaredEightWordArray':count<=8})
report={'scope':'Static source/literal inventory. Literal presence alone does not prove an executed call; absence of a literal does not exclude computed addressing.','kernelSHA256':sha,'helpers':refs,'loopOriginalWindows':windows,'effectiveFIFOLoops':rows,'contextLayout':{'registerWords':[0,4,8,12,16,20,24],'fifoWords':[28,32,36,40,44,48,52,56],'count':60,'bytes':64}}
p=Path('.context/amc-first/stage-w/context-source-audit.json');p.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:len(v['literalOccurrences']) for k,v in refs.items()}))
