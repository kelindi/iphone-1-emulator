"""One actual lock/pause prefix, independently audited; no guest execution."""
from pathlib import Path
import json,struct,re,hashlib,sys

def run(p):
 p=Path(p);old=Path('.context/amc-first/stage-u/run-1');trace=(p/'trace.log').read_text();prior=(old/'trace.log').read_text().split('STOP unsupported engine write cycles9874729042')[0]
 assert trace.startswith(prior+'LOCK PREFIX '),'fresh U trace before lock differs'
 prefix=p/'lock-prefix'
 for f in old.glob('*.bin'):
  assert (prefix/f.name).read_bytes()==f.read_bytes(),'fresh U actual binary differs: '+f.name
 assert (prefix/'stream.json').read_bytes()==(old/'stream.json').read_bytes(),'fresh U generation state differs'
 for f in old.glob('transport-*.json'):
  if f.name=='transport-audit.json':continue
  assert (prefix/f.name).read_bytes()==f.read_bytes(),'fresh U transport state differs'
 j=json.loads((p/'lock.json').read_text());assert j['prefixSaved'] and j['locked'] and j['dmaPaused'] and j['dmaActive'] and not j['transport'] and not j['inputPending'],'actual lock/pause ownership differs'
 assert (j['copied'],j['decoded'],j['queue'],j['current'],j['start'],j['words'],j['copiedWords'],j['events'],j['mask'],j['samples'],j['publications'])==(83304,17770,65534,0x14ae1568,0x14ae1000,4096,346,0x200,0x2000,108544,53),'actual frozen resources differ'
 # The source is the original stored ARM image, not generated expected opcodes alone.
 image=Path('web/firmware/kernel_1A543a.macho').read_bytes();assert hashlib.sha256(image).hexdigest()=='42e9f3c18d4a4d5c956acdcded0c3f8f247fe3e7fc96d14cfa3aedcd77c292c9';segments=[];off=28
 for _ in range(struct.unpack_from('<I',image,16)[0]):
  cmd,size=struct.unpack_from('<II',image,off)
  if cmd==1:segments.append(struct.unpack_from('<4I',image,off+24))
  off+=size
 def code(pc):return next(image[f+pc-v:f+pc-v+4] for v,z,f,n in segments if v<=pc and pc+4<=v+n)
 assert len(j['receipts'])==3,'actual lock store receipt count'
 for i,(r,pc,addr,value,locked,paused,transport) in enumerate(zip(j['receipts'],[0xc0277ce8,0xc0278218,0xc0278220],[0x38500400,0x38501000,0x38500a00],[1,0,0],[1,1,1],[0,1,1],[1,1,0])):
  assert r['retired'] and not r['pending'] and r['start']+1==r['callback']==r['end'],'actual lock store retirement differs'
  assert r['pc']==pc and r['address']==addr and r['value']==value and r['afterPC']==pc+4 and r['afterCPSR']==r['cpsr'] and r['cpsr']&63==0x13,'actual lock successor/mode differs'
  assert r['opcode'].to_bytes(4,'little')==code(pc),'actual original lock opcode differs'
  receipt=f"LOCK RETIRED index{i} start{r['start']} callback{r['callback']} end{r['end']} pc{pc:08x} cpsr{r['cpsr']:08x} opcode{r['opcode']:08x} address{addr:08x} value{value:08x} afterPC{r['afterPC']:08x} afterCPSR{r['afterCPSR']:08x} locked{locked} paused{paused} active1 transport{transport} copied83304 decoded17770 queue65534 emuRunReturned1"
  assert receipt in trace,'actual lock receipt/log differs'
  assert f'AMC W {addr:08x} width4 value{value:08x} pc{pc:08x} ' in trace,'actual lock MMIO request absent'
 reads=j['reads'];assert [r['address'] for r in reads]==[0x38500400,0x38501008,0x38501004,0x38501010],'actual readback sequence differs'
 assert [r['value'] for r in reads]==[1,4096,0x14ae1000,0x14ae1568],'actual pause readback differs'
 for r in reads:assert f"LOCK READ address{r['address']:08x} value{r['value']:08x} pc{r['pc']:08x} cpsr{r['cpsr']:08x} cycles{r['cycles']}" in trace and r['cpsr']&63==0x13,'actual readback receipt differs'
 assert j['driverFieldsAvailable'] and j['driver']==0xccaa2c00 and j['savePointer']==j['driver']+0x184,'actual saved driver pointer unavailable'
 fields=(p/'locked-driver-fields.bin').read_bytes();assert struct.unpack('<II',fields)==(0x14ae1568,3750),'actual original saved current/remaining differs'
 assert j['pc']==0xc0277dfc and j['cpsr']&63==0x13 and (p/'locked-read-code.bin').read_bytes()==code(j['pc']),'actual stopped save read instruction differs'
 assert f"STOP unsupported engine read cycles{j['cycles']} " in trace and 'address38500410 width4 value00000000' in trace and trace.count('STOP context inside-unretired-instruction')==1,'earliest unsupported BSU read differs'
 assert j['receipts'][-1]['end']<j['cycles']<=9874729042+5000,'lock continuation bound differs'
 # No successful BSU read and no later resource progress can be hidden by a final summary.
 assert not re.search(r'^AMC R 38500410 ',trace,re.M),'unsupported BSU read was fabricated'
 for n in ['sram.bin','owned-queue.bin','dma-copies.bin','dma-bytes.bin','stream-reads.bin','init-written.bin','pcm-written.bin','host-written.bin']:
  assert (p/n).read_bytes()==(old/n).read_bytes(),'resource changed after lock/pause: '+n
 assert json.loads((p/'stream-audit.json').read_text())['publishedGenerations']==53
 report={'passed':True,'scope':'Actual BSU lock and partial DMA pause; stops before first unmodeled BSU save-register value. No saved context, power gating, I2S or sound.','actualStoreReceipts':3,'actualReadReceipts':4,'remainingInputWords':3750,'ownedQueueBytes':65534,'stopCycles':j['cycles'],'additionalCyclesAfterURequest':j['cycles']-9874729042,'pubs':53,'released':51}
 (p/'lock-audit.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':run(sys.argv[1])
