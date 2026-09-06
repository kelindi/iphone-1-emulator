"""Synthetic destination receipt over frozen083 evidence; no guest execution."""
from pathlib import Path
import runpy,contextlib,io,tempfile,shutil,json,struct
b=Path(__file__).parent;base=Path('.context/amc-first/stage-q/run-1');mod=runpy.run_path(str(b/'destination-audit.py'));checks=0
with tempfile.TemporaryDirectory(prefix='destination-audit-') as td:
 out=Path(td)
 for p in base.iterdir():
  if p.name.startswith('negative-'):continue
  if p.is_dir():shutil.copytree(p,out/p.name)
  else:shutil.copy2(p,out/p.name)
 first=next(struct.iter_unpack('<6IQ',(out/'read-ledger.bin').read_bytes()));r=[0]*16;r[0]=0x40008000;r[1]=0x5000f220;r[2]=4032;r[7]=0x4000a00c;r[14]=0x60000200;r[15]=0x3009a20c
 d={'captured':True,'finished':True,'captureCycles':first[6],'finishCycles':9873752634,'cpsr':first[4],'sourcePhysical':first[0],'sctlr':1,'ttbr0':0x10000000,'ttbr1':0,'ttbcr':0,'dacr':1,'callerWindowAvailable':False,'callerWindowAddress':r[14]-4,'registers':r,'destinationMappings':[[0x10208000+1024*i,1024] for i in range(4)]}
 (out/'destination.json').write_text(json.dumps(d));(out/'destination.bin').write_bytes((out/'first/first-pcm.bin').read_bytes());(out/'destination-frame.bin').write_bytes(struct.pack('<5I',r[0],0,0,0,r[14]));(out/'destination-caller.bin').write_bytes(b'');(out/'destination-code.bin').write_bytes(mod['original_window'](r[15]));(out/'destination-prologue.bin').write_bytes(mod['original_window'](0x3009a1bc))
 start=f"DESTINATION BEGIN cycles{d['captureCycles']} source{r[1]:08x} destination{r[0]:08x} readerPC{r[15]:08x} cpsr{d['cpsr']:08x} savedLR{r[14]:08x} callerWindowAvailable0 ttbr010000000 ttbr100000000 ttbcr00000000 dacr00000001\n"
 end=f"DESTINATION VERIFIED source{r[1]:08x} destination{r[0]:08x} bytes4096 captureCycles{d['captureCycles']} finishCycles{d['finishCycles']} retainedUserContext1\n"
 trace=(out/'trace.log').read_text();trace=trace.replace('PCM READ address2200f220',start+'PCM READ address2200f220',1).replace('PASS first PCM read and ownership returned',end+'PASS first PCM read and ownership returned',1);(out/'trace.log').write_text(trace)
 def run():
  with contextlib.redirect_stdout(io.StringIO()):mod['run'](out)
 run();checks+=1
 def reject(target,change):
  global checks
  p=out/target;old=p.read_bytes();p.write_bytes(change(old))
  try:run()
  except AssertionError:checks+=1
  else:raise AssertionError('accepted invalid '+target)
  finally:p.write_bytes(old)
 for name in ('destination.bin','destination-code.bin','destination-prologue.bin','destination-frame.bin'):
  reject(name,lambda b:b[:3]+bytes([b[3]^1])+b[4:])
 for key in ('ttbr0','ttbr1','ttbcr','dacr','cpsr','captureCycles','finishCycles','sourcePhysical','callerWindowAddress'):
  def mutate(data,key=key):v=json.loads(data);v[key]^=4;return json.dumps(v).encode()
  reject('destination.json',mutate)
 reject('destination-caller.bin',lambda b:bytes(32))
print(f'destination external auditor: {checks} PASS; synthetic receipt only')
