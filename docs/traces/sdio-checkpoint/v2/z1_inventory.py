"""Independent header inventory; validate explicit wire schema and typed widths."""
from pathlib import Path
import re,json
p=Path(__file__).resolve().parent
h=re.sub(r'/\*.*?\*/','',Path('core/include/soc.h').read_text(),flags=re.S)
structs={name:body for body,name in re.findall(r'typedef struct\s*\{([^{}]*)\}\s*(\w+)\s*;',h,re.S)}
dim={'Z1_PARAM_BYTES':49,'MTZ2_PAYLOAD_LIMIT':176,'MTZ2_CONTACT_MAX':5}
# Resolve payload capacity from actual header definition, do not assume CC size.
line=re.search(r'#define\s+MTZ2_PAYLOAD_LIMIT\s+([^\n]+)',h)[1]
if re.fullmatch(r'\s*(\d+)[uU]?\s*',line):dim['MTZ2_PAYLOAD_LIMIT']=int(re.search(r'\d+',line)[0])
else:raise ValueError('review changed payload capacity expression')
expected={}
def walk(name,prefix=''):
 for decl in structs[name].split(';'):
  decl=decl.strip()
  if not decl:continue
  t,ns=re.match(r'(\w+)\s+(.+)',decl,re.S).groups()
  for n in ns.split(','):
   n=n.strip();base=n.split('[')[0];path=prefix+base
   if path=='trace':continue
   array=re.search(r'\[([^]]+)\]',n)
   count=(int(array[1]) if array[1].isdigit() else dim[array[1]]) if array else None
   if t in structs:
    for i in range(count or 1):walk(t,path+('['+str(i)+']' if count else '')+'.')
   else:expected[path]=(t,count)
walk('s5l_mtz1_t')
fields=re.findall(r'(BOOL|U8|U16|U32|U64|BYTES)\(([^)]+)\)',(p/'z1-fields.inc').read_text())
assert len(fields)==len({name for k,name in fields})
assert set(expected)=={name for k,name in fields},(set(expected)-{name for k,name in fields},{name for k,name in fields}-set(expected))
kinds={'bool':'BOOL','uint8_t':'U8','uint16_t':'U16','uint32_t':'U32','uint64_t':'U64'}
sizes={'BOOL':1,'U8':1,'U16':2,'U32':4,'U64':8}
offset=0;rows=[]
for kind,name in fields:
 t,count=expected[name];assert kind==('BYTES' if count else kinds[t]);assert not count or t=='uint8_t'
 n=count or sizes[kind];rows.append({'name':name,'kind':kind,'offset':offset,'size':n});offset+=n
(p/'z1-inventory.json').write_text(json.dumps({'passed':True,'serializedLeaves':len(rows),'excludedHostFields':['trace'],'bytes':offset,'fields':rows},indent=2)+'\n')
(p/'z1-independent-equal.inc').write_text('/* Generated from actual header, not serialization macros. */\n'+'\n'.join(('CHECK(!memcmp(a->%s,b->%s,sizeof a->%s));'%(name,name,name)) if count else 'CHECK(a->%s==b->%s);'%(name,name) for name,(t,count) in expected.items())+'\n')
print('PASS Z1 inventory',len(rows),'leaves',offset,'bytes; only host trace excluded')
