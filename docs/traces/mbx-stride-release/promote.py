"""Promote the reviewed MBX-only local release after both browser gates."""
from pathlib import Path
import hashlib,json,os
b=Path('.context/mbx-stride-integration'); web=Path('web')
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
assert json.loads((b/'upgrade-v2/run-1/controller.json').read_text())['passed']
assert json.loads((b/'final-release/run-1/controller.json').read_text())['passed']
assert json.loads((b/'final-release/manual-frame-review.json').read_text())['passed']
pins=json.loads((b/'final-release/source-pins.json').read_text())
previous=b/'previous';candidate=b/'candidate'
for name,h in pins.items():
 assert sha(candidate/name)==h,name
 assert sha(web/name)==sha(previous/name), 'Live file differs from reviewed previous package: '+name
files=[name for name in pins if name!='service-worker.js']
template=(web/'service-worker.template.js').read_text();digest=hashlib.sha256(template.encode())
# This ordering is part of the maintained offline build contract.
ordered=['index.html','phone.css','icon.svg','app.js','storage.js','checkpoint-worker.js','assets.js','emu-worker.js','manifest.json','dist/emu.js','dist/emu.wasm','seed-jit.js','jit/seed.wasm','jit/regions.json','jit/abi.json']
for name in ordered:
 digest.update(name.encode());digest.update((candidate/name).read_bytes())
version=digest.hexdigest()[:24]
hashes={name:sha(candidate/name) for name in ordered}
sw=template.replace('__VERSION__',version).replace('__FILES__',json.dumps(hashes)).encode()
assert sw==(candidate/'service-worker.js').read_bytes()
assert [name for name in files if sha(web/name)!=pins[name]]==['dist/emu.wasm']
receipt={'before':{name:sha(web/name) for name in pins},'generation':version,'changed':['dist/emu.wasm','service-worker.js'],'scope':'Local default web build; no public deployment'}
for name in receipt['changed']:
 target=web/name; temporary=target.with_name(target.name+'.stride472-staged')
 with temporary.open('xb') as f:
  f.write((candidate/name).read_bytes());f.flush();os.fsync(f.fileno())
 os.replace(temporary,target)
for name,h in pins.items():assert sha(web/name)==h,name
receipt['after']={name:sha(web/name) for name in pins};receipt['passed']=True
(b/'promotion.json').write_text(json.dumps(receipt,indent=2)+'\n')
print(json.dumps({'passed':True,'generation':version,'changed':receipt['changed']}))
