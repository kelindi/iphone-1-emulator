from pathlib import Path
import hashlib,shutil
b=Path('.context/mbx-stride-integration');original=Path('.context/rtc-release-upgrade/offline-upgrade.mjs');saved=b/'original-offline-upgrade.mjs';assert not saved.exists();shutil.copyfile(original,saved);s=original.read_text()
s=s.replace('// Real Chrome upgrade gate. The two test bundles run the genuine guest; only\n// diagnostic shell/manifest markers and a valid WASM custom section differ.','// Saved-phone upgrade gate for the narrow MBX render-target stride repair.\n// Exact old/new cores differ; diagnostic shell/manifest markers identify test generations.')
s=s.replace("const directory = resolve(process.env.ARTIFACT_DIR || '.context/offline-update-report');", "const directory = resolve('.context/mbx-stride-integration/run-1');")
s=s.replace("const sourceRoot = resolve(process.env.SOURCE_WEB_ROOT || 'web');", "const sourceRoot = resolve('.context/mbx-stride-integration/candidate');")
s=s.replace("const previousRoot = process.env.PREVIOUS_WEB_ROOT && resolve(process.env.PREVIOUS_WEB_ROOT);", "const previousRoot = resolve('.context/mbx-stride-integration/previous');")
s=s.replace("Number(process.env.PORT || 8091)","8235")
s=s.replace("import {createHash} from 'node:crypto';", "import {createHash} from 'node:crypto';\nimport {gzipSync} from 'node:zlib';\nimport {validateAssets} from './validate-assets.mjs';")
s=s.replace("const sha = bytes", "const json = value => JSON.stringify(value,(_,v)=>typeof v==='bigint'?v.toString():v,2);\nconst sha = bytes")
s=s.replace("let phase='prepare';", "let phase='prepare';const servedAssets=[],bodyTasks=[];")
s=s.replace("return variants[version]={generation,hashes};", "hashes['service-worker.js']=sha(Buffer.from(sw));return variants[version]={generation,hashes};")
s=s.replace("'b3237ef3843412aea164d3db111299650ea8433bf16311417116799959ab6606'", "'7a72add6d886440f64a853511375ba51cbbb23f2263831b6a05c51fde7969088'")
s=s.replace("assert.equal(sha(source['dist/emu.wasm']),'7a72add6d886440f64a853511375ba51cbbb23f2263831b6a05c51fde7969088');", "assert.equal(sha(source['dist/emu.wasm']),'3b43ccf4a8292a4da776491aa92185b0ea88e2fc38a3002a6e1fe1d5a6aa4463');")
s=s.replace("context.on('response',r=>browserResponses.push({phase,url:r.url(),status:r.status(),fromServiceWorker:r.fromServiceWorker(),at:Date.now()}));",'''context.on('response',r=>{
 const row={phase,url:r.url(),status:r.status(),fromServiceWorker:r.fromServiceWorker(),at:Date.now()};browserResponses.push(row);
 const u=new URL(row.url),path=u.pathname.replace(/^\\//,'')||'index.html';
 if(u.origin===new URL(url).origin&&[...files,'service-worker.js'].includes(path))bodyTasks.push((async()=>{
  const item={...row,path};servedAssets.push(item);
  try{const bytes=await r.body();item.bytes=bytes.length;item.sha256=sha(bytes);await fs.mkdir(join(directory,'served'),{recursive:true});item.file='served/'+item.sha256+'.bin';await fs.writeFile(join(directory,item.file),bytes,{flag:'wx'}).catch(e=>{if(e.code!=='EEXIST')throw e;});}catch(e){item.error=e.message;}
 })());
});''')
s=s.replace("checks:[],errors:[],variants", "checks:[],errors:[],servedAssets,variants")
s=s.replace("  report.result='pass';", "  await Promise.all(bodyTasks);report.assetValidation=validateAssets(servedAssets,variants,url);\n  report.result='pass';")
s=s.replace("JSON.stringify(report,null,2)","json(report)")
s=s.replace("const original=await saved(); report.savedBefore=original;", "const original=await saved(); report.savedBefore=original;")
s=s.replace("return {savedAt:record.savedAt,version:record.version,size:record.size,storedBytes:record.data.size,hash};", "return {savedAt:record.savedAt,version:record.version,size:record.size,storedBytes:record.data.size,hash,metadata:Object.fromEntries(Object.entries(record).filter(([key])=>key!=='data'))};")
s=s.replace("  await publish('B');\n  // Reload", "  await publish('B');phase='waiting-B';\n  // Reload")
s=s.replace("  page=await context.newPage(); observe(page); await page.goto(url);", "  phase='activate-B';page=await context.newPage(); observe(page); await page.goto(url);")
s=s.replace(" const shell=await cacheName();", " phase='corrupt-seed-offline';const shell=await cacheName();")
s=s.replace("actual RTC runtime", "actual MBX stride-fixed runtime")
s=s.replace("r.phase==='disconnected-B'||r.phase==='corrupt-kernel-offline'", "r.phase==='disconnected-B'||r.phase==='corrupt-seed-offline'||r.phase==='corrupt-kernel-offline'")
s=s.replace("await page.screenshot({path:join(directory,", "await screen(join(directory,")
# close the transformed call shape: screen(join(directory,'name.png')).
import re
s=re.sub(r"await screen\(join\(directory,('[^']+')\)\}\)",r"await screen(join(directory,\1))",s)
insert='''async function screen(path){
 const pixels=await page.evaluate(()=>{const c=document.querySelector('#screen');return c?Array.from(c.getContext('2d').getImageData(0,0,320,480).data):null;});
 if(pixels)await fs.writeFile(path.replace(/\\.png$/,'.rgba.gz'),gzipSync(Buffer.from(pixels)));
 return page.screenshot({path});
}
'''
s=s.replace("async function diagnosticClick(id){",insert+"async function diagnosticClick(id){")
a=s.index('finally {\n  await fs.writeFile(join(directory,\'report.json\')',s.index("try {\n  phase='online-A'"))
s=s[:a]+'''finally {
  await Promise.allSettled(bodyTasks);
  await fs.writeFile(join(directory,'report.json'),json(report)+'\\n');
  clearTimeout(outer);const closure={};
  for(const [name,close]of [['context',()=>context.close()],['browser',()=>browser.close()],['origin',async()=>{if(server.listening)return stopServer(server,sockets,port);}]] ){
   try{closure[name]={closed:true,result:await close()};}catch(e){closure[name]={closed:false,error:e.message};report.result='fail';}
  }
  report.closure=closure;report.browserClosed=closure.context.closed&&closure.browser.closed;
  report.closedAt=new Date().toISOString();await fs.writeFile(join(directory,'report.json'),json(report)+'\\n');
  console.log(json({result:report.result,checks:report.checks.length,error:report.error,closure}));
}
if(report.result!=='pass')process.exitCode=1;
'''
(b/'offline-upgrade.mjs').write_text(s)
