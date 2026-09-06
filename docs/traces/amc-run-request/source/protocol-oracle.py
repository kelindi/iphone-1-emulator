"""Check actual command transcript independently of the C admission arrays."""
from pathlib import Path
import json,re,runpy,sys
k=runpy.run_path(str(Path(__file__).with_name('kernel-oracle.py')))
run=Path(sys.argv[1]);text=(run/'trace.log').read_text();u32=k['u32']
# The final constants are read from the original kernel's actual literal pool.
expected=[(0x101c,1,0xc027814c),(0x100c,0,0xc027815c),(0x1000,0,0xc0278168),(0x1004,0,0xc0278174),(0x1008,0,0xc0278180),(0xa00,0,0xc0278188)]
for i in range(4):expected.extend([(0x300+4*i,0,0xc0278320),(0x310+4*i,0,0xc0278330)])
expected.extend([(0x320,0,0xc027834c),(0x324,0,0xc027834c),(0x300,u32(0xc02783d4),0xc0278398),(0x304,u32(0xc02783d8),0xc02783a4),(0xd0c,0x40,0xc02783b4)])
assert u32(0xc029c844+8)==0xc02782fc
sentinel=(0,1,0xc0277fa0)
post=text.split('OBSERVE upload marker verified;',1)[1]
actual=[(int(a,16)-0x38500000,int(v,16),int(pc,16)) for a,v,pc in re.findall(r'AMC W ([0-9a-f]+) width4 value([0-9a-f]+) pc([0-9a-f]+)',post)]
assert actual==expected+[sentinel]
task=re.search(r'TASK object([0-9a-f]+) vtable([0-9a-f]+) method([0-9a-f]+) argument(\d+)',post)
assert task
obj,vt,method=[int(v,16) for v in task.groups()[:3]];arg=int(task.group(4))
assert vt==0xc029c844 and method==u32(vt+8) and arg==0
assert 'postWrites19 status00000100 inputPending0' in text
assert 'FINAL result3 cycles9873013642 ticks1756767243' in text
assert (run/'sram.bin').read_bytes()==(run.parent/'first-101c/sram.bin').read_bytes()
# Deliberate protocol failures cannot pass by sharing the same zero values.
negatives=[]
for name,bad in [('missing acknowledgement',actual[1:]),('duplicate acknowledgement',[actual[0]]+actual),('early run',[sentinel]+actual),('wrong zero order',[actual[0],actual[2],actual[1]]+actual[3:])]:
 assert bad!=expected+[sentinel];negatives.append(name)
bad=list(actual);bad[16]=(bad[16][0],bad[16][1]^1,bad[16][2]);assert bad!=expected+[sentinel];negatives.append('wrong original task constant')
r={'pass':True,'scope':'Exact original-code-derived MMIO prefix and preserved SRAM; request observed but not accepted','acceptedPostUploadWrites':19,'unacceptedCommandAddress':'0x38500000','unacceptedCommandValue':1,'stopPC':'0xc0277fa0','markerToRunRetiredInstructions':159,'taskObject':hex(obj),'taskVtable':hex(vt),'taskMethod':hex(method),'taskArgument':arg,'rawCodecParameterWords':[2,2,48000,128000,0,0,0],'unprovenFieldLabels':'Only codec type2 is independently named; other parameter words remain raw','negativeControls':negatives,'expectedWrites':[{'offset':hex(o),'value':hex(v),'pc':hex(pc)} for o,v,pc in expected]}
(run/'protocol-oracle.json').write_text(json.dumps(r,indent=2)+'\n')
print('Original-prefix oracle PASS:19 accepted writes,1 unaccepted command,5 negative controls.')
