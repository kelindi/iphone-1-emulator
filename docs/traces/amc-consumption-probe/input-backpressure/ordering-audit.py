"""Original mapped-code/return observer plus exact unchanged Stage M run oracle."""
from pathlib import Path
import hashlib,json,re,struct,sys,runpy
MODES={0xc01533c2:0x33,0xc0272224:0x13,0x3123f7f0:0x10,0x3123cdc8:0x10,0x3123ce18:0x10,0x3123f95c:0x10}
FORMAT='<26IQIIQ9I192s144s56s32s4x';SIZE=struct.calcsize(FORMAT);assert SIZE==592

def records(blob,trace,originals):
 assert len(blob)%SIZE==0 and len(blob)<=128*SIZE,'record size/cap differs'
 logs=re.findall(r'^ORDER record.*$',trace,re.M);contexts=re.findall(r'^ORDER context.*$',trace,re.M);assert len(logs)==len(contexts)==len(blob)//SIZE,'record/log count differs'
 result=[];previous=0
 for index,(row,line,context) in enumerate(zip(struct.iter_unpack(FORMAT,blob),logs,contexts),1):
  pc,cpsr=row[:2];regs=row[2:18];driver,state,o0,o1,flags,retired,frame,saved=row[18:26];cycles,after,after_mode,end=row[26:30]
  instruction,width,obj,osize,linked,lsize,stack,ssize,after_lr=row[30:39];obytes,lbytes,sbytes,code=row[39:43]
  assert pc in MODES and cpsr&0x3f==MODES[pc] and regs[15]==pc and cycles>=previous;previous=cycles
  assert driver==0xccaa2c00 and frame==saved==0 and flags<=65535 and end==cycles+1
  assert code==originals[pc],'original mapped code differs'
  assert width==(2 if pc==0xc01533c2 else 4) and instruction==int.from_bytes(code[:width],'little')
  expected=after==(0x3123f2f8 if pc==0x3123f95c else pc+width) and after_mode&0x3f==MODES[pc]
  if pc==0x3123f95c:expected=expected and after_lr==0x3123f960;assert regs[1]==3
  if pc==0xc0272224:assert regs[0]==driver
  assert retired==int(expected),'retirement attribution differs'
  if MODES[pc]==0x10:
   decoder=pc in (0x3123cdc8,0x3123ce18);assert obj==regs[0 if pc==0x3123f95c else 4] and obj and osize==(188 if decoder else 144)
   if decoder:assert linked==struct.unpack_from('<I',obytes,0x50)[0] and linked and lsize==144
   else:assert linked==lsize==0
   if pc==0x3123f7f0:assert stack==regs[13]+8 and ssize==52
   else:assert stack==ssize==0
  else:assert obj==osize==linked==lsize==stack==ssize==0
  assert obj+osize<=2**32 and linked+lsize<=2**32 and stack+ssize<=2**32
  assert not any(obytes[osize:]+lbytes[lsize:]+sbytes[ssize:])
  wanted=f'ORDER record{index} retired{retired} pc{pc:08x} cpsr{cpsr:08x} cycles{cycles} afterPC{after:08x} afterCPSR{after_mode:08x} afterCycles{end} driver{driver:08x} state{state:08x} output0{o0:08x} output1{o1:08x} flags{flags:04x} frame{frame:08x} savedLR{saved:08x}'+''.join(f' r{i}={r:08x}' for i,r in enumerate(regs))
  assert line==wanted,'record/log bytes differ'
  wanted=f'ORDER context{index} instruction{instruction:08x} bytes{width} object{obj:08x} objectBytes{osize} linked{linked:08x} linkedBytes{lsize} stack{stack:08x} stackBytes{ssize} afterLR{after_lr:08x} objectHex{obytes.hex()} linkedHex{lbytes.hex()} stackHex{sbytes.hex()} codeHex{code.hex()}'
  assert context==wanted,'context/log bytes differ'
  record={'pc':f'{pc:08x}','cycles':cycles,'cpsr':f'{cpsr:08x}','retired':bool(retired),'afterPC':f'{after:08x}','afterCPSR':f'{after_mode:08x}','afterLR':f'{after_lr:08x}','r0':regs[0],'object':f'{obj:08x}','linked':f'{linked:08x}','codeSHA256':hashlib.sha256(code).hexdigest()}
  if pc==0x3123f7f0:record.update(scalarOutputCount=struct.unpack_from('<I',sbytes,48)[0],scalarOutputs=list(struct.unpack_from('<4Q',sbytes)),interfaceConnection=struct.unpack_from('<I',obytes,8)[0],interfaceSRAM=f'{struct.unpack_from("<I",obytes,0x84)[0]:08x}')
  if pc in (0x3123cdc8,0x3123ce18):record.update(inputIndex=struct.unpack_from('<I',obytes,0x6c)[0],finalInputIndex=struct.unpack_from('<I',obytes,0x74)[0],interfaceOutputPointers=[f'{v:08x}' for v in struct.unpack_from('<II',lbytes,0x64)],interfaceOutputCapacities=list(struct.unpack_from('<II',lbytes,0x6c)))
  result.append(record)
 assert f'ORDER summary records{len(result)} cap128' in trace
 return result

def run(out):
 out=Path(out);trace=(out/'trace.log').read_text();originals=runpy.run_path(str(Path(__file__).with_name('original-context-oracle.py')))['originals']()
 rows=records((out/'ordering.bin').read_bytes(),trace,originals)
 hashes={}
 assert json.loads((out/'later-audit.json').read_text())['passed']
 for name in ('first/functional-audit.json','second-request-audit.json','third-request-audit.json','queue-audit.json'):assert json.loads((out/name).read_text())['passed']
 report={'passed':True,'scope':'Mapped original code, bounded actual return records and exact Stage N preadmission trace/state. No additional input or playback claim.','records':rows,'stateSHA256':hashes,'observedRetiredSites':[r['pc'] for r in rows if r['retired']]}
 (out/'ordering-audit.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':run(sys.argv[1])
