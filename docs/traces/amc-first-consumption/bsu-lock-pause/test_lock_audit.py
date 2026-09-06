"""Synthetic lock receipt format around retained U artifacts; no new guest."""
from pathlib import Path
import json,struct,subprocess
b=Path('.context/amc-first/stage-v');u=Path('.context/amc-first/stage-u/run-1');p=b/'synthetic-lock-audit';p.mkdir(exist_ok=True)
for f in u.iterdir():
 if f.name=='trace.log' or (p/f.name).exists():continue
 (p/f.name).symlink_to(f.resolve(),target_is_directory=f.is_dir())
if not (p/'lock-prefix').exists():(p/'lock-prefix').symlink_to(u.resolve(),target_is_directory=True)
j=dict(prefixSaved=1,locked=1,dmaPaused=1,dmaActive=1,transport=0,inputPending=0,copied=83304,decoded=17770,queue=65534,current=0x14ae1568,start=0x14ae1000,words=4096,copiedWords=346,events=0x200,mask=0x2000,samples=108544,publications=53,receipts=[],reads=[],driverFieldsAvailable=1,driver=0xccaa2c00,savePointer=0xccaa2d84,pc=0xc0277dfc,cpsr=0x60000013,cycles=9874729142)
t=(u/'trace.log').read_text().split('STOP unsupported engine write cycles9874729042')[0]+'LOCK PREFIX synthetic receipt fixture\n'
for i,(pc,a,v,op) in enumerate(zip([0xc0277ce8,0xc0278218,0xc0278220],[0x38500400,0x38501000,0x38500a00],[1,0,0],[0xe5823400,0xe5832000,0xe5832a00])):
 cycle=9874729042+10*i;r=dict(pending=0,retired=1,start=cycle-1,callback=cycle,end=cycle,pc=pc,cpsr=0x60000013,opcode=op,address=a,value=v,afterPC=pc+4,afterCPSR=0x60000013);j['receipts'].append(r)
 t+=f'AMC W {a:08x} width4 value{v:08x} pc{pc:08x} tick1\nLOCK RETIRED index{i} start{cycle-1} callback{cycle} end{cycle} pc{pc:08x} cpsr60000013 opcode{op:08x} address{a:08x} value{v:08x} afterPC{pc+4:08x} afterCPSR60000013 locked1 paused{int(i>0)} active1 transport{int(i<2)} copied83304 decoded17770 queue65534 emuRunReturned1\n'
for a,v,pc in zip([0x38500400,0x38501008,0x38501004,0x38501010],[1,4096,0x14ae1000,0x14ae1568],[0xc0277cf0,0xc0278060,0xc0278064,0xc0278068]):
 j['reads'].append(dict(address=a,value=v,pc=pc,cpsr=0x60000013,cycles=9874729100));t+=f'LOCK READ address{a:08x} value{v:08x} pc{pc:08x} cpsr60000013 cycles9874729100\n'
t+='STOP unsupported engine read cycles9874729142 ticks1756792225 pcc0277dfc lrc0271af8 address38500410 width4 value00000000\nSTOP context inside-unretired-instruction\n'
(p/'trace.log').write_text(t);(p/'lock.json').write_text(json.dumps(j));(p/'locked-driver-fields.bin').write_bytes(struct.pack('<II',0x14ae1568,3750));(p/'locked-read-code.bin').write_bytes(struct.pack('<I',0xe5933410))
def invoke():return subprocess.run(['python3',str(b/'lock-audit.py'),str(p)],capture_output=True,text=True)
x=invoke();assert x.returncode==0,x.stderr
for field,message in [('dmaActive','actual lock/pause ownership differs'),('current','actual frozen resources differ')]:
 d=dict(j);d[field]^=1;(p/'lock.json').write_text(json.dumps(d));x=invoke();assert x.returncode and message in x.stderr,x.stderr
(p/'lock.json').write_text(json.dumps(j));f=p/'locked-driver-fields.bin';old=f.read_bytes();d=bytearray(old);d[4]^=1;f.write_bytes(d);x=invoke();f.write_bytes(old);assert x.returncode and 'actual original saved current/remaining differs' in x.stderr,x.stderr
j['receipts'][0]['afterPC']^=4;(p/'lock.json').write_text(json.dumps(j));x=invoke();assert x.returncode and 'actual lock successor/mode differs' in x.stderr,x.stderr
print('PASS5 external lock-auditor checks; synthetic format only')
