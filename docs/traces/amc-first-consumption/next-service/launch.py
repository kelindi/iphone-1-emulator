"""One reviewed Stage T run; no default guest execution and no automatic retry."""
from pathlib import Path
import argparse,hashlib,json,re,resource,shutil,subprocess,time
from commands import audit_command
base=Path('.context/amc-first/stage-t');old=Path('.context/amc-first/stage-g/run-1');sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
p=argparse.ArgumentParser();p.add_argument('--run',type=Path);args=p.parse_args()
if not args.run:print('Prepared source only; no guest run.');raise SystemExit(0)
build=json.loads((base/'build.json').read_text())
for name,digest in json.loads((base/'review-pins.json').read_text()).items():assert sha(name)==digest,name
for collection in ('sources','referenceFiles','libraryPins'):
 for name,digest in build[collection].items():assert sha(name)==digest,name
assert sha(base/'probe')==build['probeSHA256']
out=args.run;out.mkdir(parents=True,exist_ok=False)
for name in ('consumption-prefix','upload','first/upload','second-request','third-request','fourth-request','fifth-request','user-return'):(out/name).mkdir(parents=True,exist_ok=True)
scripts=('post-audit.py','transport-audit.py','caller-audit.py','audit.py','destination-audit.py','retirement-audit.py','second-audit.py','third-audit.py','later-audit.py','queue-audit.py','ordering-audit.py','original-context-oracle.py','mp4_oracle.py','commands.py','launch.py')
for name in scripts+('build.json','review-pins.json','post-original.json'):shutil.copy2(base/name,out/name)
shutil.copytree(base/'source',out/'source');shutil.copy2(old/'audit.py',out/'first-audit.py');shutil.copy2(old/'kernel-oracle.py',out/'kernel-oracle.py')
command=[str(base/'probe'),'.context/audio/ringtone-page.snap','1',str(out),'.context/amc-first/stage-d/verified/sram.bin','.context/amc-first/stage-d/verified/written.bin',str(old)]
def limits():
 resource.setrlimit(resource.RLIMIT_CPU,(180,180));resource.setrlimit(resource.RLIMIT_AS,(1024**3,1024**3));resource.setrlimit(resource.RLIMIT_FSIZE,(8*1024**2,8*1024**2));resource.setrlimit(resource.RLIMIT_CORE,(0,0))
start=time.monotonic();code=None;timed_out=False
with (out/'trace.log').open('w') as log:
 try:code=subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,preexec_fn=limits,timeout=240).returncode
 except subprocess.TimeoutExpired:timed_out=True
report={'command':command,'returncode':code,'timedOut':timed_out,'wallSeconds':time.monotonic()-start,'guestPass':code==0,'firstBoundaryAuditPass':False,'secondRequestAuditPass':False,'thirdRequestAuditPass':False,'laterAuditPass':False,'queueAuditPass':False,'orderingAuditPass':False,'consumptionAuditPass':False,'destinationAuditPass':False,'frozenScripts':{name:sha(out/name) for name in scripts+('first-audit.py','kernel-oracle.py')},'limits':{'wallSeconds':240,'cpuSeconds':180,'addressSpaceBytes':1024**3,'perFileBytes':8*1024**2,'totalRetiredInstructions':155300000,'preInputInstructions':5000000,'postInputInstructions':5000000,'postPublicationInstructions':5000000,'maximumPublications':2,'maximumAdmittedInputs':5,'unchangedEngine':True}}
run_root=out
report['guestPass']=False
report['prefixPass']=False
report['postAuditPass']=False
def save():(run_root/'run.json').write_text(json.dumps(report,indent=2)+'\n')
def audit(script,directory,logfile):
 with logfile.open('w') as log:subprocess.run(audit_command(out/script,directory),stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
save()
if not (run_root/'consumption-prefix/trace.log').exists():
 print('Earlier boundary before complete083 prefix; preserved without retry.');raise SystemExit(10)
# Only fresh artifacts from this same run fill immutable prefix files not already
# emitted at083. Existing prefix state/trace is never overwritten.
out=run_root/'consumption-prefix'
for p in run_root.iterdir():
 if p==out or p.name.startswith('negative-') or (out/p.name).exists():continue
 if p.is_file() and p.suffix in ('.bin','.json','.py'):shutil.copy2(p,out/p.name)
 elif p.is_dir() and p.name in ['first','upload','second-request','third-request','fourth-request','fifth-request','user-return','source']:shutil.copytree(p,out/p.name)
if (out/'first/trace.log').exists():
 shutil.copy2(out/'first/trace.log',out/'first/upload/trace.log');audit('kernel-oracle.py',out/'first/upload',out/'first/upload/kernel-oracle.log');audit('first-audit.py',out/'first',out/'first/functional-audit.log');report['firstBoundaryAuditPass']=True;save()
if (out/'second-input.bin').exists():
 audit('second-audit.py',out,out/'second-request-audit.log');report['secondRequestAuditPass']=True;save()
if (out/'third-input.bin').exists():
 for script,logname,flag in [('third-audit.py','third-request-audit.log','thirdRequestAuditPass'),('later-audit.py','later-audit.log','laterAuditPass'),('queue-audit.py','queue-audit.log','queueAuditPass'),('ordering-audit.py','ordering-audit.log','orderingAuditPass')]:
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
 negative('fourth-input','fourth-input.bin',flip(123,1),'later-audit.py','later-audit.json','actual later input lacks unique original provenance')
 if (out/'fifth-input.bin').exists():negative('fifth-input','fifth-input.bin',flip(123,1),'later-audit.py','later-audit.json','actual later input lacks unique original provenance')
 negative('dma-bytes','dma-bytes.bin',flip(123,1),'later-audit.py','later-audit.json','actual copied bytes differ from capture')
 negative('dma-state','trace.log',lambda b:re.sub(rb'(DMA FINAL .* current)([0-9a-f]{8})',lambda m:m[1]+b'ffffffff',b),'later-audit.py','later-audit.json','DMA final current/ownership differs')
 negative('queue','owned-queue.bin',flip(123,1),'queue-audit.py','queue-audit.json','owned queue differs from actual copied inputs')
 negative('record','ordering.bin',flip(4,0x80),'ordering-audit.py','ordering-audit.json','record/log bytes differ')
 negative('mapped-code','ordering.bin',flip(560,1),'ordering-audit.py','ordering-audit.json','original mapped code differs')
 negative('kernel-unavailable','ordering.bin',flip(682,1),'ordering-audit.py','ordering-audit.json','unavailable kernel fields fabricated')
if (out/'trace.log').exists():
 assert report['thirdRequestAuditPass'] and report['queueAuditPass'] and report['orderingAuditPass'];audit('retirement-audit.py',out,out/'retirement-audit.log');audit('audit.py',out,out/'consumption-audit.log')
 negative('coverage','read-coverage.bin',flip(123,1),'audit.py','consumption-audit.json','missing actual read coverage')
 negative('read-bytes','read-bytes.bin',flip(123,1),'audit.py','consumption-audit.json','observed read bytes differ')
 for label in ('release-pc','release-value'):
  def alter(data,label=label):
   text=data.decode();at=text.index('CONTINUE frozen075 boundary matched;');prefix=text[:at];suffix=text[at:]
   pattern=r'^(ENGINE SRAM W 2200f208 width2 value)([0-9a-f]+)( pc)([0-9a-f]+)$'
   def corrupt(match):return match[1]+('00000000' if label=='release-value' else match[2])+match[3]+(format(int(match[4],16)^4,'08x') if label=='release-pc' else match[4])
   suffix,count=re.subn(pattern,corrupt,suffix,count=1,flags=re.M);assert count==1;return (prefix+suffix).encode()
  negative(label,'trace.log',alter,'audit.py','consumption-audit.json','actual owner store differs')
 for label,old,new,expected in [('step-start','startCycles9873752633','startCycles9873752634','instruction retirement cycles differ'),('step-pc','afterPCc02726ac','afterPCc02726b0','actual retired successor/mode differs'),('step-mode','afterCPSR20000013','afterCPSR20000033','actual retired successor/mode differs'),('step-return','emuRunReturned1','emuRunReturned0','emu_run did not return outside guest'),('step-code','instructione1c030b0','instructione1c030b1','actual original owner instruction differs')]:
  def changed(data,old=old,new=new):
   assert old.encode() in data;return data.replace(old.encode(),new.encode())
  negative(label,'trace.log',changed,'audit.py','consumption-audit.json',expected)
 report['consumptionAuditPass']=True;save()
 audit('destination-audit.py',out,out/'destination-audit.log')
 for label,target,offset,expected in [('copy-destination','destination.bin',123,'actual copied destination differs'),('reader-code','destination-code.bin',4,'mapped reader bytes differ'),('reader-prologue','destination-prologue.bin',4,'mapped prologue bytes differ'),('saved-frame','destination-frame.bin',16,'actual saved frame differs')]:
  negative(label,target,flip(offset,1),'destination-audit.py','destination-audit.json',expected)
 def wrong_context(data):
  value=json.loads(data);value['ttbr0']^=0x4000;return json.dumps(value).encode()
 negative('copy-addressspace','destination.json',wrong_context,'destination-audit.py','destination-audit.json','destination context/log differs')
 report['destinationAuditPass']=True;save()
 audit('caller-audit.py',out,out/'caller-audit.log')
 audit('transport-audit.py',out,out/'transport-audit.log')
 negative('transport-values','transport-release-values.bin',flip(33,1),'transport-audit.py','transport-audit.json','transport scalar receipt differs')
 if json.loads((out/'transport-release.json').read_text())['managerAvailable']:
  negative('transport-manager','transport-release-manager.bin',flip(3,0x80),'transport-audit.py','transport-audit.json','manager bounds differ')
  def manager_availability(data):
   value=json.loads(data);value['managerAvailable']=False;return json.dumps(value).encode()
  negative('transport-manager-availability','transport-release.json',manager_availability,'transport-audit.py','transport-audit.json','unavailable manager fabricated')
 def lli_availability(data):
  value=json.loads(data);value['lliAvailableMask']^=1;return json.dumps(value).encode()
 negative('transport-lli-availability','transport-release.json',lli_availability,'transport-audit.py','transport-audit.json','LLI availability differs')
 report['transportAuditPass']=True;save()
report['prefixPass']=True;save()
audit('post-audit.py',run_root,run_root/'post-audit.log');report['postAuditPass']=True;save()
# Optional actual post-record mutations only when such a record exists.
prefix_out=out;out=run_root
negative('post-start-service','trace.log',lambda b:b.replace(b'engineService0 events',b'engineService1 events'),'post-audit.py','post-audit.json','actual post start state differs')
out=prefix_out
postdata=(run_root/'post-records.bin').read_bytes()
if postdata:
 prefix_out=out;out=run_root
 negative('post-record-code','post-records.bin',flip(120,1),'post-audit.py','post-audit.json','post original mapped code differs')
 negative('post-record-retirement','post-records.bin',flip(84,1),'post-audit.py','post-audit.json','post retirement receipt differs')
 out=prefix_out
print(json.dumps(report,indent=2))
