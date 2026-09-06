from pathlib import Path
import subprocess,os,json,hashlib
root=Path.cwd();p=root/'.context/web-lto';em=root/'.context/emsdk/upstream/emscripten/emcc';cmds=[]
def run(cmd,name,allowed=(0,),env=None):
 cmds.append(cmd);(p/'relative-focused-commands.json').write_text(json.dumps(cmds,indent=2))
 with open(p/name,'w')as f:r=subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,env=env)
 assert r.returncode in allowed,(name,r.returncode)
 return r.returncode
common=[str(em),'-O3','-DNDEBUG','-flto=full','-DS5LBOX_WASM_JIT=1','-I'+str(p/'source/core/include'),'-I'+str(p)];archive=str(p/'candidate-build/core/libemucore.a')
run(common+[str(p/'oracle.c'),archive,'-lm','-sINITIAL_MEMORY=536870912','-sSTACK_SIZE=4194304','-sALLOW_MEMORY_GROWTH=1','-sALLOW_TABLE_GROWTH=1','-sMODULARIZE=1','-sEXPORT_NAME=createOracle','-sENVIRONMENT=node','-sEXPORTED_RUNTIME_METHODS=["addFunction","removeFunction","HEAPU8","HEAPU32"]','-sEXPORTED_FUNCTIONS=["_malloc","_free"]','-o',str(p/'candidate-oracle.cjs')],'candidate-oracle-build.log')
run(['node',str(p/'test-boundaries.cjs')],'candidate-boundaries.log',env={**os.environ,'ORACLE_MODULE':str(p/'candidate-oracle.cjs'),'COMPILER_PATH':str(root/'tools/jit/compiler.cjs'),'ARTIFACT_DIR':str(p/'boundaries-candidate')})
a=json.loads((p/'boundaries-control/focused-report.json').read_text());b=json.loads((p/'boundaries-candidate/focused-report.json').read_text());assert a['checks']==b['checks']==404 and len(a['cases'])==len(b['cases'])==1212
for x,y in zip(a['cases'],b['cases']):assert x==y,(x['label'],x['engine'])
run(common+[str(p/'source/core/tests/test_vfp.c'),archive,'-lm','-sSTACK_SIZE=4194304','-sENVIRONMENT=node','-o',str(p/'candidate-vfp.cjs')],'candidate-vfp-build.log')
code=run(['node',str(p/'candidate-vfp.cjs')],'candidate-vfp.log',allowed=(0,1))
a=(p/'control-vfp.log').read_text();b=(p/'candidate-vfp.log').read_text();assert a==b,'Actual VFP control/candidate output differs'
assert code==1 and '595 passed, 12 failed'in a
out={'relativePassed':True,'vfpConformancePassed':False,'boundaryScenarios':404,'pairedEngineCases':1212,'allCaseFieldsExact':True,'stateVectorsRetainedBase64':True,'vfpControlAndCandidate':{'passedChecks':595,'failedChecks':12,'exitCode':1,'entireOutputExact':True,'failures':[s for s in a.splitlines()if 'FAIL'in s]},'scope':'Unchanged literal conformance failures retained; relative LTO execution matches control. Original compiler/guards, no VFP fix.'}
(p/'relative-focused-report.json').write_text(json.dumps(out,indent=2));print(json.dumps(out,indent=2))
