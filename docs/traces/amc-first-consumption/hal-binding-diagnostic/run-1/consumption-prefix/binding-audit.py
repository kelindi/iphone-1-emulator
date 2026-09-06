"""Independent binding diagnostic. Cloned translations are never live fetches."""
from pathlib import Path
import json,re,struct,sys,hashlib

def first_level(cp,va):
 sctlr,t0,t1,n,*_=cp;n&=7;low=n==0 or va>>(32-n)==0
 return ((t0&((0xffffffff<<(14-n))&0xffffffff))|(((va>>20)&((1<<(12-n))-1))<<2)) if low else ((t1&0xffffc000)|((va>>20)<<2))

def fresh_walk(j,w):
 """Independently classify actual legacy translation misses, without fetching."""
 cp=j['cp15'];sctlr,t0,t1,ttbcr,dacr=cp[:5];xp=bool(sctlr&(1<<23));tables=list(w['tables']);cursor=0
 def permissions(ap,apx,page,xn,dac,domain):
  sr=(sctlr>>8)&3
  af=xp and bool(sctlr&(1<<29)) and not (ap&1) and not (not apx and ap==0 and sr in (1,2))
  if af:return (6 if page else 3)|(domain<<4)
  if dac in (0,2):return (11 if page else 9)|(domain<<4)
  permitted=ap>=2 or (not apx and ap==0 and sr==2)
  if dac==1 and (not permitted or (w['access']==2 and xp and xn)):return (15 if page else 13)|(domain<<4)
  return 0
 def word(address):
  nonlocal cursor
  assert cursor<len(tables),'missing actual cloned page-table read';a,value,available=tables[cursor];cursor+=1
  assert a==address and available==1,'actual cloned page-table provenance differs';return value
 for va,count,pa,fault,available in w['spans']:
  if not (sctlr&1):expected_pa=va;expected_fault=0
  else:
   n=ttbcr&7;low=n==0 or va>>(32-n)==0
   if ttbcr&(1<<(4 if low else 5)):
    expected_fault=5;expected_pa=0
   else:
    l1=word(first_level(cp,va));kind=l1&3;domain=(l1>>5)&15;dac=(dacr>>(2*domain))&3;expected_pa=0
    if kind in (0,3):expected_fault=5
    elif kind==1:
     l2=word((l1&0xfffffc00)|((va>>10)&0x3fc));typ=l2&3
     if not typ:expected_fault=7|(domain<<4)
     else:
      ap=((l2>>4)&3) if xp or typ==3 else ((l2>>(4+2*((va>>(14 if typ==1 else 10))&3)))&3)
      apx=xp and bool(l2&(1<<9));xn=bool(l2&(1<<15)) if typ==1 else bool(l2&1)
      expected_fault=permissions(ap,apx,True,xn,dac,domain)
      if not expected_fault:expected_pa=((l2&0xffff0000)|(va&0xffff)) if typ==1 else ((l2&0xfffff000)|(va&0xfff))
    elif kind==2 and not (l1&(1<<18)):
     ap=(l1>>10)&3;apx=xp and bool(l1&(1<<15));xn=bool(l1&16)
     expected_fault=permissions(ap,apx,False,xn,dac,domain)
     if not expected_fault:expected_pa=(l1&0xfff00000)|(va&0xfffff)
    else:raise AssertionError('unreviewed actual fresh section format')
  assert fault==expected_fault,'cloned fault kind differs from actual tables'
  if fault:assert not available and pa==0,'faulted clone fabricated a mapping'
  else:assert available and pa==expected_pa,'cloned physical translation differs'
 assert cursor==len(tables),'extra cloned page-table read'

def run(p):
 p=Path(p);j=json.loads((p/'binding.json').read_text());trace=(p/'trace.log').read_text();old=Path('.context/amc-first/stage-y/run-1');stripped=''.join(line for line in trace.splitlines(True) if not line.startswith('BINDING '))
 assert stripped==(old/'trace.log').read_text(),'diagnostic changed actual Y trace'
 for f in old.glob('*.bin'):assert (p/f.name).read_bytes()==f.read_bytes(),'diagnostic changed actual Y bytes: '+f.name
 for f in old.glob('transport-*.json'):
  if f.name!='transport-audit.json':assert (p/f.name).read_bytes()==f.read_bytes(),'diagnostic changed transport state'
 assert (p/'suspend.json').read_bytes()==(old/'suspend.json').read_bytes(),'diagnostic changed suspended resources'
 assert (j['attempted'],j['pc'],j['cpsr'],j['cycles'],j['count'],j['pending'],j['target'])==(1,0x31236650,0x60000010,9874747384,0,0,1),'binding context differs'
 assert j['registers']==list(struct.unpack('<16I',(p/'registers.bin').read_bytes())),'actual binding registers differ'
 assert j['actualFetchExecuted'] is False and j['step']=={'cycles':9874747384,'pc':0x31236650,'cpsr':0x60000010,'active':1,'returned':0,'inGuest':1},'prefetch observation falsely retired'
 image=Path('.context/amc-first/stage-m/MeCCA.original').read_bytes()
 assert hashlib.sha256(image).hexdigest()=='15f8f049239e64f88660aeb1ce99aa07273ccd19ca636e624ff19e9a68e7f4ec','original MeCCA image pin differs'
 segments=[];offset=28
 for _ in range(struct.unpack_from('<I',image,16)[0]):
  command,n=struct.unpack_from('<II',image,offset)
  if command==1:segments.append(struct.unpack_from('<4I',image,offset+24))
  offset+=n
 original=next(image[f+j['pc']-v:f+j['pc']-v+32] for v,z,f,n in segments if v<=j['pc'] and j['pc']+32<=v+n)
 expected=next(bytes.fromhex(x['hex']) for x in json.loads((p/'post-original.json').read_text()) if x['pc']==j['pc'])
 assert j['expectedAvailable']==1 and bytes.fromhex(j['expectedHex'])==original==expected==(p/'binding-expected.bin').read_bytes(),'original binding window differs'
 log=re.search(r'^BINDING reason(\d+) pc([0-9a-f]+) cpsr([0-9a-f]+) cycles(\d+) count(\d+) pending(\d+) originalWindowAvailable(\d+) actualFetchExecuted0 cp15 (.+)$',trace,re.M);assert log,'actual binding receipt absent'
 assert int(log[1])==j['reason'] and [int(v,16) for v in log[8].split()]==j['cp15'],'actual binding CP15/reason differs'
 assert len(j['cp15'])==10 and len(j['walks'])==4,'diagnostic bounds differ'
 for i,w in enumerate(j['walks']):
  assert (w['access'],w['freshClone'])==((0,2,0,2)[i],int(i>=2)) and len(w['spans'])<=2 and len(w['tables'])<=8,'cloned walk bound/kind differs'
  code=bytes.fromhex(w['codeHex']);assert len(code)==32,'code byte bound differs'
  if w['available']:assert w['reason']==0 and sum(s[1] for s in w['spans'])==32 and all(s[4] for s in w['spans']),'complete code availability differs'
  else:assert code==bytes(32) and w['reason'] in (1,2,3,4,5),'unavailable code fabricated'
  for address,value,available in w['tables']:
   assert available in (0,1),'page-table availability invalid'
   if available:assert address%4==0 and 0x10000000<=address and address+4<=0x20000000,'page-table data outside actual RAM'
   else:assert value==0 and (address%4!=0 or address<0x10000000 or address+4>0x20000000),'unavailable page-table data fabricated'
  for va,n,pa,fault,available in w['spans']:
   assert 0<n<=32 and j['pc']<=va and va+n<=j['pc']+32,'diagnostic span outside attempted window'
   if available:assert not fault and 0x10000000<=pa and pa+n<=0x20000000,'available diagnostic span outside RAM'
  if w['reason']==2:assert not w['spans'] and not w['tables'],'allocation failure fabricated progress'
  if w['reason']==3:assert any(not t[2] for t in w['tables']),'table failure lacks denied RAM witness'
  if w['reason']==4:assert w['spans'] and w['spans'][-1][3] and not w['spans'][-1][4],'translation fault missing'
  if w['reason']==5:
   va,n,pa,fault,available=w['spans'][-1];assert not fault and not available and (pa<0x10000000 or pa+n>0x20000000),'physical denial lacks bounds witness'
  if i>=2 and w['reason'] in (0,4):fresh_walk(j,w)
 original=j['walks'][0];assert int(log[7])==original['available'],'actual window availability differs'
 if original['available']:
  assert j['reason']==6 and bytes.fromhex(original['codeHex'])!=expected and (p/'binding-code.bin').read_bytes()==bytes.fromhex(original['codeHex']),'actual code mismatch not established'
 else:assert j['reason']==original['reason'] and not (p/'binding-code.bin').read_bytes(),'unavailable original code fabricated'
 report={'passed':True,'scope':'Exact unchanged Y boundary; read-only cloned diagnostic only, no live instruction fetch/retirement or accepted HAL call.','reason':j['reason'],'actualOriginalWindowAvailable':bool(original['available']),'cloneReasons':[w['reason'] for w in j['walks']],'cloneFaults':[[s[3] for s in w['spans']] for w in j['walks']],'liveCP15FaultRegisters':j['cp15'][6:],'actualFetchExecuted':False,'HALAccepted':False,'sameYTraceAndResources':True}
 (p/'binding-audit.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':run(sys.argv[1])
