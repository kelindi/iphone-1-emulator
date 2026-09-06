from pathlib import Path
import re,json,hashlib
root=Path('.context/interpreter-dispatch');s=(root/'original-arm_interp.c').read_text();start=s.index('    if ((insn & 0x0c000000u) == 0x00000000u &&        /* the DP encoding space */');end=s.index('\n    /* A translation fault latched',start)
parts=re.split(r'(?m)^    } else ',s[start:end]);assert len(parts)==25
parts[0]=parts[0][4:]
groups={0:[0,2,3,4,*range(8,20),23],1:[5,6],2:[1,7],3:[20,21,22]}
assert sorted(sum(groups.values(),[]))==list(range(24))
new='''    /* Dispatch only on an encoding-class partition. Every retained predicate
     * and handler below is unchanged and keeps its original within-class order.
     * Predicate/IRQ/fetch/cycle handling above and the common abort/undefined/PC
     * epilogue below stay shared. Thumb and cond=0xf do not reach this switch. */
    switch ((insn >> 26) & 3u) {\n'''
for group,indices in groups.items():
 new+=f'    case {group}:\n'
 body='    '+parts[indices[0]]+''.join('    } else '+parts[i] for i in indices[1:])+'    } else {\n        st = ARM_UNDEFINED;\n    }\n'
 new+=''.join('    '+line if line.strip() else line for line in body.splitlines(keepends=True))+'        break;\n'
new+='    }\n'
(root/'arm_interp.c').write_text(s[:start]+new+s[end:])
predicates=[];used=0
for i,p in enumerate(parts[:-1]):
 head=re.sub(r'/\*.*?\*/','',p[:p.index('{')],flags=re.S).strip();assert head.startswith('if (') and head.endswith(')');predicate=head[3:].strip();predicates.append(predicate)
 for mask in re.findall(r'insn\s*&\s*(0x[0-9a-f]+)u',predicate):used|=int(mask,16)
 bits=[bit for bit in range(28) if used&(1<<bit)]
assert len(bits)==16,bits
c=['#include <stdio.h>\n#include <stdint.h>\n#include <stdlib.h>\n','static unsigned old(uint32_t insn) {\n']
for i,p in enumerate(predicates):c.append(f' if {p} return {i};\n')
c.append(' return 24; }\nstatic unsigned candidate(uint32_t insn) { switch((insn>>26)&3u) {\n')
for group,indices in groups.items():
 c.append(f'case {group}:\n')
 for i in indices:c.append(f' if {predicates[i]} return {i};\n')
 c.append('return 24;\n')
c.append('} return 24; }\n')
c.append('static unsigned checks; static void check(uint32_t i) { checks++; if(old(i)!=candidate(i)){printf("FAIL %08x old=%u candidate=%u\\n",i,old(i),candidate(i));exit(1);} }\n')
c.append('int main(void){const unsigned bits[16]={'+','.join(map(str,bits))+'}; for(unsigned p=0;p<65536;p++){uint32_t i=0;for(unsigned b=0;b<16;b++)i|=((p>>b)&1u)<<bits[b];for(unsigned cond=0;cond<16;cond++){check(i|(cond<<28));check(i|(cond<<28)|'+hex((~used)&0xfffffff)+'u);}} check(0xe320f003u);for(unsigned b=0;b<32;b++)check(0xe320f003u^(1u<<b)); printf("PASS %u decoder selections\\n",checks);return 0;}\n')
(root/'classify.c').write_text(''.join(c))
(root/'structure.json').write_text(json.dumps({'sourceSha256':hashlib.sha256(s.encode()).hexdigest(),'candidateSha256':hashlib.sha256((s[:start]+new+s[end:]).encode()).hexdigest(),'groups':groups,'originalPredicateAndBodyFragments':24,'fragmentsReusedVerbatimExceptIndentation':True,'withinClassOrderPreserved':True,'prefixAndEpilogueByteIdentical':True,'guardUsedMaskExcludingExactWFI':hex(used),'guardUsedBitsExcludingExactWFI':bits,'guards':predicates,'proof':'All non-WFI selection predicates use only16 enumerated bits. Class bits are included. Enumerate every projection/all16condition values with ignored bits all0/all1. Exact WFI plus32one-bit mutations cover the only full-word predicate; WFI is class00. No instruction handler was modified.'},indent=2)+'\n')
