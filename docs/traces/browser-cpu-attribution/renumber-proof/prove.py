from pathlib import Path
import re,json,hashlib,collections
root=Path('.context/jit-attribution/renumber-proof')
paths={'production':('web/dist/emu.js','web/dist/emu.wasm'),'symbols':('.context/jit-attribution/symbols/emu.js','.context/jit-attribution/symbols/emu.wasm')}
sha=lambda s:hashlib.sha256(s.encode() if isinstance(s,str) else s).hexdigest()
mods={}
for name,(js_path,wasm_path) in paths.items():
 text=(root/(name+'.wat')).read_text();js=Path(js_path).read_text()
 forms=re.split(r'\n(?=  \()',text)[1:]; forms[-1]=forms[-1].removesuffix(')\n')
 imports=re.search(r'wasmImports=\{([^}]+)\}',js).group(1)
 bindings={k:v for k,v in re.findall(r'(\w+):(\w+)',imports)}
 assert ','.join(k+':'+v for k,v in bindings.items())==imports
 funcs={};foreign={};other=[]
 for form in forms:
  if form.startswith('  (func '):
   i=int(re.match(r'  \(func \(;(\d+);\)',form).group(1))
   funcs[i]=re.sub(r'\(func \(;\d+;\)','(func',form,count=1)
  elif form.startswith('  (import '):
   m=re.fullmatch(r'  \(import "a" "(\w+)" \(func \(;(\d+);\) \(type (\d+)\)\)\)',form)
   assert m,form
   field,i,t=m.groups();foreign[int(i)]=(bindings[field],int(t))
  else:other.append(form)
 mods[name]={'funcs':funcs,'foreign':foreign,'other':other,'js':js,'wasmSha':sha(Path(wasm_path).read_bytes())}
a,b=[mods[n] for n in ['production','symbols']]
assert len(a['funcs'])==len(b['funcs'])==223
assert set(a['foreign'].values())==set(b['foreign'].values())
mapping={i:next(j for j,v in b['foreign'].items() if v==value) for i,value in a['foreign'].items()}
call=re.compile(r'\b(call|return_call|ref\.func) (\d+)\b')
shape=lambda body:call.sub(lambda m:m[1]+' @',body)
A=collections.defaultdict(list);B=collections.defaultdict(list)
for i,body in a['funcs'].items():A[sha(shape(body))].append(i)
for i,body in b['funcs'].items():B[sha(shape(body))].append(i)
assert set(A)==set(B),'Non-call instruction structure differs'
for key in A:
 assert len(A[key])==len(B[key]),'Shape multiplicity differs'
 if len(A[key])==1:mapping[A[key][0]]=B[key][0]
unresolved={i:list(B[sha(shape(body))]) for i,body in a['funcs'].items() if i not in mapping}
for _ in range(231):
 changed=False
 for i,candidates in list(unresolved.items()):
  acalls=[int(m[2]) for m in call.finditer(a['funcs'][i])]
  possible=[]
  for j in candidates:
   if j in mapping.values():continue
   bcalls=[int(m[2]) for m in call.finditer(b['funcs'][j])]
   assert len(acalls)==len(bcalls)
   if all(x not in mapping or mapping[x]==y for x,y in zip(acalls,bcalls)):possible.append(j)
  assert possible,(i,candidates)
  if len(possible)==1:mapping[i]=possible[0];del unresolved[i];changed=True
  else:unresolved[i]=possible
 if not changed:break
print('mapped',len(mapping),'unresolved',unresolved)
(root/'mapping-candidate.json').write_text(json.dumps(mapping,indent=2)+'\n')
assert not unresolved,'Ambiguous mapping: reject names'
assert len(set(mapping.values()))==len(mapping)==231
for i,body in a['funcs'].items():
 translated=call.sub(lambda m:m[1]+' '+str(mapping[int(m[2])]),body)
 assert translated==b['funcs'][mapping[i]],('Body mismatch',i,mapping[i])
# Every direct function reference in instructions is covered. Other forms,
# including tables/exports/start, are checked separately below before acceptance.
print('All223 function bodies match EXACTLY under complete function bijection')
(root/'body-proof.json').write_text(json.dumps({'passed':True,'scope':'Function bodies and import binding/types only; remaining module structure pending, names not accepted yet.','functions':223,'imports':8,'mapping':mapping,'productionSha256':a['wasmSha'],'symbolsSha256':b['wasmSha']},indent=2)+'\n')
translated_other=[]
for form in a['other']:
 if form.startswith('  (export '):
  form=re.sub(r'\(func (\d+)\)',lambda m:'(func '+str(mapping[int(m[1])])+')',form)
 elif form.startswith('  (elem '):
  match=re.fullmatch(r'(  \(elem \(;0;\) \(i32.const 1\) func )([0-9 ]+)\)',form)
  assert match,'Unhandled element form: reject'
  form=match[1]+' '.join(str(mapping[int(i)]) for i in match[2].split())+')'
 elif form.startswith('  (start '):
  raise AssertionError('Unhandled start form: reject')
 else:
  assert form.startswith(('  (type ','  (table ','  (memory ','  (global ','  (data ')),'Unhandled module form'
 translated_other.append(form)
assert translated_other==b['other'],'Non-function module structure differs after bijection'
# The JS glue defines the host meaning of minified import fields. Everything
# other than that proved bijective binding dictionary must match byte-for-byte.
normalize_js=lambda js:re.sub(r'wasmImports=\{[^}]+\}','wasmImports={VERIFIED_IMPORT_BINDINGS}',js)
assert normalize_js(a['js'])==normalize_js(b['js']),'JS glue changes beyond import binding dictionary'
symbol_file=Path('.context/jit-attribution/symbols/emu.js.symbols')
symbols={int(i):n for i,n in [line.split(':',1) for line in symbol_file.read_text().splitlines()]}
assert set(symbols)==set(mapping.values())
production_symbols={i:symbols[j] for i,j in mapping.items()}
proof={'passed':True,'scope':'All223 complete function bodies, function types, direct calls and references, imports with exact JS bindings, tables/elements, exports, memory, globals and data match under a unique complete231-function bijection. Remaining JS glue byte-identical. Source symbol map is transferred only through this proven mapping, never by equal index. Original production binary remains unchanged.',
 'definedFunctions':223,'imports':8,'bijection':mapping,'productionSymbols':production_symbols,
 'productionSha256':a['wasmSha'],'symbolBuildSha256':b['wasmSha'],'symbolsSha256':sha(symbol_file.read_bytes()),
 'proofSourceSha256':sha(Path(__file__).read_bytes())}
(root/'proof.json').write_text(json.dumps(proof,indent=2)+'\n')
print('PASS full module and JS import binding bijection; names now mapped to exact production indices')
print({i:production_symbols[i] for i in [81,228,59,27,13,111,86,217,177,154]})
