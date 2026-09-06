"""Artifact-only counts. No timing, extra MMU access, or inferred routine binding."""
from pathlib import Path
import json,gzip,struct,collections,capstone
b=Path('.context/jit-executed-work');f=b/'replay/100000000';positions=json.loads((b/'positions.json').read_text());positions=positions['positions'] if isinstance(positions,dict)else positions;slots={p['slot']:p for p in positions}
counts=collections.defaultdict(lambda:collections.Counter());attempt=collections.Counter();pages=collections.defaultdict(lambda:collections.Counter());variants=collections.defaultdict(set)
for pc,op,flags,prov,guard,adm,status,used,n in struct.iter_unpack('<8IQ',gzip.decompress((f/'rows.bin.gz').read_bytes())):
 if not used:continue
 k=(pc,op,bool(flags&256),flags&31,status);counts[k]['interpreter']+=n;attempt[(prov,guard,adm,status,flags)]+=n;pages[(pc&~1023,bool(flags&256))]['interpreter']+=n;variants[(pc,bool(flags&256))].add((op,flags&31,status))
for slot,(n,)in enumerate(struct.iter_unpack('<Q',gzip.decompress((f/'generated.bin.gz').read_bytes()))):
 if not n:continue
 p=slots[slot];pc,op,thumb=p['pc'],p['opcode'],p['thumb'];k=(pc,op,thumb,1,0);counts[k]['generated']+=n;pages[(pc&~1023,thumb)]['generated']+=n;variants[(pc,thumb)].add((op,1,0))
arm=capstone.Cs(capstone.CS_ARCH_ARM,capstone.CS_MODE_ARM);thumb=capstone.Cs(capstone.CS_ARCH_ARM,capstone.CS_MODE_THUMB)
def row(k,v):
 pc,op,t,events,status=k;dis='not fetched'
 if events&1:
  dis='; '.join(i.mnemonic+' '+i.op_str for i in (thumb if t else arm).disasm(op.to_bytes(2 if t else 4,'little'),pc))or'undecoded'
 return {'pc':pc,'pcHex':hex(pc),'opcode':op,'opcodeHex':hex(op),'thumb':t,'events':events,'status':status,'generated':v['generated'],'interpreter':v['interpreter'],'total':sum(v.values()),'disassembly':dis,'observedVariants':len(variants[(pc,t)]),'physicalMapping':'unknown','routine':'unbound'}
allrows=[row(k,v)for k,v in counts.items()];allrows.sort(key=lambda r:-r['total']);pageRows=[{'pc':pc,'pcHex':hex(pc),'thumb':t,'generated':v['generated'],'interpreter':v['interpreter'],'total':sum(v.values())}for(pc,t),v in pages.items()];pageRows.sort(key=lambda r:-r['total'])
report={'scope':'Exact deterministic first100M Settings-launch trajectory only; not a captured warm Settings/Home window, time attribution, branch edge trace or bound routine name','retired':100000000,'generated':sum(r['generated']for r in allrows),'interpreter':sum(r['interpreter']for r in allrows),'top100Instructions':allrows[:100],'top40Pages':pageRows[:40],'attemptPartitions':[dict(provenance=k[0],guard=k[1],admission=k[2],status=k[3],flags=k[4],count=v)for k,v in sorted(attempt.items(),key=lambda x:-x[1])],'uniqueWorkKeys':len(allrows)}
(b/'hotspot-ranking.json').write_text(json.dumps(report,indent=2)+'\n')
with gzip.open(b/'all-ranked-work.json.gz','wt')as o:json.dump(allrows,o,separators=(',',':'))
print(json.dumps({'pages':pageRows[:12],'instructions':allrows[:25]},indent=2))
