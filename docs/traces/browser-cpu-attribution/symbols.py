"""Isolated final-link symbol map; run only after a coordinated build window."""
from pathlib import Path
import subprocess,shlex,json,hashlib
root=Path.cwd();out=root/'.context/jit-attribution/symbols';out.mkdir(parents=True,exist_ok=True)
cmd=subprocess.check_output(['ninja','-C','web/build','-t','commands','emu'],text=True).splitlines()[-1]
args=shlex.split(cmd);assert args[:2]==[':','&&'] and args[-2:]==['&&',':'];args=args[2:-2]
assert args[0].endswith('/emcc');args[args.index('-o')+1]=str(out/'emu.js');args.append('--emit-symbol-map')
sha=lambda b:hashlib.sha256(b).hexdigest()
inputs=[root/'web/build/CMakeFiles/emu.dir/web/emu_api.c.o',root/'web/build/CMakeFiles/emu.dir/web/jit-runtime.c.o',root/'web/build/core/libemucore.a'];before={str(p.relative_to(root)):sha(p.read_bytes()) for p in inputs};production=(root/'web/dist/emu.wasm').read_bytes()
(out/'command.json').write_text(json.dumps({'argv':args,'cwd':str(root/'web/build'),'inputSha256':before,'productionSha256':sha(production)},indent=2)+'\n')
with (out/'link.log').open('w') as log:subprocess.run(args,cwd=root/'web/build',stdout=log,stderr=subprocess.STDOUT,check=True)
assert before=={str(p.relative_to(root)):sha(p.read_bytes()) for p in inputs};assert production==(root/'web/dist/emu.wasm').read_bytes()
def sections(data):
 pos=8;result=[]
 def leb():
  nonlocal pos
  value=shift=0
  while True:
   b=data[pos];pos+=1;value|=(b&127)<<shift;shift+=7
   if b<128:return value
 while pos<len(data):
  start=pos;kind=data[pos];pos+=1;length=leb();end=pos+length;payload=data[pos:end];result.append({'id':kind,'sha256':sha(payload),'length':length,'bytes':data[start:end]});pos=end
 return result
new=(out/'emu.wasm').read_bytes();a=sections(production);b=sections(new);standardA=[r for r in a if r['id']];standardB=[r for r in b if r['id']];assert [r['bytes'] for r in standardA]==[r['bytes'] for r in standardB],'Standard WASM sections changed: do not apply this symbol map to production'
symbols=(out/'emu.symbols').read_text();proof={'passed':True,'fullWasmByteIdentical':production==new,'standardSectionsByteIdentical':True,'productionSha256':sha(production),'symbolBuildSha256':sha(new),'symbolsSha256':sha(symbols.encode()),'symbols':len(symbols.splitlines()),'sections':[{k:v for k,v in r.items() if k!='bytes'} for r in standardA]};(out/'proof.json').write_text(json.dumps(proof,indent=2)+'\n');print(json.dumps(proof))
