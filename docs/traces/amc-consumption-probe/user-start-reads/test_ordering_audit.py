"""Synthetic ABI/return/code-binding checks, not actual guest evidence."""
from pathlib import Path
import importlib.util,runpy,struct
p=Path(__file__).with_name('ordering-audit.py');spec=importlib.util.spec_from_file_location('oa',p);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);originals=runpy.run_path(str(p.with_name('original-context-oracle.py')))['originals']();checks=0

def fixture(pc):
 mode=m.MODES[pc];regs=[0]*16;regs[15]=pc;regs[13]=0x08020000;regs[1]=3
 decoder=pc in (0x3123cdc8,0x3123ce18);user=mode==0x10;obj=0x08010000 if user else 0;osize=(188 if decoder else 144) if user else 0;linked=0x08011000 if decoder else 0;lsize=144 if decoder else 0;stack=regs[13]+8 if pc==0x3123f7f0 else 0;ssize=52 if stack else 0
 regs[0]=0xccaa2c00 if pc==0xc0272224 else obj if pc==0x3123f95c else 0;regs[4]=obj;ob=bytearray(192);lb=bytes(144);sb=bytearray(56)
 if decoder:struct.pack_into('<I',ob,0x50,linked)
 if stack:struct.pack_into('<4Q',sb,0,0xf220,8192,0x11220,8192);struct.pack_into('<I',sb,48,4)
 width=2 if pc==0xc01533c2 else 4;after=0x3123f2f8 if pc==0x3123f95c else pc+width;lr=0x3123f960 if pc==0x3123f95c else 0
 return [pc,mode,*regs,0xccaa2c00,0 if user else 3,0,0,0x10000 if user else 0,1,0,0,100,after,mode,101,int.from_bytes(originals[pc][:width],'little'),width,obj,osize,linked,lsize,stack,ssize,lr,bytes(ob),lb,bytes(sb),originals[pc]]
def render(r):
 pc,cpsr=r[:2];regs=r[2:18];driver,state,o0,o1,flags,retired,frame,saved=r[18:26];cycles,after,mode,end=r[26:30];ins,width,obj,on,linked,ln,stack,sn,lr=r[30:39];ob,lb,sb,code=r[39:]
 line=f'ORDER record1 retired{retired} pc{pc:08x} cpsr{cpsr:08x} cycles{cycles} afterPC{after:08x} afterCPSR{mode:08x} afterCycles{end} driver{driver:08x} state{state:08x} output0{o0:08x} output1{o1:08x} flags{flags:04x} frame{frame:08x} savedLR{saved:08x}'+''.join(f' r{i}={v:08x}' for i,v in enumerate(regs))
 context=f'ORDER context1 instruction{ins:08x} bytes{width} object{obj:08x} objectBytes{on} linked{linked:08x} linkedBytes{ln} stack{stack:08x} stackBytes{sn} afterLR{lr:08x} objectHex{ob.hex()} linkedHex{lb.hex()} stackHex{sb.hex()} codeHex{code.hex()}'
 return line+'\n'+context+'\nORDER summary records1 cap128\n'
def check(r,text=None):return m.records(struct.pack(m.FORMAT,*r),render(r) if text is None else text,originals)
def reject(r,text=None):
 global checks
 try:check(r,text)
 except AssertionError:checks+=1
 else:raise AssertionError('invalid synthetic record accepted')
for pc in m.MODES:
 row=fixture(pc);assert check(row)[0]['retired'];checks+=1
 for index,value in ((1,row[1]^0x20),(27,row[27]^4),(28,row[28]^1),(29,102)):
  bad=row.copy();bad[index]=value;reject(bad)
 bad=row.copy();data=bytearray(bad[42]);data[31]^=1;bad[42]=bytes(data);reject(bad)
 bad=row.copy();bad[1]^=0x80;reject(bad,render(row))
 bad=row.copy();bad[23]=0;bad[27]=0xffff0018;bad[28]=0x92;assert not check(bad)[0]['retired'];checks+=1
row=fixture(0x3123f95c);bad=row.copy();bad[38]^=4;reject(bad);bad=row.copy();bad[3]=2;reject(bad)
row=fixture(0x3123f7f0);bad=row.copy();sb=bytearray(bad[41]);sb[0]^=1;bad[41]=bytes(sb);reject(bad,render(row))
# A true returned error/count mismatch remains visible diagnostic data.
bad=row.copy();bad[2]=0xe00002bc;sb=bytearray(bad[41]);struct.pack_into('<I',sb,48,3);bad[41]=bytes(sb);result=check(bad)[0];assert result['r0']==0xe00002bc and result['scalarOutputCount']==3;checks+=1
bad=fixture(0x3123f7f0);bad[19]=3;reject(bad)
bad=fixture(0x3123f7f0);bad[22]=0;reject(bad)
try:m.records(bytes(m.SIZE*129),'',originals)
except AssertionError:checks+=1
else:raise AssertionError('cap accepted')
print(f'MeCCA original-code/return auditor: {checks} synthetic checks pass')
