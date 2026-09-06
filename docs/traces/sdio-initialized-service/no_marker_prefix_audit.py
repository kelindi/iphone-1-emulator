"""Audit retained incomplete negative evidence without declaring its gate passed."""
from pathlib import Path
import struct,json,hashlib
p=Path('.context/sdio-first/handoff-ready/runs/no-marker-1')
r=json.loads((p/'report.json').read_text());g=json.loads((p/'gate-mode-pair.json').read_text())
assert r['passed'] is False and r['instructions']==r['instructionLimit']==100000000
assert r['mode']=='no-marker' and r['pc']=='c014782c' and r['normalOutsideRunStop'] and not r['timeout']
assert not g['orderedFEDCPair'] # budget stop may be inside Thumb kernel scheduling; only target gates require ARM SVC
b=(p/'transactions.bin').read_bytes();assert len(b)%32==0
argument=descriptor=None;reads=[];commands=0;last_cycles=17517070017;last_ticks=256764208;data_starts=0
for cycles,ticks,pc,address,value,flags in struct.iter_unpack('<QQIIII',b):
 assert last_cycles<=cycles<=r['totalInstructions'] and last_ticks<=ticks<=r['ticks'];last_cycles,last_ticks=cycles,ticks
 assert flags&256 and ((flags>>1)&127)==4
 if flags&1:
  if address==0x38d0000c:argument=value
  elif address==0x38d00008 and value&0x80000000:descriptor=value&0x7fffffff;commands+=1
  elif address==0x38d00004 and value==0x10:data_starts+=1
 elif address==0x38d00020:
  assert descriptor==0x50074 and argument is not None and not argument&0x80000000 and (argument>>28)&7==1
  assert value&0x1000 and not value&0x0b00
  reads.append({'register':(argument>>9)&0x1ffff,'response':value,'cycles':cycles,'ticks':ticks,'pc':hex(pc)})
assert len(b)//32==r['transactions']==41254 and commands==len(reads)==9 and not data_starts
assert [(x['register'],x['response']) for x in reads]==[(0x20,0x1009),(0x10,0x1000),(0x11,0x1000)]+[(0x34,0x1000),(0x35,0x1000)]*3
assert r['mainBoundary'] and r['mainReturn'] and r['verifyEntered'] and r['imageUnchanged'] and r['dataCompletions']==504
old=Path('.context/sdio-first/checkpoint-v2/full-attempt-1')
for name,reference in {'low.bin':'full-low.bin','high.bin':'full-high.bin','low-mask.bin':'full-low-mask.bin','high-mask.bin':'full-high-mask.bin','handoff.bin':'full-handoff-frame.bin','helper.bin':'full-received-helper.bin'}.items():assert (p/name).read_bytes()==(old/reference).read_bytes()
result={'artifactChecksPassed':True,'fullNegativeGatePassed':False,'scope':'Exact retained100M-instruction prefix only;100 verifier iterations and failure return were not reached. No budget change or repeat.','events':len(b)//32,'traceBytes':len(b),'traceSha256':hashlib.sha256(b).hexdigest(),'completedZeroPairs':3,'completedReads':reads,'unexpectedRegister6Reads':0,'newDataStarts':0,'retainedImageBytesExact':True,'stop':r,'stopMode':g}
(p/'prefix-audit.json').write_text(json.dumps(result,indent=2)+'\n');print('PASS retained prefix artifact checks; full negative gate remains INCOMPLETE')
