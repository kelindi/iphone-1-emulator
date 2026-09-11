import {execFileSync} from 'node:child_process';
import {mkdir,readFile,writeFile} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import {resolve} from 'node:path';
const out=resolve(process.env.ARTIFACT_DIR||'.context/adaptive-compiler/machine-runtime-tests');await mkdir(out,{recursive:true});
const files=['core/include/arm.h','core/include/arm_code_versions.h','core/src/soc/mbx.c','core/src/snapshot.c','core/include/snapshot.h','core/include/loader.h','core/src/firmware/loader.c','core/src/boot/bringup_os1.c','core/src/boot/bringup.c','core/include/arm_adaptive_guard.h','core/include/arm_adaptive_jit.h','core/include/arm_adaptive_dispatch.h','core/src/mmu.c','core/src/arm/arm_interp.c','core/src/soc/machine.c','web/adaptive-runtime.c','web/adaptive-runtime.h','tools/jit/adaptive/decode.mjs','tools/jit/adaptive/unit.mjs','tools/jit/adaptive/emit.mjs','tools/jit/adaptive/tests/code-versions.c','tools/jit/adaptive/tests/machine-bridge.c','tools/jit/adaptive/tests/machine-bridge.mjs'];
const sha=b=>createHash('sha256').update(b).digest('hex'),report={passed:false,sources:{},commands:[]};
for(const file of files)report.sources[file]=sha(await readFile(file));
function run(cmd,args,env=process.env){const row={cmd,args};report.commands.push(row);try{row.output=execFileSync(cmd,args,{encoding:'utf8',env,stdio:'pipe'});row.passed=true;}catch(e){row.output=String(e.stdout||'')+String(e.stderr||'');throw e;}}
try{
 run('cc',['-std=c11','-Wall','-Wextra','-Werror','-Icore/include','tools/jit/adaptive/tests/code-versions.c','-o',out+'/code-versions']);
 run(out+'/code-versions',[]);
 const build=out+'/build',emcc=process.env.EMCC||'.context/emsdk/upstream/emscripten/emcc';
 run(process.env.EMCMAKE||'.context/emsdk/upstream/emscripten/emcmake',['cmake','-S','.','-B',build,'-DIPHONE1_WEB=ON','-DIPHONE1_WASM_JIT=OFF','-DIPHONE1_WEB_DIST='+out+'/dist','-DCMAKE_C_FLAGS=-DS5LBOX_ADAPTIVE_JIT=1']);
 run('cmake',['--build',build,'--target','emucore','-j','4']);
 run(emcc,['-O2','-std=c11','-Icore/include','-Iweb','tools/jit/adaptive/tests/machine-bridge.c','web/adaptive-runtime.c',build+'/core/libemucore.a','-lm','-sMODULARIZE=1','-sEXPORT_NAME=createMachine','-sENVIRONMENT=node','-sALLOW_TABLE_GROWTH=1','-sALLOW_MEMORY_GROWTH=1','-sSTACK_SIZE=4194304','-sEXPORTED_FUNCTIONS=["_malloc","_free"]','-sEXPORTED_RUNTIME_METHODS=["addFunction","removeFunction"]','-o',out+'/machine-bridge.cjs']);
 run(process.execPath,['tools/jit/adaptive/tests/machine-bridge.mjs'],{...process.env,ARTIFACT_DIR:out,MACHINE_MODULE:out+'/machine-bridge.cjs'});
 for(const file of files)if(sha(await readFile(file))!==report.sources[file])throw Error('Source changed during qualification: '+file);
 report.passed=true;
}catch(e){report.error=e.stack;process.exitCode=1;}
await writeFile(out+'/build-report.json',JSON.stringify(report,null,2));console.log(JSON.stringify({passed:report.passed,report:out+'/build-report.json',error:report.error}));
