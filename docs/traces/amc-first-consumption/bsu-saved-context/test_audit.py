"""Focused synthetic ledger contracts; not a guest result."""
import importlib.util,struct
from pathlib import Path
spec=importlib.util.spec_from_file_location('consumption_audit',Path(__file__).with_name('audit.py'));module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
pcm=bytes((i*13+7)&255 for i in range(4096));coverage=bytes([1])*4096
ledger=b''.join(struct.pack('<6IQ',0x2200f220+i,4,int.from_bytes(pcm[i:i+4],'little'),0xc00606e4,0x13,0,100+i//4) for i in range(0,4096,4))
assert module.audit_reads(ledger,coverage,pcm,pcm,100,1200)==1024
checks=1
for label,cov,data,rows,expected in [
 ('missing',coverage[:123]+b'\0'+coverage[124:],pcm,ledger,'missing actual read coverage'),
 ('corrupt',coverage,pcm[:123]+bytes([pcm[123]^1])+pcm[124:],ledger,'observed read bytes differ'),
 ('ledger',coverage,pcm,ledger[:8]+bytes([ledger[8]^1])+ledger[9:],'returned ledger byte differs')]:
 try:module.audit_reads(rows,cov,data,pcm,100,1200)
 except AssertionError as error:assert expected in str(error);checks+=1
 else:raise AssertionError(label+' mutation accepted')
print('external ledger predicate:',checks,'synthetic positive/negative checks pass; actual artifact negatives await guest run')

release_trace="""ENGINE SRAM W 2200f208 width2 value00000001 pcc02726a8
RELEASE STEP startCycles1199 startPCc02726a8 startCPSR60000013 instructione1c030b0 callbackCycles1200 callbackPCc02726a8 callbackCPSR60000013
OWNER WRITE accepted address2200f208 width2 value00000001 before0 after1 cycles1200 pcc02726a8 cpsr60000013
RELEASE storePending cycles1200 pcc02726a8 cpsr60000013 covered4096 path1 index0 driverccaa2c00
RELEASE RETIRED startCycles1199 callbackCycles1200 endCycles1200 startPCc02726a8 afterPCc02726ac startCPSR60000013 afterCPSR60000013 instructione1c030b0 emuRunReturned1 inGuest0
PASS first PCM read and ownership returned
"""
assert module.audit_release(release_trace,1200,1200,0xc02726a8,0x60000013);checks+=1
for wrong in (release_trace.replace('ENGINE SRAM W 2200f208 width2 value00000001 pcc02726a8','ENGINE SRAM W 2200f208 width2 value00000001 pcc0272690'),release_trace.replace('ENGINE SRAM W 2200f208 width2 value00000001','ENGINE SRAM W 2200f208 width2 value00000000')):
 try:module.audit_release(wrong,1200,1200,0xc02726a8,0x60000013)
 except AssertionError as error:assert 'actual owner store differs' in str(error);checks+=1
 else:raise AssertionError('owner store mutation accepted')
print('complete external predicates:',checks,'checks pass, including actual owner-record PC/value mutants')

thumb=b''.join(struct.pack('<6IQ',0x2200f220+i,4,int.from_bytes(pcm[i:i+4],'little'),0xc00606e6,0xf0000033,0,100+i//4) for i in range(0,4096,4))
assert module.audit_reads(thumb,coverage,pcm,pcm,100,1200)==1024;checks+=1
bad=bytearray(thumb);struct.pack_into('<I',bad,16,0xf0000013)
try:module.audit_reads(bad,coverage,pcm,pcm,100,1200)
except AssertionError as error:assert 'reader ISA alignment differs' in str(error);checks+=1
else:raise AssertionError('lost Thumb bit accepted')
print('final external predicates:',checks,'checks pass; full CPSR/Thumb and owner-record negatives included')

for old,new in [('startCycles1199','startCycles1200'),('afterPCc02726ac','afterPCc02726b0'),('afterCPSR60000013','afterCPSR60000033'),('emuRunReturned1','emuRunReturned0'),('instructione1c030b0','instructione1c030b1'),('inGuest0','inGuest1')]:
 try:module.audit_release(release_trace.replace(old,new),1200,1200,0xc02726a8,0x60000013)
 except AssertionError:checks+=1
 else:raise AssertionError('retirement mutant accepted: '+old)
print('retirement evidence predicates:',checks,'PASS')
