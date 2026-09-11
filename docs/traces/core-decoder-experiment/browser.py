from pathlib import Path
import os,shutil,subprocess,json,time,socket,hashlib
p=Path('.context/core-hotspot');report={'runs':[],'roundsPerSession':11,'order':['control','candidate','candidate','control']}
for arm in ['control','candidate']:
 site=p/(arm+'-site');site.mkdir(exist_ok=True)
 for name in ['firmware','jit']:
  shutil.copytree(Path('web')/name,site/name,copy_function=os.link)
 for f in Path('web').iterdir():
  if f.is_file():os.link(f,site/f.name)
 shutil.copytree(p/arm,site/'dist')
# Maintained harness with only a served-byte identity observer and raw guest
# frames added outside measured readiness. Normal UI/autosave remain enabled.
s=Path('web/tests/performance.mjs').read_text()
s=s.replace("import {mkdir,writeFile} from 'node:fs/promises';","import {mkdir,writeFile,readFile} from 'node:fs/promises';\nimport {createHash} from 'node:crypto';\nimport {gzipSync} from 'node:zlib';")
s=s.replace("const page=await context.newPage();","const page=await context.newPage();\nconst assetTasks=[],moduleBodies=[];page.on('response',r=>{if(/\\/(dist\\/emu|jit\\/seed)\\.wasm$/.test(r.url()))assetTasks.push((async()=>{const b=await r.body();moduleBodies.push({url:r.url(),sha256:createHash('sha256').update(b).digest('hex')});})());});")
s=s.replace("   console.log(JSON.stringify({kind,round,firstVisibleMs:","   const raw=await page.evaluate(()=>{const a=document.querySelector('#screen').getContext('2d').getImageData(0,0,320,480).data;let s='';for(let i=0;i<a.length;i+=32768)s+=String.fromCharCode(...a.subarray(i,i+32768));return btoa(s);});await writeFile(resolve(dir,`${round}-${kind}.rgba.gz`),gzipSync(Buffer.from(raw,'base64')));\n   console.log(JSON.stringify({kind,round,firstVisibleMs:")
s=s.replace(" report.passed=report.errors.length===0;"," await Promise.all(assetTasks);report.moduleBodies=moduleBodies;for(const [suffix,file] of [['/dist/emu.wasm',process.env.EXPECT_CORE],['/jit/seed.wasm','web/jit/seed.wasm']]){const expected=createHash('sha256').update(await readFile(file)).digest('hex');const rows=moduleBodies.filter(x=>x.url.endsWith(suffix));if(!rows.length||rows.some(x=>x.sha256!==expected))throw Error('Module identity '+suffix);}\n report.passed=report.errors.length===0;")
(p/'performance.mjs').write_text(s)
for i,arm in enumerate(report['order']):
 assert shutil.disk_usage(p).free>5*2**30,'Chrome disk reserve'
 site=p/(arm+'-site');env=dict(os.environ,WEB_ROOT=str(site),PORT='8244');log=(p/f'browser-{i}-server.log').open('x');server=subprocess.Popen(['node','tools/web-server.mjs'],env=env,stdout=log,stderr=log)
 try:
  for _ in range(100):
   try:
    with socket.create_connection(('127.0.0.1',8244),timeout=.2):break
   except OSError:time.sleep(.1)
  out=p/f'browser-{i}-{arm}';env.update(BASE_URL='http://127.0.0.1:8244',ARTIFACT_DIR=str(out),ROUNDS='11',EXPECT_CORE=str(p/arm/'emu.wasm'))
  with (p/f'browser-{i}.log').open('x') as f:result=subprocess.run(['node',str(p/'performance.mjs')],env=env,stdout=f,stderr=f,timeout=300)
  report['runs'].append({'arm':arm,'directory':str(out),'exit':result.returncode});(p/'browser-controller.json').write_text(json.dumps(report,indent=2));print(report['runs'][-1],flush=True)
  if result.returncode:break
 finally:server.terminate();server.wait(timeout=10);log.close()
report['closed']=True;(p/'browser-controller.json').write_text(json.dumps(report,indent=2))
