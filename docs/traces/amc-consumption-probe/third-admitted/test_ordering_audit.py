"""Synthetic auditor records, never presented as a guest trace."""
from pathlib import Path
import importlib.util,struct
p=Path(__file__).with_name('ordering-audit.py');spec=importlib.util.spec_from_file_location('order_audit',p);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
regs=[0]*16;regs[0]=0xccaa2c00;regs[1]=3;regs[15]=0xc0274778
row=[0xc0274778,0xa0000013,*regs,0xccaa2c00,3,1,0,0x100,1,0,0,100,0xc027477c,0xa0000013,101]
line='ORDER record1 retired1 pcc0274778 cpsra0000013 cycles100 afterPCc027477c afterCPSRa0000013 afterCycles101 driverccaa2c00 state00000003 output000000001 output100000000 flags0100 frame00000000 savedLR00000000'+''.join(f' r{i}={r:08x}' for i,r in enumerate(regs))+'\nORDER summary records1 cap128\n'
blob=struct.pack('<26IQIIQ',*row);assert m.records(blob,line)[0]['refillIndex']==3;checks=1
for index,value in [(1,0xa0000033),(23,0),(27,0xc0274778),(28,0x12),(29,102),(3,4),(18,0xccaa2c04)]:
 bad=row.copy();bad[index]=value
 try:m.records(struct.pack('<26IQIIQ',*bad),line)
 except AssertionError:checks+=1
 else:raise AssertionError(f'mutation{index} accepted')
try:m.records(blob*129,line)
except AssertionError:checks+=1
else:raise AssertionError('cap accepted')
print(f'ordering auditor: {checks} synthetic record/retirement/cap checks pass')
def rendered(r):
 pc,cpsr=r[:2];regs=r[2:18];driver,state,o0,o1,flags,retired,frame,saved=r[18:26];cycles,after,mode,end=r[26:]
 return f'ORDER record1 retired{retired} pc{pc:08x} cpsr{cpsr:08x} cycles{cycles} afterPC{after:08x} afterCPSR{mode:08x} afterCycles{end} driver{driver:08x} state{state:08x} output0{o0:08x} output1{o1:08x} flags{flags:04x} frame{frame:08x} savedLR{saved:08x}'+''.join(f' r{i}={v:08x}' for i,v in enumerate(regs))+'\nORDER summary records1 cap128\n'
for pc,cpsr,saved,after,mode in [(0xc027604c,0x13,0xc01400ed,0xc01400ec,0x33),(0xc027604c,0x13,0xc027619c,0xc027619c,0x13)]:
 case=row.copy();case[0]=case[17]=pc;case[1]=cpsr;case[7]=0xccaa2c00;case[9]=case[24]=0xe28b2d18;case[25]=saved;case[27]=after;case[28]=mode
 assert m.records(struct.pack('<26IQIIQ',*case),rendered(case))[0]['retired'];checks+=1
 wrong=case.copy();wrong[27]^=4
 try:m.records(struct.pack('<26IQIIQ',*wrong),rendered(wrong))
 except AssertionError as error:assert 'retirement attribution differs' in str(error);checks+=1
 else:raise AssertionError('wrong branch/pop successor accepted')
print(f'complete ordering auditor: {checks} checks pass, including actual saved ARM/Thumb returns')

for pc in (0xc0272224,0xc0277284):
 case=row.copy();case[0]=case[17]=pc;case[27]=pc+4
 case[2]=0xccaa2c00 if pc==0xc0272224 else 0xccaa2c08
 assert m.records(struct.pack('<26IQIIQ',*case),rendered(case))[0]['retired'];checks+=1
 for after,mode in ((pc,0x13),(pc+4,0x33),(0xffff0018,0x92)):
  bad=case.copy();bad[27]=after;bad[28]=mode;bad[23]=0
  assert not m.records(struct.pack('<26IQIIQ',*bad),rendered(bad))[0]['retired'];checks+=1
print(f'Stage M ordering auditor: {checks} synthetic checks pass')
