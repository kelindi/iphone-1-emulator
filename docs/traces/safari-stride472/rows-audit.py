from pathlib import Path
import struct,mmap,hashlib,json,collections
b=Path('.context/safari-default-mbx');u32=lambda p,o=0:struct.unpack_from('<I',p,o)[0];regs=(b/'mbx-registers.bin').read_bytes();edram=(b/'edram.bin').read_bytes()
with (b/'ram.bin').open('rb') as f:
 ram=mmap.mmap(f.fileno(),0,access=mmap.ACCESS_READ)
 def mapped(va,n):
  out=bytearray();proof=[]
  while n:
   chunk=va>>22;assert chunk<8
   root=u32(regs,0x1000+chunk*4);assert root%4096==0 and 0x8000000<=root<=0x17fff000
   at=root+((va>>12)&1023)*4;pte=u32(ram,at-0x8000000);count=min(n,4096-(va&4095));pa=pte+(va&4095)
   assert pte%4096==0 and 0x8000000<=pa and pa+count<=0x18000000
   out+=ram[pa-0x8000000:pa-0x8000000+count];proof.append([va,at,pte,pa,count]);va+=count;n-=count
  return bytes(out),proof
 results=[];heads=collections.Counter()
 for off in range(0,65536,4):
  at=0xa00000-0x2000+off
  if u32(edram,at)!=0xa0060500:continue
  w=struct.unpack_from('<11I',edram,at)
  if w[2]!=0x94060760:continue
  assert w[4]&0xf8002000==0x30000000 and w[5:8]==(0x60800200,0x8000cccc,0xffffffff) and w[10]==0x70000000
  sx=(w[4]>>14)&8191;sy=w[4]&8191;x=w[8]>>16;y=w[8]&65535;x1=w[9]>>16;y1=w[9]&65535;width=x1-x;height=y1-y
  assert 0<width<=320 and 0<height<=480 and 0<=x<x1<=320 and 0<=y<y1<=480 and sx+width<=472 and sy+height<=480
  source=bytearray();target=bytearray();spans=[];tspans=[]
  for row in range(height):
   data,sp=mapped(w[3]+(sy+row)*1888+sx*4,width*4);source+=data;spans+=sp
   data,sp=mapped(w[1]+(y+row)*1280+x*4,width*4);target+=data;tspans+=sp
  # Actual mapped source/destination bytes can have changed since submission.
  results.append({'ringOffset':off,'crossesRingEnd':off+44>65536,'words':[hex(v) for v in w],'sourceXY':[sx,sy],'destination':[x,y,x1,y1],'sourceBytes':len(source),'sourceSHA256':hashlib.sha256(source).hexdigest(),'currentDestinationSHA256':hashlib.sha256(target).hexdigest(),'sourceMappings':spans,'destinationMappings':tspans})
  heads[(width,height)]+=1
 (b/'rows-audit.json').write_text(json.dumps({'passed':True,'guestExecuted':False,'commandReexecuted':False,'packets':len(results),'completeWithinRing':sum(not r['crossesRingEnd'] for r in results),'sourceStride':1888,'sourceWidthPixels':472,'destinationStride':1280,'rectangles':[[*key,value] for key,value in heads.items()],'results':results,'limit':'Historical ring/current mappings, not rejection-time telemetry or proof of triggering batch.'},indent=2)+'\n')
 print('PASS',len(results),'actual packets, whollyinside',sum(not r['crossesRingEnd'] for r in results),'all actual bounded source/destination RAM mappings; no writes')
 print('first',json.dumps(results[0]))
