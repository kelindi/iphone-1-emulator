"""Read-only ordering oracle. Presence at a pre-step hook is not retirement."""
from pathlib import Path
import hashlib,json,re,struct,sys
PCS={0xc027602c,0xc027619c,0xc02761c0,0xc02761c4,0xc0274778,0xc0274878}
def records(blob,trace):
 assert len(blob)%120==0 and len(blob)<=128*120, 'record size/cap differs'
 logs=re.findall(r'^ORDER record.*$',trace,re.M);assert len(logs)==len(blob)//120, 'record/log count differs'
 result=[];previous=0
 for index,(row,line) in enumerate(zip(struct.iter_unpack('<24IQIIQ',blob),logs),1):
  pc,cpsr=row[:2];regs=row[2:18];driver,state,o0,o1,flags,retired=row[18:24];cycles,after_pc,after_cpsr,after_cycles=row[24:]
  assert pc in PCS and cpsr&0x3f==0x13 and regs[15]==pc and cycles>=previous;previous=cycles
  assert driver==0xccaa2c00 and regs[0 if pc==0xc0274778 else 4 if pc==0xc0274878 else 5]==driver
  assert 0<=flags<=65535 and after_cycles==cycles+1
  expected=(after_cpsr&0x3f==0x13 and after_pc==(0xc0277284 if pc==0xc02761c0 else pc+4))
  assert retired==int(expected), 'retirement attribution differs'
  if pc==0xc0274778:assert regs[1]<=2, 'refill index out of range'
  wanted=f'ORDER record{index} retired{retired} pc{pc:08x} cpsr{cpsr:08x} cycles{cycles} afterPC{after_pc:08x} afterCPSR{after_cpsr:08x} afterCycles{after_cycles} driver{driver:08x} state{state:08x} output0{o0:08x} output1{o1:08x} flags{flags:04x}'+''.join(f' r{i}={r:08x}' for i,r in enumerate(regs))
  assert line==wanted, 'record/log bytes differ'
  result.append({'pc':f'{pc:08x}','cpsr':f'{cpsr:08x}','cycles':cycles,'retired':bool(retired),'afterPC':f'{after_pc:08x}','afterCPSR':f'{after_cpsr:08x}','driverState':state,'outputWords':[o0,o1],'flags24d24e':[flags&255,flags>>8],'refillIndex':regs[1] if pc==0xc0274778 and retired else None,'registers':[f'{r:08x}' for r in regs]})
 assert f'ORDER summary records{len(result)} cap128' in trace
 return result

def run(out):
 out=Path(out);baseline=Path('.context/amc-first/stage-j/run-1');trace=(out/'trace.log').read_text()
 assert ''.join(line for line in trace.splitlines(True) if not line.startswith('ORDER '))==(baseline/'trace.log').read_text(), 'non-observer trace differs'
 assert json.loads((out/'first/functional-audit.json').read_text())['passed'] and json.loads((out/'second-request-audit.json').read_text())['passed']
 files={}
 for old in sorted(baseline.rglob('*.bin')):
  rel=old.relative_to(baseline);data=(out/rel).read_bytes();assert data==old.read_bytes(), 'guest artifact differs: '+str(rel);files[str(rel)]=hashlib.sha256(data).hexdigest()
 data=(out/'ordering.bin').read_bytes();rows=records(data,trace)
 assert any(row['refillIndex'] is not None for row in rows), 'no retired refill entry observed'
 report={'passed':True,'scope':'Bounded observer only. All previous trace/state unchanged; no third input admission or PCM consumption.','records':rows,'retiredRecords':sum(row['retired'] for row in rows),'unretiredRecords':sum(not row['retired'] for row in rows),'unchangedArtifacts':files,'recordSHA256':hashlib.sha256(data).hexdigest()}
 (out/'ordering-audit.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':run(sys.argv[1])
