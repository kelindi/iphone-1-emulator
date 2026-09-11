// Isolated real browser API build. Never writes product dist/seed/offline assets.
import {execFileSync} from 'node:child_process';
import {mkdir,readFile,writeFile} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import {resolve} from 'node:path';
const out=resolve(process.env.ARTIFACT_DIR||'.context/adaptive-compiler/workload-runtime');
await mkdir(out); // A qualification build must not overwrite earlier evidence.
const sources=execFileSync('rg',['--files','core'],{encoding:'utf8'}).trim().split('\n');
sources.push('CMakeLists.txt','web/emu_api.c','web/adaptive-runtime.c','web/adaptive-runtime.h','tools/jit/adaptive/workload-bridge.c','tools/jit/adaptive/build-workload-runtime.mjs','tools/jit/adaptive/smoke-workload-runtime.mjs');
const sha=b=>createHash('sha256').update(b).digest('hex');
const report={passed:false,scope:'Isolated ABI2 real browser API build; no app correctness or performance claim.',sources:{},commands:[]};
for(const file of sources)report.sources[file]=sha(await readFile(file));
function run(command,args){const row={command,args};report.commands.push(row);try{row.output=execFileSync(command,args,{encoding:'utf8',stdio:'pipe'});row.passed=true;}catch(e){row.output=String(e.stdout||'')+String(e.stderr||'');throw e;}}
try{
 const emcc=process.env.EMCC||'.context/emsdk/upstream/emscripten/emcc';
 run(process.env.EMCMAKE||'.context/emsdk/upstream/emscripten/emcmake',['cmake','-S','.','-B',out+'/build','-DIPHONE1_WEB=ON','-DIPHONE1_WASM_JIT=OFF','-DIPHONE1_WEB_DIST='+out+'/unused-dist','-DCMAKE_C_FLAGS=-DS5LBOX_ADAPTIVE_JIT=1']);
 run('cmake',['--build',out+'/build','--target','emucore','-j','4']);
 run(emcc,['-O3','-std=c11','-DS5LBOX_ADAPTIVE_JIT=1','-Icore/include','-Iweb','tools/jit/adaptive/workload-bridge.c','web/adaptive-runtime.c',out+'/build/core/libemucore.a','-lm','-sMODULARIZE=1','-sEXPORT_NAME=createWorkload','-sENVIRONMENT=node','-sALLOW_TABLE_GROWTH=1','-sALLOW_MEMORY_GROWTH=1','-sINITIAL_MEMORY=536870912','-sSTACK_SIZE=4194304','-sEXPORTED_FUNCTIONS=["_malloc","_free"]','-sEXPORTED_RUNTIME_METHODS=["HEAPU8","HEAPU32","addFunction","removeFunction"]','-o',out+'/workload.cjs']);
 for(const file of sources)if(sha(await readFile(file))!==report.sources[file])throw Error('Source changed during build: '+file);
 report.wasmSha256=sha(await readFile(out+'/workload.wasm'));report.wrapperSha256=sha(await readFile(out+'/workload.cjs'));report.passed=true;
}catch(e){report.error=e.stack;process.exitCode=1;}
await writeFile(out+'/build-report.json',JSON.stringify(report,null,2)+'\n');
console.log(JSON.stringify({passed:report.passed,report:out+'/build-report.json',error:report.error}));
