import {readdir,readFile,writeFile,mkdir} from 'node:fs/promises';
import {resolve,relative} from 'node:path';
import {execFileSync} from 'node:child_process';
import {cpus,platform,release} from 'node:os';
import {freezeFiles,verifyFreeze,sha256} from './freeze-lib.mjs';
const root=resolve(process.argv.slice(2).find(arg=>arg!=='--verify')||'.context/adaptive-compiler/baseline');
if(process.argv.includes('--verify')){const manifest=JSON.parse(await readFile(root+'/freeze-manifest.json'));console.log('Verified',await verifyFreeze(root,manifest.entries),'frozen files');process.exit(0);}
const entries=[];
async function collect(source,dest){for(const item of await readdir(source,{withFileTypes:true})){const from=source+'/'+item.name,to=dest+'/'+item.name;if(item.isDirectory())await collect(from,to);else if(item.isFile())entries.push({source:from,relative:to});else throw Error('Unsupported source entry '+from);}}
// Firmware is fetched through the shipped manifest's pinned hashes. Do not
// duplicate unused local firmware, browser profiles, or build directories.
for(const item of await readdir('web',{withFileTypes:true}))if(item.isFile())entries.push({source:'web/'+item.name,relative:'site/'+item.name});
await collect('web/dist','site/dist');await collect('web/jit','site/jit');
await collect('core','source/core');await collect('tools/jit','source/tools/jit');
await collect('web/tests/helpers','harness/helpers');
for(const name of ['package.json','package-lock.json','docs/adaptive-compiler-contracts.md'])entries.push({source:name,relative:'source/'+name});
entries.push({source:'web/tests/performance.mjs',relative:'harness/performance.mjs'});
await mkdir(resolve(root,'..'),{recursive:true});
const rows=await freezeFiles(entries,root);
const manifest={version:1,created:new Date().toISOString(),revision:execFileSync('git',['rev-parse','HEAD'],{encoding:'utf8'}).trim(),workingTree:execFileSync('git',['status','--porcelain'],{encoding:'utf8'}),host:{platform:platform(),release:release(),cpu:cpus()[0]?.model},entries:rows,firmware:JSON.parse(await readFile(root+'/site/manifest.json')),scope:'Independent baseline copies; firmware identities pinned by shipped manifest. Cloud host is not reference-Mac acceptance.'};
await writeFile(root+'/freeze-manifest.json',JSON.stringify(manifest,null,2)+'\n');
console.log(JSON.stringify({root,files:rows.length,manifestSha256:sha256(await readFile(root+'/freeze-manifest.json'))}));
