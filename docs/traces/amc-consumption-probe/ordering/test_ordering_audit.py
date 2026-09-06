"""Synthetic auditor records, never presented as a guest trace."""
from pathlib import Path
import importlib.util,struct
p=Path(__file__).with_name('ordering-audit.py');spec=importlib.util.spec_from_file_location('order_audit',p);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
regs=[0]*16;regs[0]=0xccaa2c00;regs[1]=2;regs[15]=0xc0274778
row=[0xc0274778,0xa0000013,*regs,0xccaa2c00,3,1,0,0x100,1,100,0xc027477c,0xa0000013,101]
line='ORDER record1 retired1 pcc0274778 cpsra0000013 cycles100 afterPCc027477c afterCPSRa0000013 afterCycles101 driverccaa2c00 state00000003 output000000001 output100000000 flags0100'+''.join(f' r{i}={r:08x}' for i,r in enumerate(regs))+'\nORDER summary records1 cap128\n'
blob=struct.pack('<24IQIIQ',*row);assert m.records(blob,line)[0]['refillIndex']==2;checks=1
for index,value in [(1,0xa0000033),(23,0),(25,0xc0274778),(26,0x12),(27,102),(3,3),(18,0xccaa2c04)]:
 bad=row.copy();bad[index]=value
 try:m.records(struct.pack('<24IQIIQ',*bad),line)
 except AssertionError:checks+=1
 else:raise AssertionError(f'mutation{index} accepted')
try:m.records(blob*129,line)
except AssertionError:checks+=1
else:raise AssertionError('cap accepted')
print(f'ordering auditor: {checks} synthetic record/retirement/cap checks pass')
