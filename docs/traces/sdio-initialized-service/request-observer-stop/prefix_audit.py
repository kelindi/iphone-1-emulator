"""Independent raw-event and retained-byte audit; does not import runtime code."""
from pathlib import Path
import hashlib,json,struct,sys
HERE=Path(__file__).resolve().parent

def audit(out):
 out=Path(out);r=json.loads((out/'report.json').read_text());g=json.loads((out/'gate-mode-pair.json').read_text())
 assert r['passed'] and r['normalOutsideRunStop'] and not r['timeout']
 assert r['instructionLimit']==100000000 and 0<r['instructions']<=100000000
 assert r['loadSeconds']<=120 and r['runSeconds']<=90 and r['dataCompletions']==504
 assert r['imageUnchanged'] and r['admissionDeviceUnchanged'] and g['armSVC']
 assert int(g['cpsr'],16)&0x3f==0x13
 sha=hashlib.sha256();events=0;old_cycles=17517070017;old_ticks=256764208;argument=None;descriptor=None;reads=[];triggers=[];transfer_starts=0
 with (out/'transactions.bin').open('rb') as f:
  while True:
   b=f.read(32*32768)
   if not b:break
   assert len(b)%32==0;sha.update(b)
   for cycles,ticks,pc,address,value,flags in struct.iter_unpack('<QQIIII',b):
    events+=1;assert old_cycles<=cycles<=r['totalInstructions'] and old_ticks<=ticks<=r['ticks'];old_cycles,old_ticks=cycles,ticks
    assert flags&256 and ((flags>>1)&127)==4
    if flags&1:
     if address==0x38d0000c:argument=value
     elif address==0x38d00008 and value&0x80000000:
      assert argument is not None;descriptor=value&0x7fffffff;triggers.append((argument,descriptor))
     elif address==0x38d00004 and value==0x10:transfer_starts+=1
    elif address==0x38d00020:
     assert argument is not None and descriptor==0x50074
     assert not argument&0x80000000 and (argument>>28)&7==1
     assert value&0x1000 and not value&0x0b00
     reads.append(((argument>>9)&0x1ffff,value,cycles,ticks,pc))
 assert events==r['transactions'] and len(triggers)==len(reads) and not transfer_starts
 assert all(d==0x50074 for a,d in triggers) and not any(reg==6 for reg,*_ in reads)
 status=[v for reg,v,*_ in reads if reg==0x20];length=[(reg,v) for reg,v,*_ in reads if reg in (0x10,0x11)];marker=[(reg,v) for reg,v,*_ in reads if reg in (0x34,0x35)]
 assert len(status)==r['statusReads'] and all(v in (0x1008,0x1009) for v in status)
 mode=r['mode']
 if mode=='control':
  assert r['pc']=='c038a8ec' and r['r4']==1000 and r['instructions']==41947204
  assert events==4581748 and sha.hexdigest()=='13881df363adc1770b768a2841874ab289f7377c5d58eb337e52569b2735eae9'
  assert status==[0x1008]*1000 and not length and not marker and len(reads)==1000
  assert not g['orderedFEDCPair'] and not r['mainReturn'] and not r['verifyEntered']
 else:
  assert r['mainBoundary'] and r['mainReturn'] and r['verifyEntered'] and 0<len(status)<1000 and status[-1]==0x1009
  assert length==[(0x10,0x1000),(0x11,0x1000)]
  if mode=='positive':
   assert r['pc']=='c0380208' and g['orderedFEDCPair']
   assert marker[-2:]==[(0x34,0x10dc),(0x35,0x10fe)] and [(a,b) for a,b,*_ in reads][-2:]==marker[-2:]
   assert r['runtimePhase']==2
  else:
   assert mode=='no-marker' and r['pc']=='c0380224' and r['r0']==0 and not g['orderedFEDCPair']
   assert marker==[(0x34,0x1000),(0x35,0x1000)]*100
 bank_files={'low.bin':'full-low.bin','high.bin':'full-high.bin','low-mask.bin':'full-low-mask.bin','high-mask.bin':'full-high-mask.bin','handoff.bin':'full-handoff-frame.bin','helper.bin':'full-received-helper.bin'}
 before=Path('.context/sdio-first/checkpoint-v2/full-attempt-1');hashes={}
 # Reference bytes already independently reconstructed from original Mach-O in073.
 for actual,original in bank_files.items():
  b=(out/actual).read_bytes();assert b==(before/original).read_bytes();hashes[actual]={'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest()}
 result={'passed':True,'scope':'Independent complete raw event parser and all retained image/mask/helper/handoff bytes; original instruction proof pins mode/branch meaning. No runtime implementation imported.','mode':mode,'events':events,'traceSha256':sha.hexdigest(),'statusReads':len(status),'zeroLengthPair':length,'markerReads':len(marker),'runtimeDataStarts':transfer_starts,'retainedBytes':hashes}
 (out/'audit.json').write_text(json.dumps(result,indent=2)+'\n');return result
if __name__=='__main__':print(json.dumps(audit(sys.argv[1]),indent=2))
