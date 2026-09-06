"""Synthetic new-format wrapper around preserved Stage T bytes, not a new guest."""
from pathlib import Path
import json,struct,subprocess,shutil
b=Path('.context/amc-first/stage-w');old=Path('.context/amc-first/stage-t/run-1');p=b/'synthetic-audit';p.mkdir(exist_ok=True)
for f in old.iterdir():
 if f.name=='trace.log' or (p/f.name).exists():continue
 (p/f.name).symlink_to(f.resolve(),target_is_directory=f.is_dir())
(p/'stream-gate').mkdir(exist_ok=True)
for n in ('sram.bin','registers.bin','owned-queue.bin','init-written.bin','pcm-written.bin','host-written.bin','dma-copies.bin','dma-bytes.bin'):
 if not (p/'stream-gate'/n).exists():(p/'stream-gate'/n).symlink_to((old/n).resolve())
t=(old/'trace.log').read_text().replace('STOP third publication work cap','STREAM GATE cycles9873752646 pcc0277fbc publications2 releases1 queued65533 copied66212 decoded679 service1; synthetic format fixture\nSTOP third publication work cap');(p/'trace.log').write_text(t)
gs=[]
for i in range(2):
 g=dict(ordinal=i+1,slot=i,published=[9873234585,9873723995][i],decoded=[348,679][i],copied=[16384,65536][i],samples=2048*(i+1),events=0x600,mask=0x2000,ipc=1 if i==0 else 0,ack=1,released=1 if i==0 else 0,pending=0,ipcCycles=9873720000,ackCycles=9873720001,releaseStart=9873752633,releaseCallback=9873752634,releaseEnd=9873752634,releasePC=0xc02726a8,releaseCPSR=0x20000013,releaseOpcode=0xe1c030b0,afterPC=0xc02726ac,afterCPSR=0x20000013,releaseAddress=0x2200f208,driver=0xe1230000,index=0,covered=4096 if i==0 else 0,reads=1024 if i==0 else 0)
 gs.append(g);(p/f'stream-pcm-{i+1}.bin').write_bytes((old/('second-pcm.bin' if i else 'first-pcm.bin')).read_bytes());(p/f'stream-seen-{i+1}.bin').write_bytes(bytes(4096) if i else (old/'read-bytes.bin').read_bytes());(p/f'stream-coverage-{i+1}.bin').write_bytes(bytes(4096) if i else (old/'read-coverage.bin').read_bytes())
r=bytearray()
for a,w,v,pc,cpsr,owner,cycle in struct.iter_unpack('<6IQ',(old/'read-ledger.bin').read_bytes()):r+=struct.pack('<Q8I',cycle,1,0,a,w,v,pc,cpsr,owner)
(p/'stream-reads.bin').write_bytes(r);(p/'stream.json').write_text(json.dumps(dict(published=2,released=1,readRecords=1024,pendingGeneration=0,ipcPending=0,gateSaved=True,generations=gs)))
def invoke():return subprocess.run(['python3',str(b/'stream-audit.py'),str(p)],capture_output=True,text=True)
x=invoke();assert x.returncode==0,x.stderr
cases=[('stream-coverage-1.bin',123,'generation read receipt differs'),('stream-pcm-1.bin',123,'actual decoded PCM differs'),('stream-reads.bin',24,'observed generation bytes differ')]
for name,offset,expected in cases:
 f=p/name;original=f.read_bytes();value=bytearray(original);value[offset]^=1;f.write_bytes(value);x=invoke();f.write_bytes(original);assert x.returncode and expected in x.stderr,x.stderr
f=p/'stream.json';original=f.read_text();value=json.loads(original);value['generations'][0]['afterPC']^=4;f.write_text(json.dumps(value));x=invoke();f.write_text(original);assert x.returncode and 'generation retirement differs' in x.stderr,x.stderr
print('PASS5 external generation-auditor checks; synthetic new-format wrapper around preserved Stage T, no new guest')
