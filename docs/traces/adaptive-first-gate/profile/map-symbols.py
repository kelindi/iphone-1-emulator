import re,json,hashlib
from pathlib import Path
root=Path('.context/adaptive-compiler/workload-first-profile')
names=dict(line.split(':',1) for line in (root/'symbols.cjs.symbols').read_text().splitlines())
def read(name):
 s=(root/name).read_text();matches=list(re.finditer(r'^ \(func \$(\d+) ',s,re.M));out={}
 for m in matches:
  start=m.start();i=start;depth=0;beg=False
  while i<len(s):
   if s[i]=='(':depth+=1;beg=True
   elif s[i]==')':
    depth-=1
    if beg and depth==0:break
   i+=1
  text=s[start:i+1];text=re.sub(r'^ \(func \$\d+', '(func $F',text);text=re.sub(r'\((call|ref.func) \$[^\s)]+',r'(\1 $F',text);text=re.sub(r'\(type \$\d+\)','(type $T)',text)
  out[int(m[1])+9]=hashlib.sha256(text.encode()).hexdigest()
 return out
a=read('original.wat');b=read('symbols.wat');mapping={}
for index,h in a.items():
 cand=[k for k,v in b.items() if v==h]
 if len(cand)==1:mapping[index]={'name':names[str(cand[0])],'relinkedIndex':cand[0],'normalizedBodySha256':h}
(root/'symbol-mapping.json').write_text(json.dumps(mapping,indent=2))
print('Unique normalized-body matches',len(mapping),'/',len(a))
for i in [9,14,15,19,35,44,81,107,113,128,157,186]:print(i,mapping.get(i))
