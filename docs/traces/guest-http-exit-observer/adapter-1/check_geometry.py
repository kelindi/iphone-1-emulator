"""Artifact-only native/screenshot geometry proof; no emulator execution."""
from pathlib import Path
import ast,hashlib,json,struct,zlib
B=Path(__file__).resolve().parent
fn=next(n for n in ast.parse(Path('.context/guest-safari/audit.py').read_text()).body if isinstance(n,ast.FunctionDef)and n.name=='png')
exec(compile(ast.Module(body=[fn],type_ignores=[]),'<retained PNG decoder>','exec'))
paths=[B/'preflight-1/initial.ppm',Path('.context/guest-http-default/browser-run-1/01-listener-restored-guest.png'),Path('.context/guest-http-default/native-run-1/frame.17.png')]
p=paths[0].read_bytes();assert p[:15]==b'P6\n320 480\n255\n';p=p[15:];assert len(p)==460800
w,h,q=png(paths[1]);assert(w,h)==(320,481);cropped=q[960:];assert cropped==p
w,h,n=png(paths[2]);assert(w,h)==(320,480)
region=lambda a:b''.join(a[(y*320+35)*3:(y*320+285)*3]for y in range(180,350))
assert region(n)==region(p)
assert region(q[:460800])!=region(p)
assert region(bytes(460800))!=region(p)
result={'passed':True,'scope':'Artifact comparison only; no new guest execution','files':{str(x):hashlib.sha256(x.read_bytes()).hexdigest()for x in paths},'browserGeometry':[320,481],'nativeGeometry':[320,480],'projection':'drop only first screenshot row; no resampling','fullCroppedBrowserRGBBytesExact':len(p),'native17AlertRegion':[35,180,250,170],'native17AlertRegionBytesExact':len(region(p)),'wrongOffsetRejected':True,'blankRejected':True}
(B/'geometry-audit.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
