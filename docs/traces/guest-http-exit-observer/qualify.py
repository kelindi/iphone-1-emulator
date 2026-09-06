"""Source-only symbol/code qualification of exact existing images; no guest execution."""
from pathlib import Path
import sys,json,hashlib,struct
sys.path.insert(0,'tools/legacy_services/guest_http')
from macho_audit import parse
import capstone
B=Path(__file__).resolve().parent
inputs={'kernel':(Path('web/firmware/kernel_1A543a.macho'),'42e9f3c18d4a4d5c956acdcded0c3f8f247fe3e7fc96d14cfa3aedcd77c292c9'),'own':(Path('.context/guest-http-default/build-quiet-2/iphone1-local-http'),'bfcc524cae77250b3d3c8d754e7bdfe328734395a0d625fa9735f00ccdd1a19f'),'libSystem':(Path('.context/guest-hello/original-readonly/usr__lib__libSystem.B.dylib'),'6243ad0b202fa364ac7b243f0416013f81416e32abb3c69e30c6b020250448e2')}
wanted={'kernel':['_exit','_exit1','_proc_exit','_psignal','_postsig','_sigexit_locked','_sosetopt','_accept','_poll'],'own':['_start','_logline','_number','_nonblock','_waitfd'],'libSystem':['__exit','___error','_accept','_poll','_setsockopt','_close','_write','_read']}
report={'scope':'Original file addresses and N_ARM_THUMB_DEF metadata; libSystem addresses require actual relocated pointer and executed code binding before a runtime hook can accept them.','images':{}}
for key,(p,sha) in inputs.items():
 a=parse(p);b=a[0];assert hashlib.sha256(b).hexdigest()==sha;syms={s['name']:s for s in a[3] if s['value']};rows=[]
 def bytes_at(va,n):
  for seg,v,vs,off,fs,prot in a[5]:
   if v<=va and va+n<=v+fs:return off+va-v,b[off+va-v:off+va-v+n]
  raise ValueError('unbacked original VA')
 for name in wanted[key]:
  sym=syms[name];va=sym['value']&~1;thumb=bool(sym['description']&8)
  size=4096 if name in ['_start','_sosetopt'] else 512
  next_va=min([s['value']&~1 for s in a[3] if (s['value']&~1)>va]+[va+size]);size=min(size,next_va-va)
  off,code=bytes_at(va,size);_,window=bytes_at(va,32)
  row={'name':name,'va':hex(va),'mode':'Thumb' if thumb else 'ARM','nDesc':sym['description'],'fileOffset':off,'entry32Hex':window.hex(),'entry32SHA256':hashlib.sha256(window).hexdigest(),'decodedBytes':len(code)};rows.append(row)
  md=capstone.Cs(capstone.CS_ARCH_ARM,capstone.CS_MODE_THUMB if thumb else capstone.CS_MODE_ARM)
  lines=[]
  for i in md.disasm(code,va):lines.append(f'{i.address:08x} {i.bytes.hex():10s} {i.mnemonic:8s} {i.op_str}')
  (B/f'{key}-{name.lstrip("_")}.txt').write_text('\n'.join(lines)+'\n')
 report['images'][key]={'path':str(p),'sha256':sha,'hooks':rows}
(B/'addresses.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
