"""Independent parser of final C artifacts; does not import prepare.py/groups."""
from pathlib import Path
import re,json,hashlib,subprocess
p=Path('.context/interpreter-dispatch')
old=(p/'original-arm_interp.c').read_text();new=(p/'arm_interp.c').read_text()
pattern=re.compile(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[A-Za-z_]\w*|0x[\da-fA-F]+[uUlL]*|\d+[uUlL]*|>>|<<|==|!=|&&|\|\||\+\+|--|->|[^\s]',re.S)
def tokens(s):return [m.group() for m in pattern.finditer(s) if not m.group().startswith(('/*','//'))]
def balanced(t,i,op,cl):
 assert t[i]==op,(t[i],op);depth=0
 for j in range(i,len(t)):
  if t[j]==op:depth+=1
  if t[j]==cl:
   depth-=1
   if depth==0:return t[i+1:j],j+1
 raise AssertionError('Unclosed block')
def chain(t,i):
 branches=[]
 while True:
  assert t[i]=='if';guard,i=balanced(t,i+1,'(',')');body,i=balanced(t,i,'{','}');branches.append((guard,body))
  if t[i]!='else':return branches,None,i
  i+=1
  if t[i]=='if':continue
  default,i=balanced(t,i,'{','}');return branches,default,i
marker='if ((insn & 0x0c000000u) == 0x00000000u &&        /* the DP encoding space */'
a=old.index(marker);end=old.index('    /* A translation fault latched',a)
old_t=tokens(old[a:end]);original,default,last=chain(old_t,0);assert last==len(old_t) and len(original)==24
b=new.index('    /* Dispatch only on an encoding-class partition.');new_end=new.index('    /* A translation fault latched',b)
assert old[:a-4]==new[:b],'Unchanged prefix';assert old[end:]==new[new_end:],'Unchanged epilogue'
t=tokens(new[b:new_end]);assert t[:13]==['switch','(','(','insn','>>','26',')','&','3u',')','{','case','0'],t[:13]
i=11;groups={};evidence=[]
while t[i]=='case':
 group=int(t[i+1]);assert t[i+2]==':';branches,d,i=chain(t,i+3);assert d==default;assert t[i:i+2]==['break',';'];i+=2
 indices=[]
 for guard,body in branches:
  matches=[n for n,pair in enumerate(original) if pair==(guard,body)];assert len(matches)==1;index=matches[0];indices.append(index)
  evidence.append({'originalIndex':index,'class':group,'guard':' '.join(guard),'bodyTokenSha256':hashlib.sha256(' '.join(body).encode()).hexdigest()})
 assert indices==sorted(indices),'Within-class precedence changed';groups[group]=indices
assert sorted(groups)==list(range(4)) and t[i:]==['}'];assert sorted(sum(groups.values(),[]))==list(range(24))
mask=0;wfi=[]
for index,(guard,body) in enumerate(original):
 expr=' '.join(guard)
 if expr=='insn == 0xe320f003u':wfi.append(index);continue
 assert not set(re.findall(r'\b[A-Za-z_]\w*\b',expr)) - {'insn'},expr
 matches=list(re.finditer(r'insn\s*&\s*(0x[0-9a-fA-F]+)u',expr))
 for m in matches:mask|=int(m[1],16)
 assert 'insn' not in re.sub(r'insn\s*&\s*0x[0-9a-fA-F]+u','0',expr),expr
assert wfi==[19];bits=[b for b in range(28) if mask&(1<<b)];assert len(bits)==16 and mask&(3<<26)==3<<26
code=['#include <stdint.h>\n#include <stdio.h>\n#include <stdlib.h>\n__attribute__((noinline,noipa)) unsigned old(uint32_t insn){\n']
for n,(g,_) in enumerate(original):code.append('if ('+' '.join(g)+f') return {n};\n')
code.append('return 24;}\n__attribute__((noinline,noipa)) unsigned candidate(uint32_t insn){switch((insn>>26)&3u){\n')
for group,indices in groups.items():
 code.append(f'case {group}:\n')
 for n in indices:code.append('if ('+' '.join(original[n][0])+f') return {n};\n')
 code.append('return 24;\n')
code.append('}return 24;}\nstatic unsigned checks;void check(uint32_t i){checks++;if(old(i)!=candidate(i)){printf("FAIL %08x\\n",i);exit(1);}}\n')
code.append('int main(void){unsigned bits[16]={'+','.join(map(str,bits))+'};for(unsigned x=0;x<65536;x++){uint32_t i=0;for(unsigned b=0;b<16;b++)i|=((x>>b)&1u)<<bits[b];for(unsigned c=0;c<16;c++){check(i|(c<<28));check(i|(c<<28)|'+hex((~mask)&0xfffffff)+'u);}}check(0xe320f003u);for(unsigned b=0;b<32;b++)check(0xe320f003u^(1u<<b));printf("PASS %u independent selections\\n",checks);}\n')
(p/'independent-classify.c').write_text(''.join(code))
report={'independentFinalArtifactParse':True,'originalGuardBodyFragments':len(original),'candidateGuardBodyFragments':len(evidence),'bodyAndGuardTokensExact':True,'withinClassOrderExact':True,'prefixAndEpilogueBytesExact':True,'groups':groups,'nonWfiGuardMask':hex(mask),'nonWfiGuardBits':bits,'allInsnsUsesAudited':True,'fullWordPredicate':'insn == 0xe320f003u','proofScope':'Every non-WFI guard is proven to read only masked16bits. Ignored0/1 representatives cannot equal WFI because its ignored bits are neither all0 nor all1. Hence projections cover every other32bit word; exactWFI covers the unique exception. Both classification functions use noinline/noipa to keep independent executed paths.','fragments':evidence}
assert 0xe320f003&(~mask&0xfffffff) not in [0,(~mask&0xfffffff)]
(p/'audit.json').write_text(json.dumps(report,indent=2)+'\n');print('Final-artifact structural/dependency audit PASS')
