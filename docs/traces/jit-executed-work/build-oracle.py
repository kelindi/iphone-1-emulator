from pathlib import Path
import subprocess,json,hashlib
p=Path('.context/jit-executed-work').resolve();root=Path.cwd();commands=[]
for kind in ['control','observer']:
 cmd=[str(root/'.context/emsdk/upstream/emscripten/emcc'),'-O3','-DNDEBUG','-DS5LBOX_WASM_JIT=1','-I'+str(root/'core/include'),'-I'+str(p),str(p/'oracle.c'),str(p/('base-libemucore.a' if kind=='control' else 'observer-libemucore.a')),'-lm','-sINITIAL_MEMORY=536870912','-sALLOW_MEMORY_GROWTH=1','-sALLOW_TABLE_GROWTH=1','-sMODULARIZE=1','-sEXPORT_NAME=createOracle','-sENVIRONMENT=node','-sEXPORTED_RUNTIME_METHODS=["addFunction","removeFunction","HEAPU8","HEAPU32"]','-sEXPORTED_FUNCTIONS=["_malloc","_free"]','-o',str(p/(kind+'-oracle.cjs'))]
 if kind=='observer':cmd.extend(['-DFC_ORACLE=1',str(p/'census.c.o')])
 commands.append(cmd);subprocess.run(cmd,check=True)
(p/'oracle-build-provenance.json').write_text(json.dumps({'commands':commands},indent=2)+'\n')
