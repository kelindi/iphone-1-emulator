import assert from 'node:assert/strict';
import {readFile,writeFile} from 'node:fs/promises';
import {validateAssets,requiredAssets} from './validate-assets.mjs';
const base='.context/web-lto/browser',r=JSON.parse(await readFile(base+'/measured/1-original/report-partial.json')),pins=JSON.parse(await readFile(base+'/site-pins.json')),site=pins.sites.original;
const cases=[];
function check(name,rows,expected){const result=validateAssets(rows,site,r.origin);assert.equal(result.passed,expected,name);cases.push({name,passed:true,accepted:result.passed,errors:result.errors});}
check('actual retained 22 response rows',r.assets,true);
for(const name of requiredAssets)check('missing '+name,r.assets.filter(a=>{const u=new URL(a.url);return (u.pathname==='/'?'index.html':u.pathname.slice(1))!==name;}),false);
for(const name of Object.keys(site))check('mismatched '+name,r.assets.map(a=>{const u=new URL(a.url);return (u.pathname==='/'?'index.html':u.pathname.slice(1))===name?{...a,sha256:'0'.repeat(64)}:a;}),false);
for(const [name,patch] of [['method-shaped URL',{url:()=>r.assets[0].url}],['malformed URL',{url:'http://['}],['relative URL',{url:'/app.js'}],['foreign origin',{url:'https://foreign.invalid/app.js'}],['missing digest',{sha256:undefined}],['malformed digest',{sha256:'invalid'}],['capture error',{error:'body unavailable'}]])check(name,[{...r.assets[0],...patch},...r.assets.slice(1)],false);
check('null row',[null,...r.assets],false);check('non-array',null,false);
// Demonstrate validator errors are data, so they cannot skip subsequent cleanup.
let closed=false;try{const result=validateAssets([{url:'http://['}],site,r.origin);assert(!result.passed);}finally{closed=true;}assert(closed);
const result={passed:true,checks:cases.length+1,actualRows:r.assets.length,requiredKinds:requiredAssets.length,cases,cleanupReachedOnMalformed:true,noGuestRun:true};await writeFile(base+'/reporter-fix/tests.json',JSON.stringify(result,null,2)+'\n');console.log(JSON.stringify({...result,cases:undefined}));
