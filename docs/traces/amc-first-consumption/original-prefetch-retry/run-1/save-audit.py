"""Independent original-instruction/resource audit of one bounded save-only run."""
from pathlib import Path
import json,struct,re,hashlib,sys
KERNEL=Path('web/firmware/kernel_1A543a.macho')
def original_code():
 image=KERNEL.read_bytes();assert hashlib.sha256(image).hexdigest()=='42e9f3c18d4a4d5c956acdcded0c3f8f247fe3e7fc96d14cfa3aedcd77c292c9';segs=[];off=28
 for _ in range(struct.unpack_from('<I',image,16)[0]):
  cmd,n=struct.unpack_from('<II',image,off)
  if cmd==1:segs.append(struct.unpack_from('<4I',image,off+24))
  off+=n
 def code(pc):return next(image[f+pc-v:f+pc-v+4] for v,z,f,n in segs if v<=pc and pc+4<=v+n)
 return code

def receipts(j,context,copies,sram,trace,code):
 assert j['prefixSaved'] and j['owned'] and j['phase']==9 and j['resourceUnchanged'] and j['fieldsAvailable'],'save incomplete/resource changed'
 assert j.get('availability')=={'context':1,'dmaFields':1,'firstSRAM':1,'secondSRAM':1,'stopCode':1},'individual save field unavailable'
 assert (j['generation'],j['copied'],j['decoded'],j['queue'])==(1,83304,17770,65534),'save identity/counters differ'
 words=[1,j['generation']&0xffffffff,j['generation']>>32,j['decoded']&0xffffffff,j['decoded']>>32,j['copied']&0xffffffff,j['copied']>>32]
 assert len(context)==64 and list(struct.unpack_from('<7I',context))==words and struct.unpack_from('<I',context,60)[0]==0,'saved context differs'
 assert copies==sram[0x28000:0x2c000]+sram[0x14000:0x1c000],'actual original SRAM copies differ'
 rows=j['reads'];assert len(rows)==9,'actual save read coverage missing'
 assert [r['off'] for r in rows]==[0x410+4*i for i in range(7)]+[0x188,0x404],'actual save read order differs'
 assert [r['value'] for r in rows]==words+[0,0],'actual save values differ'
 assert [r['pc'] for r in rows]==[0xc0277dfc]*7+[0xc0277144,0xc0277e4c],'actual save read PCs differ'
 assert rows[0]['cycles']==9874729099 and all(a['cycles']<b['cycles'] for a,b in zip(rows,rows[1:])) and rows[-1]['cycles']<j['cycles'],'actual save chronology differs'
 for i,r in enumerate(rows):
  assert r['cpsr']&63==0x13 and r['opcode'].to_bytes(4,'little')==code(r['pc']),'actual save instruction/mode differs'
  assert f"SAVE READ index{i} offset{r['off']:03x} value{r['value']:08x} pc{r['pc']:08x} cpsr{r['cpsr']:08x} opcode{r['opcode']:08x} cycles{r['cycles']} returned1" in trace,'actual save read receipt absent'
 return words

def run(p):
 p=Path(p);old=Path('.context/amc-first/stage-v/run-1');trace=(p/'trace.log').read_text();prior=(old/'trace.log').read_text().split('STOP unsupported engine read cycles9874729099')[0]
 assert trace.startswith(prior+'SAVE PREFIX actual V first save read before admission\n'),'fresh V actual trace differs'
 prefix=p/'save-prefix'
 for f in old.glob('*.bin'):assert (prefix/f.name).read_bytes()==f.read_bytes(),'fresh V binary differs: '+f.name
 for n in ('stream.json','lock.json'):
  assert (prefix/n).read_bytes()==(old/n).read_bytes(),'fresh V state differs: '+n
 for f in old.glob('transport-*.json'):
  if f.name!='transport-audit.json':assert (prefix/f.name).read_bytes()==f.read_bytes(),'fresh V transport differs'
 j=json.loads((p/'save.json').read_text());code=original_code();sram=(p/'sram.bin').read_bytes()
 words=receipts(j,(p/'saved-context.bin').read_bytes(),(p/'saved-sram-copies.bin').read_bytes(),sram,trace,code)
 coverage=(p/'saved-copy-coverage.bin').read_bytes();assert coverage==bytes([1])*49152,'actual SRAM copy read coverage missing'
 ledger=(p/'saved-copy-reads.bin').read_bytes();assert ledger and len(ledger)%32==0 and len(ledger)<=49152*32,'actual SRAM copy read ledger bounds'
 observed=bytearray(49152);last=0
 for address,width,value,pc,cpsr,opcode,cycles in struct.iter_unpack('<6IQ',ledger):
  assert width in (1,2,4) and cpsr&63==0x13 and opcode.to_bytes(4,'little')==code(pc),'actual SRAM copy reader instruction differs'
  off=address-0x22000000
  if 0x28000<=off and off+width<=0x2c000:index=off-0x28000
  elif 0x14000<=off and off+width<=0x1c000:index=0x4000+off-0x14000
  else:raise AssertionError('actual SRAM copy source outside saved spans')
  assert value.to_bytes(4,'little')[:width]==sram[off:off+width],'actual SRAM copy returned bytes differ'
  assert j['reads'][-1]['cycles']<cycles<=j['cycles'] and last<=cycles,'actual SRAM copy read chronology differs'
  observed[index:index+width]=bytes([1])*width;last=cycles
 assert observed==coverage,'actual SRAM copy read ledger coverage differs'
 assert struct.unpack('<II',(p/'saved-dma-fields.bin').read_bytes())==(0x14ae1568,3750),'saved actual DMA continuation differs'
 for name in ('sram.bin','owned-queue.bin','dma-copies.bin','dma-bytes.bin','stream-reads.bin','init-written.bin','pcm-written.bin','host-written.bin'):
  assert (p/name).read_bytes()==(old/name).read_bytes(),'save mutated owned resources: '+name
 assert json.loads((p/'stream.json').read_text())==json.loads((old/'stream.json').read_text()),'save mutated output ownership'
 suspended='SUSPEND PREFIX actual X ONCTRL request before forwarding; domains0/1 already ungated\n' in trace
 if suspended:
  previous=Path('.context/amc-first/stage-x/run-1');baseline=(previous/'trace.log').read_text();prefix_text=baseline.split('STOP unmodeled active lifecycle cycles9874735590')[0]
  assert trace.startswith(prefix_text+'SUSPEND PREFIX actual X ONCTRL request before forwarding; domains0/1 already ungated\n'),'fresh X actual trace differs'
  pc=j['pc'];address=0x39a0000c;value='00000003';cycles=j['cycles']
  assert (cycles,pc,j['cpsr']&63)==(9874735590,0xc0411f3c,0x13),'actual X preforward context differs'
  assert (p/'saved-stop-code.bin').read_bytes()==code(pc),'save stopped original instruction differs'
  for f in previous.glob('*.bin'):assert (p/'power-prefix'/f.name).read_bytes()==f.read_bytes(),'fresh X binary differs: '+f.name
  assert (p/'power-prefix/save.json').read_bytes()==(previous/'save.json').read_bytes(),'fresh X save metadata differs'
 else:
  stops=re.findall(r'^STOP (.+) cycles(\d+) ticks(\d+) pc([0-9a-f]+) lr([0-9a-f]+) address([0-9a-f]+) width(\d+) value([0-9a-f]+)$',trace,re.M)
  assert len(stops)==1 and stops[0][0]=='unmodeled active lifecycle','save earliest lifecycle stop differs'
  why,cycles,ticks,pc,lr,address,width,value=stops[0];pc=int(pc,16);address=int(address,16)
  assert int(cycles)==j['cycles'] and pc==j['pc'] and j['cpsr']&63==0x13 and int(width)==4,'save stop context differs'
  assert address in (0x39a0000c,0x39a00010,0x3c500048),'unexpected lifecycle boundary'
  assert (p/'saved-stop-code.bin').read_bytes()==code(pc),'save stopped original instruction differs'
  assert trace.count('STOP context inside-unretired-instruction')==1,'save lifecycle stop retired'
  assert 'RESET W ' not in trace.split('SAVE PREFIX ')[1],'save crossed reset admission'
  assert 9874729099<int(cycles)<=9873752634+5000000,'save continuation bound differs'
  previous=Path('.context/amc-first/stage-w/run-1')
  assert trace==(previous/'trace.log').read_text(),'observer successor changed actual W trace'
  changed={'saved-context.bin','saved-dma-fields.bin','saved-sram-copies.bin','saved-stop-code.bin'}
  for f in previous.glob('*.bin'):
   if f.name not in changed:assert (p/f.name).read_bytes()==f.read_bytes(),'observer successor changed W artifact: '+f.name
  before=json.loads((previous/'save.json').read_text());current=dict(j);current.pop('availability');current['fieldsAvailable']=0;assert current==before,'observer successor changed W engine/read state'
 report={'passed':True,'scope':'Original seven-word save and actual48KiB SRAM copies; retained same-process generation only, stops before unsupported lifecycle write. No reset/restore/power transition or sound.','actualReads':9,'savedRegisterWords':words,'smallFIFOWords':0,'actualSRAMCopyBytes':49152,'originalSavedDMAWords':3750,'ownedQueueBytes':65534,'publications':53,'ownershipReturns':51,'stop':{'pc':hex(pc),'address':hex(address),'value':value,'cycles':int(cycles),'instructionRetired':False},'freshVBinaryFiles':len(list(old.glob('*.bin'))),'actualWTraceAndUnchangedFilesExact':True,'observerOnlySuccessor':True}
 (p/'save-audit.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':run(sys.argv[1])
