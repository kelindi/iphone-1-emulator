"""Reuse reviewed two-file preservation machinery with explicit own Photos paths."""
from pathlib import Path
import hashlib,json,importlib.util
B=Path('.context/photo-bootstrap'); own=Path('tools/legacy_services/photo_bootstrap').resolve()
p=Path('tools/legacy_services/guest_http/install.py').resolve()
spec=importlib.util.spec_from_file_location('photo_copy_installer',p)
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
m.PROGRAM_PATH='/usr/libexec/iphone1-photo-bootstrap'
m.PLIST_NAME='com.iphone1.photo-bootstrap.plist'
m.PLIST_PATH='/System/Library/LaunchDaemons/'+m.PLIST_NAME
m.HERE=own
report=json.loads((B/'build-1/build.json').read_text())
assert report['passed'] and report['audit']['passed'] and report['programName']=='iphone1-photo-bootstrap'
for path,expected in report['sourceSHA256'].items():
 path=Path(path) if Path(path).is_absolute() else p.parent/path
 assert hashlib.sha256(path.read_bytes()).hexdigest()==expected,str(path)
source=Path('.context/graphics-speed/rootfs-mbx2d.img')
result=m.install(source,B/'installed/photos.img','73d0d2a0ed4564dc16c67c7c6efc167725d2833461cd47919faa676595c0ded9',B/'build-1/iphone1-photo-bootstrap',report['audit']['sha256'])
print(json.dumps(result,indent=2))
