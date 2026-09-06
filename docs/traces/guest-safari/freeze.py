"""Pin the concrete isolated native probe and exact current browser source.
Run once after installation; hardlinks preserve space, never write through them.
"""
import hashlib,json,os
from pathlib import Path
B=Path(__file__).resolve().parent
def digest(p):
 h=hashlib.sha256()
 with Path(p).open('rb')as f:
  for c in iter(lambda:f.read(1048576),b''):h.update(c)
 return h.hexdigest()
site=B/'site';site.mkdir()
names=['index.html','phone.css','app.js','assets.js','storage.js','checkpoint-worker.js','emu-worker.js','seed-jit.js','manifest.json','icon.svg','service-worker.js','dist/emu.js','dist/emu.wasm','jit/abi.json','jit/regions.json','jit/seed.wasm']
projection={}
for name in names:
 source=Path('web')/name;target=site/name;target.parent.mkdir(parents=True,exist_ok=True);os.link(source,target);projection[name]={'sha256':digest(source),'bytes':source.stat().st_size}
(B/'site-pins.json').write_text(json.dumps(projection,indent=2)+'\n')
paths=[B/n for n in ['server.c','http.c','http.h','build.sh','iphone1-local-http','org.iphone1.qualification.http.plist','rootfs_work.py','prepare_image.py','probe.c','iphone1-original.c','peek.c','peek.h','identity.h','probe','watch-pcs.json','snapshot_transport.py','test_snapshot_transport.c','test_snapshot_transport','test_transport.py','run_native.py','request_capture.py','browser.mjs','freeze.py','installed/server.img','installed/server.img.json','site-pins.json']]
paths += [Path('.context/build-rtc-release/core/libemucore.a'),Path('web/firmware/kernel_1A543a.macho'),Path('web/firmware/devicetree_m68ap.bin'),Path('.context/legacy-upgrade/shutdown/disk.img')]
pins={str(p.resolve()):digest(p)for p in paths}
with (B/'source-pins.json').open('x')as f:json.dump(pins,f,indent=2);f.write('\n')
print(json.dumps({'sourcePinsSHA256':digest(B/'source-pins.json'),'probeSHA256':digest(B/'probe'),'imageSHA256':digest(B/'installed/server.img'),'files':len(pins),'projectionFiles':len(projection)},indent=2))
