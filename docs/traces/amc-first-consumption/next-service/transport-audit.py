"""Frozen Stage R equality plus observer scalar/availability/bounds consistency."""
from pathlib import Path
import json,struct,hashlib,sys

def names():
 n=['cycles','timerTicks']
 for _ in range(2):n+=['i2s.index']+['i2s.reg']*7+['i2s.reads','i2s.writes','i2s.unknownReads','i2s.unknownWrites','i2s.unknownCount']+['i2s.unknownOffset']*8
 for _ in range(2):
  n+=['dmac.index','dmac.config','dmac.sync','dmac.rawTc','dmac.rawErr','dmac.reads','dmac.writes','dmac.unknownReads','dmac.unknownWrites','dmac.items','dmac.transfers','dmac.bytes','dmac.completions','dmac.configWrites','dmac.configFirst']
  for _ in range(8):n+=['channel.index','channel.src','channel.dst','channel.lli','channel.ctrl','channel.cfg','channel.runs','channel.bytes']
 n+=['power.state','power.cfg0','power.cfg1','power.sram','power.cfg24','power.cfg28']+['wm8758.reg','wm8758.written']*128+['wm8758.reads','wm8758.writes']
 return n

def audit_stage(out,stage):
 out=Path(out);d=json.loads((out/f'transport-{stage}.json').read_text());assert d['stage']==stage
 rows=d['values'];keys=[x[0] for x in rows];v=[x[1] for x in rows];fixed=names();assert keys[:len(fixed)]==fixed,'transport schema differs'
 tail=keys[len(fixed):]
 if tail!=['clock.available']:
  assert tail[:6]==['clock.base','clock.size','clock.count','clock.reads','clock.writes','clock.oob'];count=v[len(fixed)+2];assert count<=1024 and tail[6:]==['clock.reg']*count+['clock.available'],'clock capacity differs';assert v[-1]==1 and v[len(fixed)]==0x3c500000
 else:assert v[-1]==0
 assert all(type(x)==int and 0<=x<2**64 for x in v)
 raw=(out/f'transport-{stage}-values.bin').read_bytes();assert raw==struct.pack('<'+'Q'*len(v),*v),'transport scalar receipt differs'
 manager=(out/f'transport-{stage}-manager.bin').read_bytes();assert len(manager)==64
 if d['managerAvailable']:
  assert d['managerAddress']==0x39254d38,'manager source differs'
  w=struct.unpack('<16I',manager);base,end,read,write=w[:4];capacity=w[8]
  assert 0<base<end<=2**32 and 0<capacity<=0x40000 and end-base==capacity and base<=read<=end and base<=write<=end and w[5]<=capacity,'manager bounds differ'
 else:assert manager==bytes(64),'unavailable manager fabricated'
 lli=(out/f'transport-{stage}-lli.bin').read_bytes();mask=d['lliAvailableMask'];assert len(lli)==256 and 0<=mask<65536
 assert stage=='release' or mask==0,'early LLI fabricated'
 # Actual runnable controller/channel state; never inferred from the Active bit.
 for i in range(16):
  at=44+(i//8)*79;config=v[at+1];ch=at+15+(i%8)*8;address=v[ch+3]&~3;cfg=v[ch+5]
  eligible=stage=='release' and config&1 and cfg&1 and not cfg&0x40000 and 0x10000000<=address and address+16<=0x20000000
  assert bool(mask&(1<<i))==bool(eligible),'LLI availability differs'
  if not mask&(1<<i):assert lli[i*16:i*16+16]==bytes(16),'unavailable LLI fabricated'
 return {'stage':stage,'cycles':v[0],'timerTicks':v[1],'managerAvailable':d['managerAvailable'],'managerWords':list(struct.unpack('<16I',manager)) if d['managerAvailable'] else None,'lliAvailableMask':mask,'scalarSHA256':hashlib.sha256(raw).hexdigest(),'values':rows}

def run(out):
 out=Path(out);old=Path('.context/amc-first/stage-r/run-1')
 assert (out/'trace.log').read_bytes()==(old/'trace.log').read_bytes(),'Stage R exact trace differs'
 for p in old.glob('*.bin'):assert (out/p.name).read_bytes()==p.read_bytes(),'Stage R exact artifact differs: '+p.name
 for name in ['destination-audit.json','caller-audit.json','consumption-audit.json','retirement-audit.json']:assert json.loads((out/name).read_text())['passed']
 stages=[audit_stage(out,s) for s in ['fixture','first-pcm','copy','release']]
 assert [s['cycles'] for s in stages]==[9825000000,9873234585,9873751123,9873752634],'transport boundary differs'
 assert not stages[0]['managerAvailable'] and not stages[1]['managerAvailable'],'early manager fabricated'
 report={'passed':True,'scope':'Exact Stage R stop/state plus actual manager and existing model register census. No downstream continuation or FIFO acceptance.','stages':stages}
 (out/'transport-audit.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({'passed':True,'stages':[{k:v for k,v in s.items() if k!='values'} for s in stages]}))
if __name__=='__main__':run(sys.argv[1])
