"""One reviewed Stage N run; no default guest execution and no automatic retry."""
from pathlib import Path
import argparse,hashlib,json,re,resource,shutil,subprocess,time
from commands import audit_command
base=Path('.context/amc-first/stage-n');old=Path('.context/amc-first/stage-g/run-1');sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
p=argparse.ArgumentParser();p.add_argument('--run',type=Path);args=p.parse_args()
if not args.run:print('Prepared source only; no guest run.');raise SystemExit(0)
build=json.loads((base/'build.json').read_text())
for name,digest in json.loads((base/'review-pins.json').read_text()).items():assert sha(name)==digest,name
for collection in ('sources','referenceFiles','libraryPins'):
 for name,digest in build[collection].items():assert sha(name)==digest,name
assert sha(base/'probe')==build['probeSHA256']
out=args.run;out.mkdir(parents=True,exist_ok=False)
for name in ('upload','first/upload','second-request','third-request'):(out/name).mkdir(parents=True,exist_ok=True)
scripts=('audit.py','second-audit.py','third-audit.py','queue-audit.py','ordering-audit.py','original-context-oracle.py','mp4_oracle.py','commands.py','launch.py')
for name in scripts+('build.json','review-pins.json'):shutil.copy2(base/name,out/name)
shutil.copytree(base/'source',out/'source');shutil.copy2(old/'audit.py',out/'first-audit.py');shutil.copy2(old/'kernel-oracle.py',out/'kernel-oracle.py')
command=[str(base/'probe'),'.context/audio/ringtone-page.snap','1',str(out),'.context/amc-first/stage-d/verified/sram.bin','.context/amc-first/stage-d/verified/written.bin',str(old)]
def limits():
 resource.setrlimit(resource.RLIMIT_CPU,(180,180));resource.setrlimit(resource.RLIMIT_AS,(1024**3,1024**3));resource.setrlimit(resource.RLIMIT_FSIZE,(8*1024**2,8*1024**2));resource.setrlimit(resource.RLIMIT_CORE,(0,0))
start=time.monotonic();code=None;timed_out=False
with (out/'trace.log').open('w') as log:
 try:code=subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,preexec_fn=limits,timeout=240).returncode
 except subprocess.TimeoutExpired:timed_out=True
report={'command':command,'returncode':code,'timedOut':timed_out,'wallSeconds':time.monotonic()-start,'guestPass':code==0,'firstBoundaryAuditPass':False,'secondRequestAuditPass':False,'thirdRequestAuditPass':False,'queueAuditPass':False,'orderingAuditPass':False,'consumptionAuditPass':False,'frozenScripts':{name:sha(out/name) for name in scripts+('first-audit.py','kernel-oracle.py')},'limits':{'wallSeconds':240,'cpuSeconds':180,'addressSpaceBytes':1024**3,'perFileBytes':8*1024**2,'totalRetiredInstructions':155300000,'preInputInstructions':5000000,'postInputInstructions':5000000,'postPublicationInstructions':5000000,'maximumPublications':2,'maximumAdmittedInputs':3,'observerOnlySuccessorOfStageM':True}}
def save():(out/'run.json').write_text(json.dumps(report,indent=2)+'\n')
def audit(script,directory,logfile):
 with logfile.open('w') as log:subprocess.run(audit_command(out/script,directory),stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
save()
if (out/'first/trace.log').exists():
 shutil.copy2(out/'first/trace.log',out/'first/upload/trace.log');audit('kernel-oracle.py',out/'first/upload',out/'first/upload/kernel-oracle.log');audit('first-audit.py',out/'first',out/'first/functional-audit.log');report['firstBoundaryAuditPass']=True;save()
if (out/'second-input.bin').exists():
 audit('second-audit.py',out,out/'second-request-audit.log');report['secondRequestAuditPass']=True;save()
if (out/'third-input.bin').exists():
 for script,logname,flag in [('third-audit.py','third-request-audit.log','thirdRequestAuditPass'),('queue-audit.py','queue-audit.log','queueAuditPass'),('ordering-audit.py','ordering-audit.log','orderingAuditPass')]:
  audit(script,out,out/logname);report[flag]=True;save()
controls=[]
def negative(label,target,mutate,script,output_report,expected):
 artifacts=[p for p in out.iterdir() if p.name!=output_report and not p.name.startswith('negative-')];mutant=out/('negative-'+label);mutant.mkdir()
 for artifact in artifacts:
  if artifact.name==target:(mutant/target).write_bytes(mutate(artifact.read_bytes()))
  else:(mutant/artifact.name).symlink_to(artifact.resolve(),target_is_directory=artifact.is_dir())
 logfile=out/('negative-'+label+'.log');argv=audit_command(out/script,mutant)
 with logfile.open('w') as log:r=subprocess.run(argv,stdout=log,stderr=subprocess.STDOUT,timeout=30)
 assert r.returncode!=0 and expected in logfile.read_text();controls.append({'label':label,'command':argv,'returncode':r.returncode,'expectedFailure':expected});report['artifactNegatives']=controls;save()
def flip(offset,mask):
 def mutate(data):
  data=bytearray(data);data[offset]^=mask;return bytes(data)
 return mutate
if report['thirdRequestAuditPass'] and report['queueAuditPass'] and report['orderingAuditPass']:
 negative('third-input','third-input.bin',flip(123,1),'third-audit.py','third-request-audit.json','actual third input lacks unique original provenance')
 negative('queue','owned-queue.bin',flip(123,1),'queue-audit.py','queue-audit.json','owned queue differs from actual copied inputs')
 negative('record','ordering.bin',flip(4,0x80),'ordering-audit.py','ordering-audit.json','record/log bytes differ')
 negative('mapped-code','ordering.bin',flip(560,1),'ordering-audit.py','ordering-audit.json','original mapped code differs')
if code==0:
 assert report['thirdRequestAuditPass'] and report['queueAuditPass'] and report['orderingAuditPass'];audit('audit.py',out,out/'consumption-audit.log')
 negative('coverage','read-coverage.bin',flip(123,1),'audit.py','consumption-audit.json','missing actual read coverage')
 negative('read-bytes','read-bytes.bin',flip(123,1),'audit.py','consumption-audit.json','observed read bytes differ')
 for label in ('release-pc','release-value'):
  def alter(data,label=label):
   text=data.decode();at=text.index('CONTINUE frozen075 boundary matched;');prefix=text[:at];suffix=text[at:]
   pattern=r'^(ENGINE SRAM W 2200f208 width2 value)([0-9a-f]+)( pc)([0-9a-f]+)$'
   def corrupt(match):return match[1]+('00000000' if label=='release-value' else match[2])+match[3]+(format(int(match[4],16)^4,'08x') if label=='release-pc' else match[4])
   suffix,count=re.subn(pattern,corrupt,suffix,count=1,flags=re.M);assert count==1;return (prefix+suffix).encode()
  negative(label,'trace.log',alter,'audit.py','consumption-audit.json','actual owner store differs')
 report['consumptionAuditPass']=True;save()
print(json.dumps(report,indent=2))
if code!=0:raise SystemExit(10)
