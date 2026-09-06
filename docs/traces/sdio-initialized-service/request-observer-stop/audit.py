"""Independent actual original-request audit. No model or C gate import.

Synthetic tests use the genuine immutable077 prefix plus an explicitly synthetic
suffix. Those tests are parser contracts, never labeled actual continuation.
"""
from pathlib import Path
import hashlib,json,struct,sys,re
import prefix_audit
PREFIX_EVENTS=22919
PREFIX_SHA='c92482f0414171909c7f453e322e8015263f5e6038eb42769eb6c03b794eac82'

def retained(out):
 old=Path('.context/sdio-first/checkpoint-v2/full-attempt-1');result={}
 for a,b in {'low.bin':'full-low.bin','high.bin':'full-high.bin','low-mask.bin':'full-low-mask.bin','high-mask.bin':'full-high-mask.bin','handoff.bin':'full-handoff-frame.bin','helper.bin':'full-received-helper.bin'}.items():
  value=(out/a).read_bytes();assert value==(old/b).read_bytes(),a
  result[a]={'bytes':len(value),'sha256':hashlib.sha256(value).hexdigest()}
 return result

def audit(out):
 out=Path(out);r=json.loads((out/'report.json').read_text())
 if r['mode']=='control':return prefix_audit.audit(out)
 q=json.loads((out/'request.json').read_text());g=json.loads((out/'gate-mode-pair.json').read_text())
 assert r['mode']=='positive' and r['passed'] and r['normalOutsideRunStop'] and not r['timeout']
 assert r['instructionLimit']==100000000 and 235719<r['instructions']<=100000000
 assert r['totalInstructions']==17517070017+r['instructions']
 assert 0<=r['loadSeconds']<=120 and 0<=r['runSeconds']<=90 and r['runtimePhase']==2
 assert r['dataCompletions']==504 and r['imageUnchanged'] and r['admissionDeviceUnchanged'] and r['mainBoundary'] and r['mainReturn'] and r['verifyEntered']
 assert q['passed']==1 and q['observerInvalid']==0 and q['prefix']==1 and q['prefixInstructions']==235719 and q['prefixTransactions']==PREFIX_EVENTS
 assert q['prefixCycles']==17517305736
 assert int(g['cpsr'],16)&63==19 and q['finalCPSR']==int(g['cpsr'],16) and q['finalPC']==int(r['pc'],16)==0xc0431528
 assert q['previousPC']==q['triggerPC']==0xc0431524 and q['previousCPSR']&63==19 and q['triggerCPSR']&63==19 and q['lastCount']==1 and q['lastInstructionOK']==1
 sequence=[q[k] for k in ('prefixCycles','reg6Cycles','returnCycles','overwriteCycles','verifyCycles','setupCycles','builderCycles','frameCycles','triggerCycles','finalCycles')]
 assert all(a<b for a,b in zip(sequence,sequence[1:])) and sequence[-1]==r['totalInstructions']
 assert 0<=q['ignoredWrapperReturn']<=0xffffffff
 assert q['reg6Value']==0x1100 and q['requestCount']==1 and q['setupCardMask']==3
 assert q['hostBlockCommitted']==1 and q['hostBlockObject'] and q['verifyCycles']<q['hostBlockCycles']<q['setupCycles']
 assert q['argument']==0x9a000001 and q['descriptor']==q['pendingCommand']==0x501f5 and q['pendingArgument']==q['argument']
 # Independent bit extraction rather than importing the native predicate.
 a=q['argument'];fields={'write':a>>31,'function':(a>>28)&7,'block':(a>>27)&1,'increment':(a>>26)&1,'address':(a>>9)&0x1ffff,'count':a&511}
 assert fields=={'write':1,'function':1,'block':1,'increment':0,'address':0x10000,'count':1}
 assert q['blockLength']==q['cardBlockSize']==320 and q['blockCount']==1 and q['source']==q['physical'] and q['source'] and q['source']%4==0 and q['source']<=0xffffffff-319
 assert q['capacity']>=320 and q['offset']==0 and q['logical']==18 and q['busy']==1 and q['now']==r['ticks']<q['deadline']
 assert q['finalCompletions']==q['triggerCompletions'] and q['finalDataCompletions']==q['triggerDataCompletions']==504 and q['finalDataStarts']==q['triggerDataStarts']
 assert q['dataArmed']==q['dataActive']==q['endpointUsed']==q['slot0Phase']==q['slot1Phase']==q['slot0Used']==q['slot1Used']==0 and q['runtimeReady']==1
 assert q['mainPhase']==7 and q['finalDataStarts']==504
 assert q['finalEvents']==q['triggerEvents'] and q['finalIRQ']==q['triggerIRQ']
 for name in ('runtime-slot0.bin','runtime-slot1.bin'):
  assert (out/name).read_bytes()==b'\xa5'*16+b'\0'*320+b'\xa5'*16
 virtual=(out/'virtual-frame.bin').read_bytes();physical=(out/'physical-source.bin').read_bytes();later=(out/'virtual-trigger.bin').read_bytes()
 assert len(virtual)==320 and virtual==physical==later
 n,kind,zero,opcode,size,seq,result=struct.unpack_from('<HBBHHHH',virtual)
 assert (n,kind,zero,size,seq,result)==(18,1,0,14,q['sequence'],0) and opcode==q['opcode'] and opcode in (0x93,0x43)
 builder=q['builders'][opcode==0x43]
 assert builder['entered']==builder['finished']==1 and builder['driver']==q['driver'] and builder['object']==q['object'] and builder['buffer']==q['virtual'] and builder['sequence']==seq and builder['cycles']==q['builderCycles']
 if opcode==0x93:assert q['pulldowns'] and builder['args']==[1,65535,0] and virtual[12:18]==bytes.fromhex('0100ffff0000')
 else:assert builder['args'][0]==6 and virtual[12:18]==bytes.fromhex('0600000004ff')
 # Capture metadata is bound to the independently parsed original witness log.
 log=(out/'milestones.log').read_text()
 assert re.search(r'original host block-size setter committed pc=c037a1c8 object=%08x blockSize=320 cycles=%d'%(q['hostBlockObject'],q['hostBlockCycles']),log)
 assert re.search(r'original register6 wrapper return at c0380220 r0=%08x cycles=%d \(recorded, not assumed false\)'%(q['ignoredWrapperReturn'],q['returnCycles']),log)
 assert re.search(r'original framed packet pc=c0380804 object=%08x virtual=%08x physical=%08x capacity=%d offset=0 logical=18 physicalLength=320 sequence=%d opcode=%04x cycles=%d'%(q['object'],q['virtual'],q['physical'],q['capacity'],seq,opcode,q['frameCycles']),log)
 raw=(out/'transactions.bin').read_bytes();assert len(raw)==r['transactions']*32
 assert hashlib.sha256(raw[:PREFIX_EVENTS*32]).hexdigest()==PREFIX_SHA
 cycles0=17517070017;ticks0=256764208;argument=None;pending=None;reads=[];triggers=[];setup_writes=[];controller={};reg6=[]
 for index,(cycles,ticks,pc,address,value,flags) in enumerate(struct.iter_unpack('<QQIIII',raw)):
  assert cycles0<=cycles<=r['totalInstructions'] and ticks0<=ticks<=r['ticks'];cycles0,ticks0=cycles,ticks
  assert flags&256 and flags&~0x1ff==0 and ((flags>>1)&127)==4
  if flags&1:
   if address==0x38d0000c:argument=value
   elif address==0x38d00008 and value&0x80000000:
    assert pending is None and argument is not None
    pending=(argument,value&0x7fffffff,cycles,ticks,pc);triggers.append(pending)
    if pending[1]==0x501f5:
     assert index==len(raw)//32-1 and pc==0xc0431524 and cycles==q['triggerCycles'] and argument==q['argument']
     assert controller.get(0x44)==q['source'] and controller.get(0x48)==320 and controller.get(0x4c)==1
    else:assert pending[1]==0x50074
   elif address==0x38d00004:assert value in (0,1,2,3) # FIFO setup is not DMA start.
   if address in (0x38d00044,0x38d00048,0x38d0004c):controller[address-0x38d00000]=value
  elif address==0x38d00020:
   assert pending is not None and pending[1]==0x50074 and pc==0xc0431594
   arg=pending[0];function=(arg>>28)&7;reg=(arg>>9)&0x1ffff;write=bool(arg>>31)
   assert value&0x1000
   if not write and function==1 and reg==6:
    assert value==0x1100 and cycles==q['reg6Cycles'];reg6.append((arg,value,cycles))
   else:assert not value&0x0b00
   if write:setup_writes.append((function,reg,arg&255,cycles))
   reads.append((function,reg,write,value,cycles));pending=None
 assert pending is not None and pending[1]==0x501f5 and len(triggers)==len(reads)+1 and len(reg6)==1
 assert q['finalCompletions']==3414+len(reads)
 assert [(f,reg,w,v) for f,reg,w,v,c in reads[:6]]==[(1,0x20,False,0x1009),(1,0x10,False,0x1000),(1,0x11,False,0x1000),(1,0x34,False,0x10dc),(1,0x35,False,0x10fe),(1,6,False,0x1100)]
 for wanted in ((1,4,3),(0,0x110,0x40),(0,0x111,1)):
  lo,hi=(q['verifyCycles'],q['setupCycles']) if wanted==(1,4,3) else (q['setupCycles'],q['triggerCycles'])
  assert any((f,reg,v)==wanted and lo<c<hi for f,reg,v,c in setup_writes),wanted
 assert len([1 for f,reg,w,v,c in reads if f==1 and not w and reg==0x20])==r['statusReads']
 assert r['readErrors']==1
 result={'passed':True,'scope':'Independent complete raw transaction/parser and requested source-byte audit only; no controller response or DMA completion for runtime CMD53','events':len(raw)//32,'traceSha256':hashlib.sha256(raw).hexdigest(),'exact077PrefixEvents':PREFIX_EVENTS,'exact077PrefixSha256':PREFIX_SHA,'register6R5':reg6,'ignoredWrapperReturn':q['ignoredWrapperReturn'],'command':opcode,'logicalBytes':18,'physicalBytes':320,'paddingBytes':302,'physicalSourceSha256':hashlib.sha256(physical).hexdigest(),'fields':fields,'completedCMD52':len(reads),'pendingCMD53':1,'retainedBytes':retained(out)}
 (out/'audit.json').write_text(json.dumps(result,indent=2)+'\n');return result
if __name__=='__main__':print(json.dumps(audit(sys.argv[1]),indent=2))
