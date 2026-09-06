"""Actual original exception/retry receipts. No descriptor or instruction injection."""
from pathlib import Path
import json,re,sys,importlib.util
spec=importlib.util.spec_from_file_location('binding_audit',Path(__file__).with_name('binding-audit.py'));ba=importlib.util.module_from_spec(spec);spec.loader.exec_module(ba)
def run(p):
 p=Path(p);j=json.loads((p/'fetch.json').read_text());initial=json.loads((p/'binding.json').read_text());trace=(p/'trace.log').read_text()
 assert j['mapped']==1 and not j['pending'] and 1<=j['count']<=8 and len(j['records'])==j['count'],'original mapped retry not reached'
 assert 0<j['cycles']-initial['cycles']<=1000000,'bounded original retry differs'
 expected=bytes.fromhex(initial['expectedHex']);faults=0
 for i,r in enumerate(j['records']):
  assert not(r['fetchPresent'] and r['fetchBlock']==r['pc']&~1023 and r['fetchGeneration']==r['tlbGeneration'] and r['fetchPrivileged']==int(r['cpsr']&31!=16)),'live fetch cache could bypass failed translation'
  pre=initial if i==0 else json.loads((p/f'fetch-attempt-{i}/binding.json').read_text())
  assert pre['reason']==4 and pre['registers']==initial['registers'] and pre['cpsr']==initial['cpsr'] and pre['cp15'][:6]==initial['cp15'][:6],'actual failed-fetch context differs'
  assert (r['start'],r['pc'],r['cpsr'],r['sctlr'],r['ifsr'],r['ifar'])==(pre['cycles'],pre['pc'],pre['cpsr'],pre['cp15'][0],pre['cp15'][7],pre['cp15'][9]),'actual pre-step differs'
  for w in pre['walks'][2:]:ba.fresh_walk(pre,w);assert not w['available'] and w['reason']==4 and w['spans'][0][3]==7,'actual failed translation differs'
  kind=3 if r['fiq'] and not r['cpsr']&64 else 2 if r['irq'] and not r['cpsr']&128 else 1
  mode,vector={1:(23,12),2:(18,24),3:(17,28)}[kind]
  cpsr=(r['cpsr']&~(63|512))|mode|128|256
  if kind==3:cpsr|=64
  if r['sctlr']&(1<<25):cpsr|=512
  vector|=0xffff0000 if r['sctlr']&8192 else 0
  ifsr,ifar=(7,r['pc']) if kind==1 else (r['ifsr'],r['ifar'])
  assert (r['kind'],r['end'],r['afterPC'],r['afterCPSR'],r['afterIFSR'],r['afterIFAR'],r['afterLR'],r['afterSPSR'],r['returned'])==(kind,r['start']+1,vector,cpsr,ifsr,ifar,r['pc']+4,r['cpsr'],1),'actual exception receipt differs'
  faults+=kind==1
  line=f"FETCH RETURN index{i} kind{kind} start{r['start']} end{r['end']} pc{r['pc']:08x} cpsr{r['cpsr']:08x} afterPC{vector:08x} afterCPSR{cpsr:08x} ifsr{ifsr:08x} ifar{ifar:08x} lr{r['pc']+4:08x} spsr{r['cpsr']:08x} emuRunReturned1 targetInstructionRetired0"
  assert trace.splitlines().count(line)==1,'actual returned exception log differs'
 assert faults==j['faults'] and faults>=1,'real original prefetch abort absent'
 q=p/f"fetch-attempt-{j['count']}";retry=json.loads((q/'binding.json').read_text())
 assert retry['reason']==0 and retry['registers']==initial['registers'] and retry['cpsr']==initial['cpsr'] and retry['cp15'][:6]==initial['cp15'][:6] and retry['cycles']==j['cycles'] and (j['pc'],j['cpsr'])==(initial['pc'],initial['cpsr']),'actual mapped retry context differs'
 assert retry['step']=={'cycles':j['cycles'],'pc':j['pc'],'cpsr':j['cpsr'],'active':1,'returned':0,'inGuest':1} and not retry['actualFetchExecuted'],'mapped target instruction falsely executed'
 assert bytes.fromhex(retry['expectedHex'])==expected==(q/'binding-expected.bin').read_bytes() and (q/'binding-code.bin').read_bytes()==expected,'actual mapped original window differs'
 with Path('.context/audio/ringtone-page.snap').open('rb') as f:base,size=ba.geometry_from_header(f.read(64))
 for i,w in enumerate(retry['walks']):
  assert w['available'] and not w['reason'] and bytes.fromhex(w['codeHex'])==expected,'actual mapped original window differs'
  assert w['access']==(0,2,0,2)[i] and w['freshClone']==int(i>=2),'mapped walk kind differs'
  for va,n,pa,fault,available in w['spans']:assert available and not fault and n==32 and va==j['pc'] and base<=pa and pa+n<=base+size,'actual mapped span differs'
  for a,v,available in w['tables']:assert available and a%4==0 and base<=a and a+4<=base+size,'actual mapped table bounds differ'
  if i>=2:ba.fresh_walk(retry,w)
 assert re.search(r'^STOP mapped original retry before instruction cycles'+str(j['cycles'])+r' ticks\d+ pc31236650 ',trace,re.M),'actual mapped retry stop absent'
 assert (p/'registers.bin').read_bytes()==(p/'fetch-prefix/registers.bin').read_bytes(),'mapped retry original registers differ'
 result={'passed':True,'actualPrefetchAborts':faults,'actualIRQDiversions':sum(r['kind']==2 for r in j['records']),'actualFIQDiversions':sum(r['kind']==3 for r in j['records']),'coreStepsAfterZ':j['cycles']-initial['cycles'],'mappedRetryPC':hex(j['pc']),'mappedRetryCycles':j['cycles'],'originalWindowBytes':32,'targetInstructionRetired':False,'HALAccepted':False,'audiblePlayback':False,'scope':'Original core exception followed by actual original mapped retry, stopped before instruction; ordinary kernel/device continuation unchanged.'}
 (p/'prefetch-audit.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':run(sys.argv[1])
