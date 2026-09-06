"""One capture-only run; preserve every failure, never retry automatically."""
from pathlib import Path
import argparse,hashlib,json,resource,shutil,subprocess,time
base=Path('.context/amc-first/stage-i');old=Path('.context/amc-first/stage-g/run-1');h=Path('.context/amc-first/stage-h/run-1')
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
p=argparse.ArgumentParser();p.add_argument('--run',type=Path);args=p.parse_args()
if not args.run:print('No guest run requested.');raise SystemExit(0)
build=json.loads((base/'build.json').read_text());pins=json.loads((base/'review-pins.json').read_text())
for collection in ('sources','referenceFiles','libraryPins'):
 for name,digest in build[collection].items():assert sha(name)==digest,name
for name,digest in pins.items():assert sha(name)==digest,name
assert sha(base/'probe')==build['probeSHA256']
out=args.run;out.mkdir(parents=True,exist_ok=False);(out/'upload').mkdir();(out/'first/upload').mkdir(parents=True)
for name in ('audit.py','mp4_oracle.py','launch.py','build.json','review-pins.json'):shutil.copy2(base/name,out/name)
shutil.copytree(base/'source',out/'source');shutil.copy2(old/'audit.py',out/'first-audit.py');shutil.copy2(old/'kernel-oracle.py',out/'kernel-oracle.py')
command=[str(base/'probe'),'.context/audio/ringtone-page.snap','1',str(out),'.context/amc-first/stage-d/verified/sram.bin','.context/amc-first/stage-d/verified/written.bin',str(old)]
def limits():
 resource.setrlimit(resource.RLIMIT_CPU,(180,180));resource.setrlimit(resource.RLIMIT_AS,(1024**3,1024**3));resource.setrlimit(resource.RLIMIT_FSIZE,(8*1024**2,8*1024**2));resource.setrlimit(resource.RLIMIT_CORE,(0,0))
start=time.monotonic();code=None;timeout=False
with (out/'trace.log').open('w') as log:
 try:code=subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,preexec_fn=limits,timeout=240).returncode
 except subprocess.TimeoutExpired:timeout=True
report={'command':command,'returncode':code,'timedOut':timeout,'wallSeconds':time.monotonic()-start,'capturePass':False,'consumptionPass':False,'transferAccepted':False,'firstBoundaryAuditPass':False,'frozenScriptHashes':{name:sha(out/name) for name in ('audit.py','mp4_oracle.py','launch.py','first-audit.py','kernel-oracle.py')},'limits':{'wallSeconds':240,'cpuSeconds':180,'addressSpaceBytes':1024**3,'perFileBytes':8*1024**2,'totalRetired':155300000,'postPublicationRetired':5000000,'maximumPublications':2}}
def save():(out/'run.json').write_text(json.dumps(report,indent=2)+'\n')
def audit(script,directory,logfile):
 with logfile.open('w') as log:subprocess.run(['python3',str(script),str(directory)],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
save()
if (out/'first/trace.log').exists():
 shutil.copy2(out/'first/trace.log',out/'first/upload/trace.log')
 audit(out/'kernel-oracle.py',out/'first/upload',out/'first/upload/kernel-oracle.log');audit(out/'first-audit.py',out/'first',out/'first/functional-audit.log');report['firstBoundaryAuditPass']=True;save()
assert code==10 and not timeout, 'unexpected observational exit'
audit(out/'audit.py',out,out/'capture-audit.log')
artifacts=[p for p in out.iterdir() if p.name!='capture-audit.json'];negative=out/'negative-captured-byte';negative.mkdir()
for artifact in artifacts:
 if artifact.name=='second-input.bin':
  data=bytearray(artifact.read_bytes());data[123]^=1;(negative/artifact.name).write_bytes(data)
 else:(negative/artifact.name).symlink_to(artifact.resolve(),target_is_directory=artifact.is_dir())
with (out/'negative-captured-byte.log').open('w') as log:r=subprocess.run(['python3',str(out/'audit.py'),str(negative)],stdout=log,stderr=subprocess.STDOUT,timeout=30)
assert r.returncode!=0 and 'captured bytes lack unique original provenance' in (out/'negative-captured-byte.log').read_text()
report['capturePass']=True;report['actualArtifactNegativeRejected']=True;save();print(json.dumps(report,indent=2))
