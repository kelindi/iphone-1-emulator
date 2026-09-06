"""Read-only exact original-instruction and literal proof; no guest/model run."""
from pathlib import Path
import hashlib,json,struct,capstone
p=Path(__file__).resolve().parent
kernel=Path('web/firmware/kernel_1A543a.macho');data=kernel.read_bytes()
assert hashlib.sha256(data).hexdigest()=='42e9f3c18d4a4d5c956acdcded0c3f8f247fe3e7fc96d14cfa3aedcd77c292c9'
assert struct.unpack_from('<I',data)[0]==0xfeedface
segments=[];pos=28
for i in range(struct.unpack_from('<I',data,16)[0]):
 command,size=struct.unpack_from('<II',data,pos)
 assert size>=8 and pos+size<=len(data)
 if command==1:segments.append(struct.unpack_from('<IIII',data,pos+24))
 pos+=size

def read(address,n):
 spans=[off+address-va for va,vsize,off,fsize in segments if va<=address and address+n<=va+fsize]
 assert len(spans)==1
 return data[spans[0]:spans[0]+n]
md=capstone.Cs(capstone.CS_ARCH_ARM,capstone.CS_MODE_ARM)
expected={
0xc038a848:('add','fp, fp, sl'),0xc038a854:('mov','r1, #0x20'),
0xc038a860:('add','r4, r4, #1'),0xc038a86c:('cmp','r4, #0x3e8'),
0xc038a870:('beq','#0xc038a8ec'),0xc038a878:('tst','r3, #8'),
0xc038a87c:('beq','#0xc038a850'),0xc038a880:('tst','r3, #1'),
0xc038a884:('beq','#0xc038a850'),0xc038a88c:('mov','r1, #0x10'),
0xc038a8a8:('mov','r1, #0x11'),0xc038a8c4:('orr','sl, sl, r2, lsl #8'),
0xc038a8e0:('tst','sl, #1'),0xc038a8e4:('beq','#0xc038a7c0'),
0xc038a7c4:('sub','r2, sl, #1'),0xc038a7c8:('cmp','fp, r3'),
0xc038a7cc:('cmpls','r2, #0x800'),0xc038a7d0:('bhs','#0xc038a968'),
0xc038a968:('mov','r0, #1'),0xc038aaa4:('subs','r4, r0, #0'),
0xc038aab4:('bl','#0xc03801b4'),0xc03801c4:('mov','r4, #0x64'),
0xc03801d0:('mov','r1, #0x34'),0xc03801d4:('bl','#0xc038013c'),
0xc03801d8:('ldrh','r3, [sp]'),0xc03801dc:('ldr','r2, [pc, #0x48]'),
0xc03801e0:('cmp','r3, r2'),0xc03801e4:('beq','#0xc0380208'),
0xc03801e8:('mov','r0, #1'),0xc03801f0:('blx','r3'),
0xc038020c:('mov','r1, #6'),0xc0380220:('mov','r0, #1'),
0xc0380168:('cmp','r0, #0'),0xc038016c:('beq','#0xc03801a8'),
0xc0380174:('add','r1, r5, #1'),0xc0380188:('cmp','r0, #0'),
0xc038018c:('beq','#0xc03801a8'),0xc038019c:('orr','r3, r3, r2, lsl #8'),
0xc03801a0:('strh','r3, [r6]')}
rows=[]
for address,expect in expected.items():
 raw=read(address,4);instructions=list(md.disasm(raw,address));assert len(instructions)==1
 i=instructions[0];assert (i.mnemonic,i.op_str)==expect,(hex(address),i.mnemonic,i.op_str,expect)
 rows.append({'address':hex(address),'bytes':raw.hex(),'mnemonic':i.mnemonic,'operands':i.op_str})
literals={'mainLastSourceOffset':(0xc038a7c0+8+0x1bc,122607),'firmwareMarker':(0xc03801dc+8+0x48,0xfedc)}
for name,(address,value) in literals.items():assert struct.unpack('<I',read(address,4))[0]==value
report={'passed':True,'scope':'Static original instruction/literal proof only; no model or guest execution','kernel':str(kernel),'kernelSha256':hashlib.sha256(data).hexdigest(),'checkedInstructions':len(rows),'instructions':rows,'literals':{k:{'address':hex(a),'value':v,'bytes':read(a,4).hex()} for k,(a,v) in literals.items()},'zeroRequestHazard':'A zero requested length underflows size-1 to UINT32_MAX, so the actual loader success exit alone does not prove whole source was sent. Device admission must independently require exact complete image/coverage and valid final command4.'}
(p/'source-contract.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS',len(rows),'original instructions,',len(literals),'literal values')
