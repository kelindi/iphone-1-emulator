from pathlib import Path
import re,json,hashlib,shutil
p=Path('.context/core-hotspot'); src=Path('core/src/arm/arm_interp.c').read_text()
start=src.index('    if ((insn & 0x0c000000u) == 0x00000000u &&        /* the DP encoding space */')
end=src.index('\n\n    /* A translation fault latched during',start)
chain=src[start:end]
# Top-level else-if delimiters have exactly four spaces in maintained source.
parts=re.split(r'\n    } else if ',chain)
parts[0]=parts[0].removeprefix('    if ') if hasattr(str,'removeprefix') else parts[0][7:]
last=parts[-1]; parts[-1],fallback=last.rsplit('\n    } else {',1)
entries=[]
for i,part in enumerate(parts):
 pos=part.index('{'); cond=part[:pos].strip(); body=part[pos+1:]
 entries.append((cond,body))
# Exact top-three-bit admission of each original arm. Relative priority stays intact.
groups=[[0,1],[5],[0],[0],[0],[3],[2,3],[4],[0],[0],[0],[0],[0],[0],[0],[0],[0],[0],[0],[1],[7],[6,7],[7],[0,1]]
assert len(entries)==len(groups),(len(entries),len(groups))
result='    /* Partition the existing decoder by bits 27:25. Each bucket keeps\n     * the original predicates, precedence and instruction bodies unchanged. */\n    switch ((insn >> 25) & 7u) {\n'
for group in range(8):
 result+=f'    case {group}u:\n'
 selected=[e for e,g in zip(entries,groups) if group in g]
 for n,(cond,body) in enumerate(selected):result+=('        if ' if n==0 else '\n        } else if ')+cond+' {'+body
 result+='\n        } else {\n            st = ARM_UNDEFINED;\n        }\n        break;\n'
result+='    }'
(p/'arm_interp.c').write_text(src[:start]+result+src[end:])
(p/'original-arm_interp.c').write_text(src)
(p/'decoder-arms.json').write_text(json.dumps([{'arm':i,'condition':c,'groups':g} for i,((c,b),g) in enumerate(zip(entries,groups))],indent=2))
# Exhaustive routing equivalence checks, all 28 payload bits under AL and one
# other condition. Only original condition-specific predicate is exact WFI.
def classify(selected):
 return ''.join(('if ' if n==0 else 'else if ')+entries[i][0]+f' return {i};\n' for n,i in enumerate(selected))+'return -1;\n'
c='#include <stdint.h>\n#include <stdio.h>\nstatic int original(uint32_t insn){'+classify(list(range(len(entries))))+'}\nstatic int candidate(uint32_t insn){switch((insn>>25)&7){'
for g in range(8):c+=f'case {g}: '+classify([i for i,x in enumerate(groups) if g in x])
c+='}return -2;}\nint main(void){for(unsigned cond=0;cond<2;cond++)for(uint32_t payload=0;payload<0x10000000u;payload++){uint32_t insn=payload|(cond?0xe0000000u:0);if(original(insn)!=candidate(insn)){printf("mismatch %08x\\n",insn);return 1;}}puts("PASS 536870912 decoder routes");}\n'
(p/'test-routing.c').write_text(c)
print(len(entries),'arms partitioned')
