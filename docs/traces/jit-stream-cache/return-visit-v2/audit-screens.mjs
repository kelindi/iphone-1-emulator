import {readFile,writeFile} from 'node:fs/promises';
import {resolve,dirname} from 'node:path';
import {fileURLToPath} from 'node:url';
import {createHash} from 'node:crypto';
import {compareRaw} from './frames.mjs';
const base=dirname(fileURLToPath(import.meta.url));const series=JSON.parse(await readFile(resolve(base,'measured/series.json')));const rows=[];const sha=x=>createHash('sha256').update(x).digest('hex');
for(const run of series.runs){
 const folder=resolve(base,'measured',`${run.index}-${run.variant}`);let report=run.report;
 if(!report){try{report=JSON.parse(await readFile(resolve(folder,'report-partial.json')));}catch{continue;}}
 for(const action of report.actions){const name=action.label+'-'+action.kind;const ref=(action.round===1?'C0':'H1')+'-'+action.kind;
 const [before,after,reference]=await Promise.all([readFile(resolve(folder,name+'-before.rgba')),readFile(resolve(folder,name+'.rgba')),readFile(resolve(base,'references',ref+'.rgba'))]);
 const stable=compareRaw(before,after),againstOriginal=compareRaw(reference,after);
 const same=(a,b)=>JSON.stringify(a)===JSON.stringify(b);
 const reportedStable=action.validation.stable,reportedReference=Object.fromEntries(Object.entries(action.validation.reference).filter(([k])=>k!=='sha256'));
 rows.push({index:run.index,variant:run.variant,label:action.label,kind:action.kind,rawSha256:sha(after),referenceSha256:sha(reference),stable,againstOriginal,reportedIdentity:sha(after)===action.validation.rawSha256,reportedComparisons:same(stable,reportedStable)&&same(againstOriginal,reportedReference),passed:stable.passed&&againstOriginal.passed});
 }}
const report={scope:'Independent reread of all raw frame pairs and same-labelled frozen original references; original RGB<=2 below30/exactalpha unchanged.',expectedStages:88,actualStages:rows.length,allStagesPresent:rows.length===88,passed:rows.length===88&&rows.every(r=>r.passed&&r.reportedIdentity&&r.reportedComparisons),failed:rows.filter(r=>!r.passed||!r.reportedIdentity||!r.reportedComparisons),rows};
await writeFile(resolve(base,'screen-audit.json'),JSON.stringify(report,null,2)+'\n');console.log(JSON.stringify({passed:report.passed,stages:rows.length,failed:report.failed}));
