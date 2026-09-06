"""Artifact-only narrower audit after unavailable destination capture. No guest."""
from pathlib import Path
import json,struct,re,hashlib,importlib.util,sys
p=Path(sys.argv[1]);spec=importlib.util.spec_from_file_location('frozen',p/'save-audit.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);code=m.original_code()
j=json.loads((p/'save.json').read_text());assert j['owned']==1 and j['phase']==9 and j['resourceUnchanged']==1 and j['fieldsAvailable']==0
trace=(p/'trace.log').read_text();old=Path('.context/amc-first/stage-v/run-1');prior=(old/'trace.log').read_text().split('STOP unsupported engine read cycles9874729099')[0];assert trace.startswith(prior+'SAVE PREFIX actual V first save read before admission\n')
for f in old.glob('*.bin'):assert (p/'save-prefix'/f.name).read_bytes()==f.read_bytes(),f.name
words=[1,1,0,17770,0,83304,0];rows=j['reads'];assert len(rows)==9 and [r['off'] for r in rows]==[0x410+4*i for i in range(7)]+[0x188,0x404] and [r['value'] for r in rows]==words+[0,0]
for i,r in enumerate(rows):
 assert r['pc']==([0xc0277dfc]*7+[0xc0277144,0xc0277e4c])[i] and r['cpsr']&63==0x13 and r['opcode'].to_bytes(4,'little')==code(r['pc'])
 assert f"SAVE READ index{i} offset{r['off']:03x} value{r['value']:08x} pc{r['pc']:08x} cpsr{r['cpsr']:08x} opcode{r['opcode']:08x} cycles{r['cycles']} returned1" in trace
sram=(p/'sram.bin').read_bytes();assert sram==(old/'sram.bin').read_bytes();coverage=(p/'saved-copy-coverage.bin').read_bytes();assert coverage==b'\1'*49152,'source coverage missing';data=(p/'saved-copy-reads.bin').read_bytes();assert len(data)%32==0
seen=bytearray(49152);last=rows[-1]['cycles']
for address,width,value,pc,cpsr,op,cycles in struct.iter_unpack('<6IQ',data):
 assert width in (1,2,4) and cpsr&63==0x13 and op.to_bytes(4,'little')==code(pc),'source instruction differs'
 off=address-0x22000000
 if 0x28000<=off and off+width<=0x2c000:i=off-0x28000
 elif 0x14000<=off and off+width<=0x1c000:i=0x4000+off-0x14000
 else:raise AssertionError('source range differs')
 assert value.to_bytes(4,'little')[:width]==sram[off:off+width],'source bytes differ'
 assert last<=cycles<j['cycles'];last=cycles;seen[i:i+width]=b'\1'*width
assert seen==coverage
for name in ('owned-queue.bin','dma-copies.bin','dma-bytes.bin','stream-reads.bin','init-written.bin','pcm-written.bin','host-written.bin'):assert (p/name).read_bytes()==(old/name).read_bytes(),name
for name,n in [('saved-context.bin',64),('saved-dma-fields.bin',8),('saved-sram-copies.bin',49152),('saved-stop-code.bin',4)]:assert (p/name).read_bytes()==bytes(n),'unavailable field must remain explicit zeros'
assert 'STOP unmodeled active lifecycle cycles9874735590 ticks1756792320 pcc0411f3c lrc0411de4 address39a0000c width4 value00000003' in trace
report={'passed':True,'scope':'Narrower actual register/source-read evidence only. Complete save audit fails because destination observer exceeded4096-byte peek limit. No saved-destination, completed-power or restore proof.','actualSaveReads':9,'actualSRAMSourceReadBytes':49152,'actualSRAMReadRecords':len(data)//32,'sourceCoverageComplete':True,'resourceBytesUnchanged':True,'savedDestinationAvailable':False,'stopBeforeForwarding':{'pc':'c0411f3c','address':'39a0000c','value':3,'cycles':9874735590},'completeSavePass':False,'priorActualArtifactNegatives':40}
(p/'source-read-audit.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
