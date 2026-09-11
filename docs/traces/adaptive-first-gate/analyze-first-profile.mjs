import fs from 'node:fs';
const root='.context/adaptive-compiler',p=JSON.parse(fs.readFileSync(root+'/workload-first.cpuprofile')),b=fs.readFileSync(root+'/workload-refresh-runtime/workload.wasm');
let at=8;function u(){let n=0,s=0,c;do{c=b[at++];n|=(c&127)<<s;s+=7;}while(c&128);return n>>>0;}function str(){const n=u(),s=b.subarray(at,at+n).toString();at+=n;return s;}
const exports={};while(at<b.length){const id=b[at++],len=u(),end=at+len;if(id===7){const n=u();for(let i=0;i<n;i++){const name=str(),kind=b[at++],index=u();if(kind===0)exports[index]=name;}}at=end;}
const nodes=new Map(p.nodes.map(n=>[n.id,n])),parent=new Map();for(const n of p.nodes)for(const c of n.children||[])parent.set(c,n.id);
const wasmUrls=[...new Set(p.nodes.filter(n=>n.callFrame.url.startsWith('wasm:')).map(n=>n.callFrame.url))];
// Main module has emu_run export at matching function index; confirm candidates.
const wrapper=fs.readFileSync(root+'/workload-refresh-runtime/workload.cjs','utf8');
const aliases=Object.fromEntries([...wrapper.matchAll(/Module\["_([^"\]]+)"\]=wasmExports\["([^"\]]+)"\]/g)].map(m=>[m[2],m[1]]));for(const k in exports)exports[k]=aliases[exports[k]]||exports[k];
const runIndex=Object.keys(exports).find(k=>exports[k]==='emu_run');
const mainUrls=wasmUrls.filter(url=>p.nodes.some(n=>n.callFrame.url===url&&n.callFrame.functionName===`wasm-function[${runIndex}]`));
const main=mainUrls.find(url=>p.nodes.filter(n=>n.callFrame.url===url).every(n=>n.callFrame.columnNumber<b.length));if(!main)throw Error('No matching core offsets');const wabtUrl='wasm://wasm/00249afe';
const symbolMap=JSON.parse(fs.readFileSync(root+'/workload-first-profile/symbol-mapping.json'));
function label(n){const f=n.callFrame,index=/wasm-function\[(\d+)\]/.exec(f.functionName)?.[1];return f.url===main?`core:${exports[index]||symbolMap[index]?.name||f.functionName}`:f.url===wabtUrl?`wabt:${f.functionName}`:f.url.startsWith('wasm:')?`generated:${f.functionName}`:`${f.functionName||'(anonymous)'} @ ${f.url}`;}
const self=new Map(),inclusive=new Map(),cats=new Map();let total=0;for(let i=0;i<p.samples.length;i++){const us=p.timeDeltas[i],id=p.samples[i],n=nodes.get(id);total+=us;const l=label(n);self.set(l,(self.get(l)||0)+us);const labels=new Set();let chain=[],cur=id;while(cur){const nd=nodes.get(cur);labels.add(label(nd));chain.push(nd);cur=parent.get(cur);}for(const k of labels)inclusive.set(k,(inclusive.get(k)||0)+us);
let category=n.callFrame.url===main?'core WASM':n.callFrame.url===wabtUrl?'WABT WASM':n.callFrame.url.startsWith('wasm:')?'generated unit WASM':n.callFrame.url.includes('wabt')?'WABT JS':chain.some(x=>x.callFrame.functionName==='compile'&&x.callFrame.url.endsWith('workload-compiler.mjs'))?'compiler other':n.callFrame.functionName==='(garbage collector)'?'GC':'other/setup/report';cats.set(category,(cats.get(category)||0)+us);
}
const rows=m=>[...m].sort((a,b)=>b[1]-a[1]).map(([functionName,us])=>({functionName,sampledMs:us/1000,percent:us/total*100}));
const report={scope:'Main-thread V8 sampling, entire process incl setup/checkpoint; weighted sample intervals, not gate CPU attribution.',mainModuleUrl:main,totalSampledMs:total/1000,exports,categories:rows(cats),self:rows(self),inclusive:rows(inclusive)};
fs.writeFileSync(root+'/workload-first-profile/analysis.json',JSON.stringify(report,null,2));console.log(JSON.stringify({...report,self:report.self.slice(0,24),inclusive:report.inclusive.slice(0,18)},null,2));
