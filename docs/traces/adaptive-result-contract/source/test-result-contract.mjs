import {execFileSync} from 'node:child_process';
import {mkdir,readFile,writeFile} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import {resolve} from 'node:path';
const out=resolve(process.env.ARTIFACT_DIR||'.context/adaptive-compiler/result-contract-tests');await mkdir(out,{recursive:true});
const files=['core/include/arm.h','core/include/arm_adaptive_jit.h','tools/jit/adaptive/tests/result-contract.c','tools/jit/adaptive/tests/bridge.c','tools/jit/adaptive/tests/bridge.mjs','tools/jit/adaptive/decode.mjs','tools/jit/adaptive/unit.mjs','tools/jit/adaptive/emit.mjs'];
const sha=b=>createHash('sha256').update(b).digest('hex');
const report={passed:false,scope:'ABI2 result validation and cycle ownership, native C and actual generated WASM through C. No machine scheduler/admission qualification.',sources:{},commands:[]};
for(const file of files)report.sources[file]=sha(await readFile(file));
function run(cmd,args,env=process.env){const row={cmd,args};report.commands.push(row);try{row.output=execFileSync(cmd,args,{encoding:'utf8',env,stdio:'pipe'});row.passed=true;}catch(e){row.output=String(e.stdout||'')+String(e.stderr||'');throw e;}}
try{
 run(process.env.CC||'cc',['-std=c11','-Wall','-Wextra','-Werror','-Icore/include','tools/jit/adaptive/tests/result-contract.c','-o',out+'/native']);
 run(out+'/native',[]);
 run(process.env.EMCC||'.context/emsdk/upstream/emscripten/emcc',['-O2','-std=c11','-Icore/include','tools/jit/adaptive/tests/bridge.c','-sMODULARIZE=1','-sEXPORT_NAME=createBridge','-sENVIRONMENT=node','-sALLOW_TABLE_GROWTH=1','-sEXPORTED_RUNTIME_METHODS=["addFunction","removeFunction"]','-o',out+'/bridge.cjs']);
 run(process.execPath,['tools/jit/adaptive/tests/bridge.mjs'],{...process.env,BRIDGE_MODULE:out+'/bridge.cjs'});
 for(const file of files)if(sha(await readFile(file))!==report.sources[file])throw Error('Source changed during test: '+file);
 report.passed=true;
}catch(e){report.error=e.stack;process.exitCode=1;}
await writeFile(out+'/report.json',JSON.stringify(report,null,2));console.log(JSON.stringify({passed:report.passed,report:out+'/report.json',error:report.error}));
