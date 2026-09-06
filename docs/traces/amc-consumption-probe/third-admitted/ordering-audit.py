"""Read-only ordering oracle. Presence at a pre-step hook is not retirement."""
from pathlib import Path
import hashlib,json,re,struct,sys,runpy
PCS={0xc027602c,0xc0272224,0xc0277284,0xc027604c,0xc0274778,0xc0274878}
def records(blob,trace):
 assert len(blob)%128==0 and len(blob)<=128*128, 'record size/cap differs'
 logs=re.findall(r'^ORDER record.*$',trace,re.M);assert len(logs)==len(blob)//128, 'record/log count differs'
 result=[];previous=0
 for index,(row,line) in enumerate(zip(struct.iter_unpack('<26IQIIQ',blob),logs),1):
  pc,cpsr=row[:2];regs=row[2:18];driver,state,o0,o1,flags,retired,frame,saved_lr=row[18:26];cycles,after_pc,after_cpsr,after_cycles=row[26:]
  assert pc in PCS and cpsr&0x3f==0x13 and regs[15]==pc and cycles>=previous;previous=cycles
  assert driver==0xccaa2c00
  if pc!=0xc0277284:assert regs[0 if pc in (0xc0274778,0xc0272224) else 4 if pc==0xc0274878 else 5]==driver
  assert 0<=flags<=65535 and after_cycles==cycles+1
  successor=pc+4;mode=0x13
  if pc==0xc027604c:successor=saved_lr&(~1 if saved_lr&1 else ~3);mode|=(saved_lr&1)<<5
  expected=(after_cpsr&0x3f==mode and after_pc==successor)
  if pc in (0xc027602c,0xc027604c):assert frame==regs[7] and frame<=0xfffffff8
  else:assert frame==saved_lr==0
  assert retired==int(expected), 'retirement attribution differs'
  if pc==0xc0274778:assert regs[1]<=3, 'refill index out of range'
  wanted=f'ORDER record{index} retired{retired} pc{pc:08x} cpsr{cpsr:08x} cycles{cycles} afterPC{after_pc:08x} afterCPSR{after_cpsr:08x} afterCycles{after_cycles} driver{driver:08x} state{state:08x} output0{o0:08x} output1{o1:08x} flags{flags:04x} frame{frame:08x} savedLR{saved_lr:08x}'+''.join(f' r{i}={r:08x}' for i,r in enumerate(regs))
  assert line==wanted, 'record/log bytes differ'
  result.append({'pc':f'{pc:08x}','cpsr':f'{cpsr:08x}','cycles':cycles,'retired':bool(retired),'afterPC':f'{after_pc:08x}','afterCPSR':f'{after_cpsr:08x}','driverState':state,'outputWords':[o0,o1],'flags24d24e':[flags&255,flags>>8],'frame':f'{frame:08x}','savedLR':f'{saved_lr:08x}','refillIndex':regs[1] if pc==0xc0274778 and retired else None,'registers':[f'{r:08x}' for r in regs]})
 assert f'ORDER summary records{len(result)} cap128' in trace
 return result

def run(out):
 runpy.run_path(str(Path(__file__).with_name('original-return-oracle.py')))
 out=Path(out);trace=(out/'trace.log').read_text();rows=records((out/'ordering.bin').read_bytes(),trace)
 assert json.loads((out/'first/functional-audit.json').read_text())['passed'] and json.loads((out/'second-request-audit.json').read_text())['passed'] and json.loads((out/'third-request-audit.json').read_text())['passed']
 frames={r['frame']:r['savedLR'] for r in rows if r['pc']=='c027602c' and r['retired']}
 for r in rows:
  if r['pc']=='c027604c' and r['retired']:assert frames.get(r['frame'])==r['savedLR'], 'saved return frame differs'
 entry=[r for r in rows if r['pc']=='c0272224' and r['retired']];wrapper=[r for r in rows if r['pc']=='c0277284' and r['retired']]
 actual4=list(re.finditer(r'^AMC W 38500004 width4 value00000001 .*$',trace,re.M));actual400=list(re.finditer(r'^AMC W 38500b0c width4 value00000400 .*$',trace,re.M))
 # Report partial or absent startup honestly. A method entry cannot replace its actual writes.
 report={'passed':True,'scope':'Actual bounded instruction/start observations. This does not replace separate PCM consumption or MMIO witnesses.','records':rows,'startMethodEntries':len(entry),'serviceWrapperEntries':len(wrapper),'actualServiceWrites':len(actual4),'actual400Enables':len(actual400),'allStartWitnessKindsPresent':bool(entry and wrapper and actual4 and actual400)}
 (out/'ordering-audit.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':run(sys.argv[1])
