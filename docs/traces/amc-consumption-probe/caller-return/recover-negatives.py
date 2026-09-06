"""Finish only post-run artifact controls after the preserved launcher TypeError."""
from pathlib import Path
import subprocess,json,hashlib
out=Path('.context/amc-first/stage-l/run-1');sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
assert sha(out/'ordering-audit.py')=='44a78e1079f6dc5842556702ac11b68d81577ed4ddaa1af4b29d38241a63431e'
original_run=(out/'run.json').read_bytes();original_launch=(out/'launch.py').read_bytes();positive=(out/'ordering-audit.json').read_bytes();assert json.loads(positive)['passed']
assert 'TypeError' in Path('.context/amc-first/stage-l/run-1-launch.log').read_text()
artifacts=[p for p in out.iterdir() if not p.name.startswith(('negative-','recovered-')) and p.name not in ('ordering-audit.json','post-audit-recovery.json')];controls=[]
for label,target,offset,message in [('record','ordering.bin',4,'record/log bytes differ'),('queue','owned-queue.bin',123,'guest artifact differs')]:
 mutant=out/('recovered-negative-'+label);mutant.mkdir(exist_ok=False)
 for artifact in artifacts:
  if artifact.name==target:
   data=bytearray(artifact.read_bytes());data[offset]^=0x80 if label=='record' else 1;(mutant/target).write_bytes(data)
  else:(mutant/artifact.name).symlink_to(artifact.resolve(),target_is_directory=artifact.is_dir())
 logfile=out/('recovered-negative-'+label+'.log');command=['python3',str(out/'ordering-audit.py'),str(mutant)]
 with logfile.open('w') as log:r=subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,timeout=30)
 assert r.returncode!=0 and message in logfile.read_text();controls.append({'command':command,'returncode':r.returncode,'expectedFailure':message,'logSHA256':sha(logfile)})
assert (out/'run.json').read_bytes()==original_run and (out/'launch.py').read_bytes()==original_launch and (out/'ordering-audit.json').read_bytes()==positive
report={'passed':True,'guestRuns':1,'guestReplayed':False,'originalLauncherFailed':True,'originalRunReportPreserved':True,'originalLauncherPreserved':True,'positiveFrozenAuditSHA256':sha(out/'ordering-audit.py'),'positiveAuditReportSHA256':sha(out/'ordering-audit.json'),'controls':controls,'scope':'Complete the two missing artifact negatives only; no guest execution, device mutation or source/auditor repair.'}
(out/'post-audit-recovery.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
