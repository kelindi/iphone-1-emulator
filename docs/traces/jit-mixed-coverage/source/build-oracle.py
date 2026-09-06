from pathlib import Path
import subprocess,json
p=Path('.context/jit-mixed-coverage').resolve();r=Path.cwd();cmd=[str(r/'.context/emsdk/upstream/emscripten/emcc'),'-O3','-DNDEBUG','-DS5LBOX_WASM_JIT=1','-I'+str(r/'core/include'),'-I'+str(p/'candidate'),str(p/'candidate/oracle.c'),str(r/'.context/jit-fallback-census/base-libemucore.a'),'-lm','-sINITIAL_MEMORY=536870912','-sALLOW_MEMORY_GROWTH=1','-sALLOW_TABLE_GROWTH=1','-sMODULARIZE=1','-sEXPORT_NAME=createOracle','-sENVIRONMENT=node','-sEXPORTED_RUNTIME_METHODS=["addFunction","removeFunction","HEAPU8","HEAPU32"]','-sEXPORTED_FUNCTIONS=["_malloc","_free"]','-o',str(p/'candidate-oracle.cjs')]
(p/'oracle-build.json').write_text(json.dumps({'command':cmd},indent=2));subprocess.run(cmd,check=True)
