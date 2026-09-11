from pathlib import Path
import re,json,hashlib,collections,copy
root=Path('.context/core-hotspot/binding')
oldroot=Path('.context/jit-attribution/renumber-proof')
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
proof=json.loads((oldroot/'proof.json').read_text())
assert proof['passed'] and proof['productionSymbols']['81']=='arm_step'
def leb(data,pos):
 value=0;shift=0
 while True:
  byte=data[pos];pos+=1;value|=(byte&127)<<shift
  if byte<128:return value,pos
  shift+=7
def sections(path):
 data=Path(path).read_bytes();pos=8;out=[]
 while pos<len(data):
  start=pos;tag=data[pos];size,pos=leb(data,pos+1);out.append((tag,data[pos:pos+size],data[start:pos+size]));pos+=size
 return out
wat_bindings=[]
for label,path in [('old','.context/jit-attribution/no-map/emu.wasm'),('current','web/dist/emu.wasm')]:
 actual=sections(path);assembled=sections(root/(label+'-roundtrip.wasm'))
 assert [r for t,p,r in actual if t!=12]==[r for t,p,r in assembled]
 counts=[p for t,p,r in actual if t==12];assert len(counts)==1
 declared,_=leb(counts[0],0);data=next(p for t,p,r in actual if t==11);segments,_=leb(data,0);assert declared==segments==105
 # WABT omits optional DataCount in absence of memory.init/data.drop. Reinsert
 # only that independently counted, exact original section at original place.
 iterator=iter(assembled);rebuilt=b'\x00asm\x01\x00\x00\x00'+b''.join(r if t==12 else next(iterator)[2]for t,p,r in actual)
 assert rebuilt==Path(path).read_bytes()
 (root/(label+'-roundtrip-with-datacount.wasm')).write_bytes(rebuilt)
 wat_bindings.append({'module':label,'fullByteExactAfterExplicitDataCountRestoration':True,'dataCount':segments,'sha256':sha(path)})
assert wat_bindings[0]['sha256']==proof['productionSha256']
assert wat_bindings[1]['sha256']=='3b43ccf4a8292a4da776491aa92185b0ea88e2fc38a3002a6e1fe1d5a6aa4463'
def forms(path):
 fs=re.split(r'\n(?=  \()',Path(path).read_text())[1:];fs[-1]=fs[-1].removesuffix(')\n')
 return {int(re.match(r'  \(func \(;(\d+);\)',x)[1]):x for x in fs if x.startswith('  (func ')},[x for x in fs if not x.startswith('  (func ')]
a,ao=forms(oldroot/'production.wat');b,bo=forms(root/'current.wat')
assert a.keys()==b.keys()and len(a)==223
# Full imports, exact types, exports and indirect table/element targets are unchanged.
struct=lambda xs:[x for x in xs if not x.startswith(('  (data ','  (global '))]
assert struct(ao)==struct(bo)
assert Path('.context/jit-attribution/no-map/emu.js').read_bytes()==Path('web/dist/emu.js').read_bytes()
call=lambda x:re.findall(r'\b(?:call|return_call|ref\.func) (\d+)\b',x)
# Unique non-address instruction fingerprint, anchored by exact old symbol proof.
# Remove only i32 constant operands for this uniqueness check, never for acceptance.
def shape(x):
 x=re.sub(r'\(func \(;\d+;\)','(func',x,count=1)
 return re.sub(r'(\bi32.const )-?\d+',r'\1@',x)
fingerprint=shape(a[81])
assert [i for i,x in a.items()if shape(x)==fingerprint]==[81]
assert [i for i,x in b.items()if shape(x)==fingerprint]==[81]
def compare(old,new,cpu_only=True):
 x,y=old.splitlines(),new.splitlines();assert len(x)==len(y)
 diffs=[]
 for line,(left,right)in enumerate(zip(x,y),1):
  if left==right:continue
  l=re.fullmatch(r'(\s*i32.const )(-?\d+)',left);r=re.fullmatch(r'(\s*i32.const )(-?\d+)',right)
  assert l and r and l[1]==r[1],('nonconstant change',line)
  before,after=int(l[2]),int(r[2]);assert after-before==48,('wrong relocation',line,before,after)
  # Every changed immediate is inside the same statically located CPU object.
  assert (23352<=before<=90904 if cpu_only else 21984<=before<5246912),('out-of-static address',line)
  diffs.append({'line':line,'old':before,'current':after})
 assert call(old)==call(new)
 return diffs
main=compare(a[81],b[81]);direct=sorted(set(map(int,call(a[81]))));callees=[]
for i in direct:
 changes=compare(a[i],b[i],cpu_only=False);callees.append({'index':i,'oldProvenName':proof['productionSymbols'][str(i)],'changedAddressOccurrences':len(changes),'directCallsUnchanged':True,'relocatedStaticAddresses':sorted({x['old']for x in changes})})
# Global stack/base relocation independently agrees with the +48 shift. The RTC
# data segment change is retained, not claimed semantically equal.
global_a=[x for x in ao if x.startswith('  (global ')];global_b=[x for x in bo if x.startswith('  (global ')];assert len(global_a)==len(global_b)==1
assert global_a[0].replace('5246912','5246960')==global_b[0]
negative=[]
for name,mutated in [('wrong_call',b[81].replace('call 28','call 27',1)),('wrong_constant',b[81].replace('i32.const 23468','i32.const 23469',1)),('wrong_opcode',b[81].replace('i32.load','i32.load8_u',1)),('removed_instruction',b[81].replace('    local.set 0\n','',1))]:
 try:compare(a[81],mutated)
 except AssertionError as e:negative.append({'name':name,'rejected':True,'reason':str(e)})
 else:raise AssertionError('Negative accepted '+name)
source=Path('core/src/arm/arm_interp.c');frozen=Path('.context/rtc-write/web-source/core/src/arm/arm_interp.c');assert sha(source)==sha(frozen)
report={'passed':True,'scope':'Bounded symbol identity of current3b43 index81 as arm_step, not whole-module semantic equality. The full function and its16direct callees retain all types/instruction order/control flow/call targets exactly, except enumerated +48 static-address relocations (all811 in arm_step itself are CPU-object references). A unique nonconstant structural fingerprint independently identifies81. Imports/JS bindings/types/exports/indirect tables are exact. Changed data/device/snapshot functions are not normalized or declared equivalent.','name':'arm_step','index':81,'source':str(frozen),'sourceLine':2759,'sourceSha256':sha(source),'oldCoreSha256':proof['productionSha256'],'currentCoreSha256':sha('web/dist/emu.wasm'),'oldProofSha256':sha(oldroot/'proof.json'),'oldProofSourceSha256':sha(oldroot/'prove.py'),'oldWatSha256':sha(oldroot/'production.wat'),'currentWatSha256':sha(root/'current.wat'),'roundtripBothByteExactAfterExplicitDataCountRestoration':True,'watBindings':wat_bindings,'functionBodyOccurrencesChanged':len(main),'distinctRelocatedAddresses':len({x['old']for x in main}),'addressRelocations':main,'allDirectCallSites':call(a[81]),'directCallees':callees,'importsTypesExportsTableAndGlueExact':True,'initialStackOld':5246912,'initialStackCurrent':5246960,'negativeControls':negative,'limitations':['Address relocation proof establishes source/function identity and unchanged instruction sequence, not identical absolute guest-core memory layout.','Direct callees have the same bounded relocation proof; transitive device behavior is not claimed unchanged.','Self samples include inlined Thumb decoding, condition/decode, register/flag operations, fetch-cache checks and exception paths, not only ARM route selection. Separate noninlined helpers have separate exclusive samples.']}
(root/'proof.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:report[k]for k in ['passed','name','index','functionBodyOccurrencesChanged','distinctRelocatedAddresses','roundtripBothByteExactAfterExplicitDataCountRestoration']}));print(callees)
