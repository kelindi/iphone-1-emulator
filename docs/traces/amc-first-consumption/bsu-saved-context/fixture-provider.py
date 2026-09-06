"""Read-only original checkpoint page extraction, never load/run a guest."""
from pathlib import Path
import mmap,struct,json
p=Path('.context/audio/ringtone-page.snap')
with p.open('rb') as f:
 m=mmap.mmap(f.fileno(),0,access=mmap.ACCESS_READ);sections={};cursor=40
 while cursor<len(m)-8:
  tag,z,n=struct.unpack_from('<4sIQ',m,cursor);assert z==0 and cursor+16+n<=len(m)-8;sections[tag]=(cursor+16,n);cursor+=16+n
  if tag==b'END ':break
 g=sections[b'GEOM'][0];base,size=struct.unpack_from('<II',m,g);c=sections[b'CPU '][0];cp15=struct.unpack_from('<16I',m,c+68);sctlr,actlr,cpacr,ttbr0,ttbr1,ttbcr,dacr=cp15[:7]
 r,n=sections[b'RAM '];np=struct.unpack_from('<Q',m,r)[0];assert np==(size+4095)//4096;cursor=r+8;pages=[]
 for i in range(np):
  cls=m[cursor];cursor+=1;assert cls<=2;pages.append((cls,cursor));cursor+=0 if cls==0 else 1 if cls==1 else min(4096,size-4096*i)
 assert cursor==r+n
 def physical(a,n):
  assert a>=base and a+n<=base+size;out=b''
  while n:
   i,off=divmod(a-base,4096);chunk=min(n,4096-off);cls,p=pages[i]
   out+=bytes(chunk) if cls==0 else bytes([m[p]])*chunk if cls==1 else m[p+off:p+off+chunk];a+=chunk;n-=chunk
  return out
 def word(a):return struct.unpack('<I',physical(a,4))[0]
 def translate(a):
  assert a>=0xc0000000 and sctlr&1;N=ttbcr&7;assert N and not ttbcr&32
  descriptor=word((ttbr1&0xffffc000)|((a>>20)<<2));domain=(descriptor>>5)&15;assert (dacr>>(domain*2))&3 in (1,3)
  kind=descriptor&3
  if kind==2:
   if descriptor&(1<<18):return (descriptor&0xff000000)|(a&0xffffff)
   return (descriptor&0xfff00000)|(a&0xfffff)
  assert kind==1;small=word((descriptor&0xfffffc00)|((a>>10)&0x3fc));assert small&3 in (2,3);return (small&0xfffff000)|(a&4095)
 def virtual(a,n):
  out=b''
  while n:
   chunk=min(n,1024-(a&1023));out+=physical(translate(a),chunk);a+=chunk;n-=chunk
  return out
 def vword(a):return struct.unpack('<I',virtual(a,4))[0]
 # Root actual input observer already identified these driver/provider values.
 driver=0xccaa2c00;provider=vword(driver+0x68);vtable=vword(provider)
 report={'scope':'Original pre-ringtone checkpoint only; not a new guest trace or proof of provider-pointer lifetime after V.','snapshotBytes':len(m),'ramBase':base,'ramSize':size,'sctlr':sctlr,'ttbr1':ttbr1,'ttbcr':ttbcr,'dacr':dacr,'driver':driver,'driverVtable':vword(driver),'provider':provider,'providerVtable':vtable,'method4b8':vword(vtable+0x4b8),'method4bc':vword(vtable+0x4bc),'method4c4':vword(vtable+0x4c4),'providerFirst32':virtual(provider,32).hex(),'vtableMethods':virtual(vtable+0x4a8,32).hex()}
 parent=vword(provider+0x50);pv=vword(parent);nc=vword(provider+0x5c);npw=vword(provider+0x64);assert nc<=16 and npw<=16
 report['parent']={'object':hex(parent),'vtable':hex(pv),'clockTarget':hex(vword(pv+0x4b4)),'powerTarget':hex(vword(pv+0x4b8)),'clockIDs':[vword(vword(provider+0x60)+4*i) for i in range(nc)],'powerIDs':[vword(vword(provider+0x68)+4*i) for i in range(npw)]}
 report['platformClients']={hex(off):{'object':hex(vword(parent+off)),'vtable':hex(vword(vword(parent+off))),'target98':hex(vword(vword(vword(parent+off))+0x98))} for off in (0x78,0x7c) if vword(parent+off)}
 for record in report['platformClients'].values():
  owner=vword(int(record['object'],16)+0x18);record['owner']=hex(owner);record['ownerVtable']=hex(vword(owner));record['actual4a4']=hex(vword(vword(owner)+0x4a4))
 report['driverRegionPhysical']={hex(off):hex(translate(vword(driver+off))) for off in (0x1cc,0x1d0,0x1d4)}
 report['driverRegions']={hex(off):hex(vword(driver+off)) for off in (0x1c4,0x1c8,0x1cc,0x1d0,0x1d4)}
 Path('.context/amc-first/stage-x/fixture-provider.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
