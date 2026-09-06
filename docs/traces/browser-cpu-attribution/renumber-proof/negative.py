from pathlib import Path
import shutil,subprocess,json,re
root=Path('.context/jit-attribution/renumber-proof');base=root/'negatives';base.mkdir(exist_ok=True)
src=(root/'prove.py').read_text();production=(root/'production.wat').read_text();symbols=(root/'symbols.wat').read_text();rows=[]
# Mutate separate text representations; never alter production or linked WASM.
cases={
 'instruction':lambda s:s.replace('i32.const 1','i32.const 2',1),
 'direct-call':lambda s:re.sub(r'\bcall (\d+)',lambda m:'call '+str((int(m[1])+1)%231),s,count=1),
 'table-element':lambda s:re.sub(r'(\(elem .*? func )(\d+)',lambda m:m[1]+str((int(m[2])+1)%231),s,count=1),
 'export':lambda s:s.replace('(export "j"','(export "wrong"',1),
 'memory':lambda s:s.replace('(memory (;0;) 8192 32768)','(memory (;0;) 8193 32768)',1),
}
for name,mutate in cases.items():
 target=base/name;target.mkdir(exist_ok=True)
 (target/'production.wat').write_text(production);bad=mutate(symbols);assert bad!=symbols
 (target/'symbols.wat').write_text(bad)
 script=src.replace("root=Path('.context/jit-attribution/renumber-proof')",'root=Path('+repr(str(target))+')')
 (target/'prove.py').write_text(script)
 result=subprocess.run(['python3',str(target/'prove.py')],text=True,capture_output=True)
 (target/'stdout.log').write_text(result.stdout);(target/'stderr.log').write_text(result.stderr)
 assert result.returncode!=0,name
 rows.append({'case':name,'rejected':True,'failure':result.stderr.strip().splitlines()[-1]})
(root/'negative-proof.json').write_text(json.dumps({'passed':True,'cases':rows},indent=2)+'\n');print(json.dumps(rows))
for name,mutate in {
 'host-import':lambda s:s.replace('a:_emscripten_get_now','a:wrong_host_function',1),
 'javascript-glue':lambda s:s+'\n/* unexpected glue change */\n'
}.items():
 target=base/name;target.mkdir(exist_ok=True)
 (target/'production.wat').write_text(production);(target/'symbols.wat').write_text(symbols)
 original_js=Path('.context/jit-attribution/symbols/emu.js').read_text();bad=mutate(original_js);assert bad!=original_js
 (target/'emu.js').write_text(bad)
 script=src.replace("root=Path('.context/jit-attribution/renumber-proof')",'root=Path('+repr(str(target))+')')
 script=script.replace("'.context/jit-attribution/symbols/emu.js'",repr(str(target/'emu.js')))
 (target/'prove.py').write_text(script)
 result=subprocess.run(['python3',str(target/'prove.py')],text=True,capture_output=True)
 (target/'stdout.log').write_text(result.stdout);(target/'stderr.log').write_text(result.stderr)
 assert result.returncode!=0,name
 rows.append({'case':name,'rejected':True,'failure':result.stderr.strip().splitlines()[-1]})
(root/'negative-proof.json').write_text(json.dumps({'passed':True,'cases':rows},indent=2)+'\n');print(json.dumps(rows[-2:]))
