"""One reviewed StageK run only when explicitly invoked with --run NEW_DIR.
No default guest execution. Source review and scheduling release are external
prerequisites; this script does not grant its own admission.
"""
from pathlib import Path
import argparse,hashlib,json,re,resource,shutil,subprocess,time
base=Path('.context/amc-first/stage-k');old=Path('.context/amc-first/stage-g/run-1')
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
p=argparse.ArgumentParser();p.add_argument('--run',type=Path);args=p.parse_args()
if not args.run:print('Prepared source only; no guest run.');raise SystemExit(0)
build=json.loads((base/'build.json').read_text())
for path,digest in json.loads((base/'review-pins.json').read_text()).items():assert sha(path)==digest,path
for collection in ('sources','referenceFiles','libraryPins'):
 for name,digest in build[collection].items():assert sha(name)==digest,name
assert sha(base/'probe')==build['probeSHA256']
old_run=json.loads((old/'run.json').read_text());assert sha(old/'audit.py')==old_run['auditSHA256'];assert sha(old/'kernel-oracle.py')==old_run['kernelOracleSHA256']
out=args.run;out.mkdir(parents=True,exist_ok=False);(out/'upload').mkdir();(out/'second-request').mkdir();(out/'first/upload').mkdir(parents=True)
for name in ('audit.py','launch.py','build.json','review-pins.json','second-audit.py','mp4_oracle.py','ordering-audit.py'):shutil.copy2(base/name,out/name)
shutil.copytree(base/'source',out/'source');shutil.copy2(old/'audit.py',out/'first-audit.py');shutil.copy2(old/'kernel-oracle.py',out/'kernel-oracle.py')
command=[str(base/'probe'),'.context/audio/ringtone-page.snap','1',str(out),'.context/amc-first/stage-d/verified/sram.bin','.context/amc-first/stage-d/verified/written.bin',str(old)]
def limits():
 resource.setrlimit(resource.RLIMIT_CPU,(180,180));resource.setrlimit(resource.RLIMIT_AS,(1024**3,1024**3));resource.setrlimit(resource.RLIMIT_FSIZE,(8*1024**2,8*1024**2));resource.setrlimit(resource.RLIMIT_CORE,(0,0))
start=time.monotonic();code=None;timed_out=False
with (out/'trace.log').open('w') as log:
 try:code=subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,preexec_fn=limits,timeout=240).returncode
 except subprocess.TimeoutExpired:timed_out=True
report={'command':command,'returncode':code,'timedOut':timed_out,'wallSeconds':time.monotonic()-start,'guestPass':code==0,'firstBoundaryAuditPass':False,'consumptionAuditPass':False,'secondRequestAuditPass':False,'orderingAuditPass':False,'sourceManifestSHA256':sha(out/'build.json'),'frozenScripts':{name:sha(out/name) for name in ('audit.py','first-audit.py','kernel-oracle.py','launch.py','second-audit.py','mp4_oracle.py','ordering-audit.py')},'limits':{'wallSeconds':240,'cpuSeconds':180,'addressSpaceBytes':1024**3,'perFileBytes':8*1024**2,'totalRetiredInstructions':155300000,'preInputInstructions':5000000,'postInputInstructions':5000000,'postPublicationInstructions':5000000,'maximumPublications':2}}
def save(): (out/'run.json').write_text(json.dumps(report,indent=2)+'\n')
def audit(script,directory,logfile):
 with logfile.open('w') as log:subprocess.run(['python3',str(script),str(directory)],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
save()
# The075 prefix remains a separately audited result even on later failure.
if (out/'first/trace.log').exists():
 shutil.copy2(out/'first/trace.log',out/'first/upload/trace.log')
 audit(out/'kernel-oracle.py',out/'first/upload',out/'first/upload/kernel-oracle.log')
 audit(out/'first-audit.py',out/'first',out/'first/functional-audit.log')
 report['firstBoundaryAuditPass']=True;save()
if (out/'second-input.bin').exists():
 audit(out/'second-audit.py',out,out/'second-request-audit.log')
 report['secondRequestAuditPass']=True;save()
assert code==10 and not timed_out, 'unexpected observational exit'
audit(out/'ordering-audit.py',out,out/'ordering-audit.log')
# Actual artifact negatives use the same frozen auditor. No fake guest output.
artifacts=[p for p in out.iterdir() if p.name!='ordering-audit.json'];negatives=[]
for label,target,offset,message in [('record','ordering.bin',4,'record/log bytes differ'),('queue','owned-queue.bin',123,'guest artifact differs')]:
 mutant=out/('negative-'+label);mutant.mkdir()
 for artifact in artifacts:
  if artifact.name==target:
   data=bytearray(artifact.read_bytes());data[offset]^=0x80 if label=='record' else 1;(mutant/target).write_bytes(data)
  else:(mutant/artifact.name).symlink_to(artifact.resolve(),target_is_directory=artifact.is_dir())
 logfile=out/('negative-'+label+'.log')
 with logfile.open('w') as log:r=subprocess.run(['python3',str(out/'ordering-audit.py'),str(mutant)],stdout=log,stderr=subprocess.STDOUT,timeout=30)
 assert r.returncode!=0 and message in logfile.read_text();negatives.append({'label':label,'returncode':r.returncode,'expectedFailure':message})
report['orderingAuditPass']=True;report['artifactNegatives']=negatives;save();print(json.dumps(report,indent=2))
