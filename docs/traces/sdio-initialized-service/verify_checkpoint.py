"""Read/validate the exact reviewed073 package; never executes the guest."""
from pathlib import Path
import subprocess,struct,hashlib,json,importlib.util
old=Path('.context/sdio-first/checkpoint-v2');new=Path(__file__).resolve().parent
source=old/'full-attempt-1/private-package/manifest.json'
assert hashlib.sha256(source.read_bytes()).hexdigest()=='911b4b85c3f984c736edbb51b882c68be6ef1b8788a9b343b9b1d9eaeac11787'
spec=importlib.util.spec_from_file_location('frozen_package',old/'package.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
manifest=json.loads(source.read_text())
for path,record in json.loads((old/'source-pins.json').read_text()).items():assert hashlib.sha256(Path(path).read_bytes()).hexdigest()==record['sha256']
def validate(m,s):
 result=subprocess.run([str(new/'validate')],input=struct.pack('<II',len(m),len(s))+m+s,capture_output=True,timeout=120)
 print(result.stdout.decode(),result.stderr.decode());assert result.returncode==0
m,s,r=module.load(source.parent,manifest['identity'],validate)
(new/'actual-canonical.json').write_text(json.dumps({'passed':True,'scope':'Exact immutable073 manifest and all36 original pins; unchanged Python package loader and successor C canonical loader with runtime unattached; no guest instruction executed','manifestSha256':'911b4b85c3f984c736edbb51b882c68be6ef1b8788a9b343b9b1d9eaeac11787','generation':r['generation'],'machineBytes':len(m),'sidecarBytes':len(s),'machineSha256':hashlib.sha256(m).hexdigest(),'sidecarSha256':hashlib.sha256(s).hexdigest()},indent=2)+'\n')
