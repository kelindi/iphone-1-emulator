from pathlib import Path
import importlib.util,json,struct,copy
b=Path(__file__).parent;spec=importlib.util.spec_from_file_location('saveaudit',b/'save-audit.py');a=importlib.util.module_from_spec(spec);spec.loader.exec_module(a)
code=a.original_code();words=[1,1,0,17770,0,83304,0];context=bytearray(64);struct.pack_into('<7I',context,0,*words);sram=bytes((i*13)&255 for i in range(0x2c000));copies=sram[0x28000:0x2c000]+sram[0x14000:0x1c000]
j={'prefixSaved':1,'owned':1,'phase':9,'resourceUnchanged':1,'fieldsAvailable':1,'generation':1,'copied':83304,'decoded':17770,'queue':65534,'cycles':9874729200,'reads':[],'availability':{'context':1,'dmaFields':1,'firstSRAM':1,'secondSRAM':1,'stopCode':1}};trace=''
for i,(off,pc,value) in enumerate(zip([0x410+4*i for i in range(7)]+[0x188,0x404],[0xc0277dfc]*7+[0xc0277144,0xc0277e4c],words+[0,0])):
 r={'off':off,'pc':pc,'value':value,'cpsr':0x60000013,'opcode':int.from_bytes(code(pc),'little'),'cycles':9874729099+i*8};j['reads'].append(r);trace+=f"SAVE READ index{i} offset{off:03x} value{value:08x} pc{pc:08x} cpsr{r['cpsr']:08x} opcode{r['opcode']:08x} cycles{r['cycles']} returned1\n"
assert a.receipts(j,context,copies,sram,trace,code)==words
checks=1
for which in ('word','copies','missing','opcode','thumb','generation','resource','trace','availability'):
 k=copy.deepcopy(j);c=bytearray(context);p=bytearray(copies);t=trace
 if which=='word':c[0]^=1
 if which=='copies':p[23]^=1
 if which=='missing':k['reads'].pop()
 if which=='opcode':k['reads'][0]['opcode']^=1
 if which=='thumb':k['reads'][0]['cpsr']^=32
 if which=='generation':k['generation']+=1
 if which=='resource':k['resourceUnchanged']=0
 if which=='availability':k['availability']['secondSRAM']=0
 if which=='trace':t=t.replace('returned1','returned0',1)
 try:a.receipts(k,c,p,sram,t,code)
 except AssertionError:checks+=1
 else:raise AssertionError('accepted '+which)
print('PASS',checks,'synthetic auditor controls using original instruction bytes; no guest evidence')
