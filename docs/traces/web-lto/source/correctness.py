from pathlib import Path
import subprocess,json,hashlib,shutil,os
p=Path('.context/web-lto').resolve();root=Path.cwd();em=root/'.context/emsdk/upstream/emscripten/emcc';commands=[]
def run(cmd,log,env=None):
 commands.append(cmd);(p/'correctness-commands.json').write_text(json.dumps(commands,indent=2))
 with open(p/log,'w')as f:subprocess.run(cmd,check=True,stdout=f,stderr=subprocess.STDOUT,env=env)
sha=lambda f:hashlib.sha256(Path(f).read_bytes()).hexdigest()
shutil.copy2(root/'.context/jit-fallback-census/oracle.c',p/'oracle.c');shutil.copy2(p/'source/web/jit-runtime.c',p/'runtime-baseline.c');shutil.copy2(p/'source/web/jit-runtime.h',p/'jit-runtime.h')
s=(root/'.context/jit-fallback-census/test-boundaries.cjs').read_text();s=s.replace("stateSha256:crypto.createHash('sha256').update(bytes).digest('hex'),","stateSha256:crypto.createHash('sha256').update(bytes).digest('hex'),stateBase64:bytes.toString('base64'),")
assert s!=(root/'.context/jit-fallback-census/test-boundaries.cjs').read_text();(p/'test-boundaries.cjs').write_text(s)
for kind in ['control','candidate']:
 flags=['-O3','-DNDEBUG']+(['-flto=full']if kind=='candidate'else[])
 common=[str(em),*flags,'-DS5LBOX_WASM_JIT=1','-I'+str(p/'source/core/include'),'-I'+str(p)]
 archive=str(p/(kind+'-build')/'core/libemucore.a')
 cmd=common+[str(p/'oracle.c'),archive,'-lm','-sINITIAL_MEMORY=536870912','-sSTACK_SIZE=4194304','-sALLOW_MEMORY_GROWTH=1','-sALLOW_TABLE_GROWTH=1','-sMODULARIZE=1','-sEXPORT_NAME=createOracle','-sENVIRONMENT=node','-sEXPORTED_RUNTIME_METHODS=["addFunction","removeFunction","HEAPU8","HEAPU32"]','-sEXPORTED_FUNCTIONS=["_malloc","_free"]','-o',str(p/(kind+'-oracle.cjs'))]
 run(cmd,kind+'-oracle-build.log')
 env={**os.environ,'ORACLE_MODULE':str(p/(kind+'-oracle.cjs')),'COMPILER_PATH':str(root/'tools/jit/compiler.cjs'),'ARTIFACT_DIR':str(p/('boundaries-'+kind))}
 run(['node',str(p/'test-boundaries.cjs')],kind+'-boundaries.log',env)
 # The unchanged test exercises literal VFP/FPSCR results through actual WASM arm_step.
 run(common+[str(p/'source/core/tests/test_vfp.c'),archive,'-lm','-sSTACK_SIZE=4194304','-sENVIRONMENT=node','-o',str(p/(kind+'-vfp.cjs'))],kind+'-vfp-build.log')
 run(['node',str(p/(kind+'-vfp.cjs'))],kind+'-vfp.log')
a=json.loads((p/'boundaries-control/focused-report.json').read_text());b=json.loads((p/'boundaries-candidate/focused-report.json').read_text());assert a['checks']==b['checks']==404 and len(a['cases'])==len(b['cases'])==1212
for x,y in zip(a['cases'],b['cases']):assert x==y,(x['label'],x['engine'])
assert (p/'control-vfp.log').read_text()==(p/'candidate-vfp.log').read_text()
out={'passed':True,'boundaryScenarios':404,'pairedEngineCases':1212,'allCaseFieldsExact':True,'stateVectorsRetainedBase64':True,'vfpResult':(p/'candidate-vfp.log').read_text().strip(),'sourceSha256':{n:sha(p/n)for n in ['oracle.c','runtime-baseline.c','jit-runtime.h','test-boundaries.cjs']},'compilerSha256':sha(root/'tools/jit/compiler.cjs'),'scope':'Separate original runtime policy control/LTO cores. No census or continuation. Original tests plus raw state-vector retention only.'}
(p/'focused-report.json').write_text(json.dumps(out,indent=2));print(json.dumps(out,indent=2))
