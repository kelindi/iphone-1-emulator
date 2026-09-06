"""Actual same-run retained allocations and original retired controller stores."""
from pathlib import Path
import json,re,struct,sys,importlib.util
spec=importlib.util.spec_from_file_location('save_audit',Path(__file__).with_name('save-audit.py'));sa=importlib.util.module_from_spec(spec);spec.loader.exec_module(sa)
def run(p):
 p=Path(p);j=json.loads((p/'suspend.json').read_text());trace=(p/'trace.log').read_text();code=sa.original_code()
 assert j['owned'] and j['resourcesValid'] and j['controllerAvailable'] and 2<=j['phase']<=4,'suspension resource transaction incomplete'
 assert all(j[n]==0 for n in ('activeInitialized','activeConfigured','activeDecoder','activeQueue','activeIRQ','activeEvents','activeMask','activeService','activeDMA','activeIdleEnabled','activeResetComplete')),'active hardware was retained'
 assert (j['generation'],j['copied'],j['decoded'],j['queue'])==(1,83304,17770,65534),'retained conservation differs'
 assert (p/'suspended-queue.bin').read_bytes()==(p/'power-prefix/owned-queue.bin').read_bytes()==(p/'owned-queue.bin').read_bytes(),'actual suspended queue differs'
 assert (p/'suspended-sram.bin').read_bytes()==(p/'power-prefix/sram.bin').read_bytes()==(p/'sram.bin').read_bytes(),'actual suspended SRAM differs'
 for name in ('init-written.bin','pcm-written.bin','host-written.bin','dma-bytes.bin','dma-copies.bin','stream-reads.bin'):
  assert (p/name).read_bytes()==(p/'power-prefix'/name).read_bytes(),'retained resource mutation: '+name
 assert (p/'stream.json').read_bytes()==(p/'power-prefix/stream.json').read_bytes(),'suspension changed real ownership'
 controls=struct.unpack('<20I',(p/'suspended-control.bin').read_bytes())
 assert controls[0:4]==(1,1,0,1) and controls[10:16]==(0x14ae1000,4096,346,0,1,1),'retained control ownership differs'
 # Successful publication clears service in the pinned engine; only actual+4
 # may rearm it. Validate the actual last-publication trace, not a toy fixture.
 last_publication=trace.rsplit('STREAM PUBLICATION generation53 ',1)
 assert len(last_publication)==2 and not re.search(r'^AMC W 38500004 ',last_publication[1],re.M),'retained service lacks actual publication provenance'
 stores=re.findall(r'^SUSPEND WRITE index(\d+) address([0-9a-f]+) value([0-9a-f]+) clockBefore([0-9a-f]+) clockAfter([0-9a-f]+) powerBefore([0-9a-f]+) powerAfter([0-9a-f]+) pc([0-9a-f]+) cpsr([0-9a-f]+) opcode([0-9a-f]+) cycles(\d+) owned(\d+) active(\d+)$',trace,re.M)
 receipts=re.findall(r'^SUSPEND RETIRED index(\d+) start(\d+) callback(\d+) end(\d+) pc([0-9a-f]+) cpsr([0-9a-f]+) opcode([0-9a-f]+) address([0-9a-f]+) value([0-9a-f]+) afterPC([0-9a-f]+) afterCPSR([0-9a-f]+) emuRunReturned1$',trace,re.M)
 assert len(stores)==len(receipts)==j['phase'],'controller writes not all retired'
 c=0x4fffe267;power=0x12ec;last=9874735589
 for i,(row,receipt) in enumerate(zip(stores,receipts)):
  idx,a,v,cb,ca,pb,pa,pc,cpsr,op,cycles,owned,active=row;a,v,cb,ca,pb,pa,pc,cpsr,op=[int(x,16) for x in (a,v,cb,ca,pb,pa,pc,cpsr,op)];cycles=int(cycles)
  expected=[(0x39a0000c,3,c,power),(0x39a00010,0x10,c,power|0x10),(0x3c500048,c|8,c|8,power),(0x3c500048,c|0x10,c|0x10,power)][i]
  assert int(idx)==i and (a,v,ca,pa)==expected and (cb,pb)==(c,power),'controller transition/order differs'
  assert op.to_bytes(4,'little')==code(pc) and cpsr&63==0x13 and op>>28==14 and op&0x0c100000==0x04000000,'actual controller store instruction differs'
  assert int(owned)==(i>0) and int(active)==(i==0),'resource transfer chronology differs'
  expected_receipt=[str(i),str(cycles-1),str(cycles),str(cycles),f'{pc:08x}',f'{cpsr:08x}',f'{op:08x}',f'{a:08x}',f'{v:08x}',f'{pc+4:08x}',f'{cpsr:08x}']
  assert list(receipt)==expected_receipt and cycles>last,'suspension retirement differs'
  c,power,last=ca,pa,cycles
 assert (j['clock'],j['power'])==(c,power),'actual final controller differs'
 state=re.findall(r'^SUSPEND DRIVER stateBefore(\d+) stateAfter(\d+) address([0-9a-f]+) pc([0-9a-f]+) cpsr([0-9a-f]+) opcode([0-9a-f]+) start(\d+) end(\d+) afterPC([0-9a-f]+) afterCPSR([0-9a-f]+) emuRunReturned1$',trace,re.M)
 assert bool(state)==bool(j['driverStoreRetired']),'driver state receipt absent'
 if state:
  assert len(state)==1 and j['phase']==4,'driver suspended state differs'
  assert (j['driverStateAvailable'] and j['driverState']==5) or (not j['driverStateAvailable'] and j['driverState']==0 and j['cpsr']&31==16),'unavailable final kernel state fabricated'
  before,after,address,pc,cpsr,opcode,start,end,afterpc,aftercpsr=state[0]
  assert int(before)==3 and last<int(start)<int(end)<=j['cycles'] and int(after)==5 and int(address,16)==0xccaa2c00+0xe0 and int(pc,16)==0xc0272640 and int(cpsr,16)&63==0x13 and int(opcode,16).to_bytes(4,'little')==code(0xc0272640) and int(end)==int(start)+1 and int(afterpc,16)==0xc0272644 and cpsr==aftercpsr,'actual driver state store differs'
 tail=trace.split('SUSPEND PREFIX ',1)[1]
 assert not re.search(r'^(AMC [RW]|ENGINE SRAM W|RESET W|DMA COPY|STREAM PUBLICATION|STREAM RELEASE)',tail,re.M),'suspension admitted active work'
 stops=re.findall(r'^STOP (.+) cycles(\d+) ticks(\d+) pc([0-9a-f]+) lr([0-9a-f]+) address([0-9a-f]+) width(\d+) value([0-9a-f]+)$',tail,re.M)
 assert len(stops)==1 and int(stops[0][1])==j['cycles'] and last<=j['cycles']<=9873752634+5000000,'bounded suspension stop differs'
 report={'passed':True,'scope':'Actual synchronous controller stores and one live retained decoder generation; no reset/restore or portable snapshot; no audible output.','retiredControllerStores':len(stores),'driverState5Retired':bool(state),'lastVerifiedKernelStore':5 if state else None,'finalKernelFieldAvailable':bool(j['driverStateAvailable']),'finalKernelField':j['driverState'] if j['driverStateAvailable'] else None,'retainedQueueBytes':j['queue'],'retainedSRAMBytes':180224,'activeEngineEmpty':True,'stop':stops[0],'clock':hex(c),'power':hex(power)}
 (p/'suspend-audit-corrected.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':run(sys.argv[1])
