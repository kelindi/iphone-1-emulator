"""Explicit one-attempt launcher; requires separate root authorization.

Never auto-retries. Canonical C load occurs before any guest instruction; old
Python package checks immutable identity/hash/path bytes before handing them on.
"""
from pathlib import Path
import argparse,hashlib,json,struct,subprocess,time,importlib.util
import audit
HERE=Path(__file__).resolve().parent
OLD=Path('.context/sdio-first/checkpoint-v2')
MANIFEST_SHA='911b4b85c3f984c736edbb51b882c68be6ef1b8788a9b343b9b1d9eaeac11787'

def main():
 parser=argparse.ArgumentParser();parser.add_argument('mode',choices=['control','positive','no-marker']);args=parser.parse_args()
 # Only one fresh output name per reviewed mode. Existing failures are retained.
 out=HERE/'runs'/(args.mode+'-1');out.mkdir(parents=True,exist_ok=False)
 pins=json.loads((HERE/'source-pins.json').read_text())
 for name,v in pins.items():
  p=Path(name);assert p.stat().st_size==v['bytes'] and hashlib.sha256(p.read_bytes()).hexdigest()==v['sha256'],name
 if args.mode!='control':assert json.loads((HERE/'runs/control-1/audit.json').read_text())['passed']
 if args.mode=='no-marker':assert json.loads((HERE/'runs/positive-1/audit.json').read_text())['passed']
 source=OLD/'full-attempt-1/private-package/manifest.json';raw=source.read_bytes();assert hashlib.sha256(raw).hexdigest()==MANIFEST_SHA
 manifest=json.loads(raw)
 spec=importlib.util.spec_from_file_location('frozen_package',OLD/'package.py');package=importlib.util.module_from_spec(spec);spec.loader.exec_module(package)
 def bind_to_reviewed_pair(m,s):
  # Cprobe canonical validation follows; this callback only rebinds immutable
  # known073 bytes. It never claims to reconstruct or canonicalize a machine.
  assert len(m)==191881187 and hashlib.sha256(m).hexdigest()=='2cf3a7391b2972cea6e152672693b9028b4707765937b8344018d0f5929ea0d5'
  assert len(s)==246287 and hashlib.sha256(s).hexdigest()=='9b91cff0fd310e457df7850424260cd6307e39ec08467f4e0c935725953507d8'
 m,s,_=package.load(source.parent,manifest['identity'],bind_to_reviewed_pair)
 before={'mode':args.mode,'sourcePins':pins,'manifestSha256':MANIFEST_SHA,'inputMachineSha256':hashlib.sha256(m).hexdigest(),'inputSidecarSha256':hashlib.sha256(s).hexdigest(),'loadLimitSeconds':120,'guestLimitSeconds':90,'outerProcessLimitSeconds':240,'guestInstructionLimit':100000000,'scope':'One isolated continuation from unchanged073, no automatic retries or package publication'}
 (out/'attempt.json').write_text(json.dumps(before,indent=2)+'\n')
 start=time.monotonic()
 try:
  with (out/'process.log').open('wb') as log:
   result=subprocess.run([str(HERE/'probe'),args.mode,str(out)],input=struct.pack('<II',len(m),len(s))+m+s,stdout=log,stderr=subprocess.STDOUT,timeout=240)
  process={'exitCode':result.returncode,'outerSeconds':time.monotonic()-start,'outerTimeout':False}
 except subprocess.TimeoutExpired:
  process={'exitCode':None,'outerSeconds':time.monotonic()-start,'outerTimeout':True}
 (out/'process.json').write_text(json.dumps(process,indent=2)+'\n')
 assert process['exitCode']==0 and not process['outerTimeout'],process
 # Recheck immutable old package and pinned inputs; a successful run never saves
 # changed device state over the checkpoint it consumed.
 for name,v in pins.items():assert hashlib.sha256(Path(name).read_bytes()).hexdigest()==v['sha256'],name
 print(json.dumps(audit.audit(out),indent=2))
if __name__=='__main__':main()
