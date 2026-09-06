"""Artifact-only correction; cannot execute an emulator or repeat the guest."""
from pathlib import Path
import json,hashlib,subprocess,shutil,re
b=Path('.context/amc-first/stage-u');p=b/'run-1';a=p/'artifact-v2';a.mkdir(exist_ok=False)
shutil.copy2(b/'artifact-audit-v2.py',a/'audit.py');shutil.copy2(p/'mp4_oracle.py',a/'mp4_oracle.py')
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest();pins={f.name:sha(f) for f in a.iterdir() if f.is_file()};(a/'pins.json').write_text(json.dumps(pins,indent=2)+'\n')
report={'scope':'Artifact-only audit correction; exactly one unchanged guest run. Original frozen auditor/log and failed launcher retained.','correction':'Generation2 IPC predates frozen083. Original kernel IPC entry/return and owner-store sequence now independently validates every generation; no fabricated timed STREAM line.','pins':pins,'positive':False,'negatives':[]}
def save():(a/'report.json').write_text(json.dumps(report,indent=2)+'\n')
def invoke(directory,name):
 for n,h in pins.items():assert sha(a/n)==h
 with (a/(name+'.log')).open('w') as log:r=subprocess.run(['python3',str(a/'audit.py'),str(directory)],stdout=log,stderr=subprocess.STDOUT,timeout=30)
 return r.returncode,(a/(name+'.log')).read_text()
code,log=invoke(p,'positive');assert code==0,log;report['positive']=True;save()
def flip(offset):
 def fn(data):d=bytearray(data);d[offset]^=1;return bytes(d)
 return fn
def field(n,key):
 def fn(data):d=json.loads(data);d['generations'][n-1][key]^=4;return json.dumps(d).encode()
 return fn
def ipc(data):
 text=data.decode();i=text.index('STREAM GATE');before,tail=text[:i],text[i:];tail,n=re.subn(r'(CONSUMER HOOK pcc0271f30 r0)00000000',r'\g<1>00000001',tail,count=1);assert n==1;return (before+tail).encode()
def owner(data):
 text=data.decode();i=text.index('STREAM GATE');before,tail=text[:i],text[i:];tail,n=re.subn(r'(ENGINE SRAM W 2200f20a width2 value00000001 pc)c02726a8',r'\g<1>c02726ac',tail,count=1);assert n==1;return (before+tail).encode()
cases=[('coverage','stream-coverage-4.bin',flip(123),'generation read receipt differs'),('read-value','stream-reads.bin',flip(24),'observed generation bytes differ'),('read-generation','stream-reads.bin',flip(8),'read generation/width/owner invalid'),('pcm','stream-pcm-53.bin',flip(123),'actual decoded PCM differs'),('release-successor','stream.json',field(3,'afterPC'),'generation retirement differs'),('release-opcode','stream.json',field(3,'releaseOpcode'),'generation original store differs'),('copy','dma-bytes.bin',flip(123),'actual copied bytes differ from capture'),('queue','owned-queue.bin',flip(123),'final queue differs from actual copied bytes'),('ipc-return','trace.log',ipc,'actual IPC return differs'),('actual-owner','trace.log',owner,'actual generation owner write differs')]
for label,target,mutate,expected in cases:
 m=a/('negative-'+label);m.mkdir()
 for f in p.iterdir():
  if f.name in ('artifact-v2','stream-audit.json') or f.name.startswith('negative-'):continue
  if f.name==target:(m/f.name).write_bytes(mutate(f.read_bytes()))
  else:(m/f.name).symlink_to(f.resolve(),target_is_directory=f.is_dir())
 code,log=invoke(m,'negative-'+label);assert code!=0 and expected in log,(label,code,log);report['negatives'].append({'label':label,'returncode':code,'expected':expected});save()
print(json.dumps(report))
