from pathlib import Path
import shutil,json,hashlib,os,subprocess
root=Path.cwd();out=root/'.context/jit-continuation/browser';out.mkdir(exist_ok=True)
runtime=root/'.context/jit-continuation'
files=['index.html','phone.css','icon.svg','app.js','storage.js','checkpoint-worker.js','assets.js','emu-worker.js','manifest.json','seed-jit.js','service-worker.template.js']
hash=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
before={f:hash(root/'web'/f) for f in files}
for arm in ['baseline','candidate']:
 web=out/arm;web.mkdir(exist_ok=True)
 for name in files:shutil.copy2(root/'web'/name,web/name)
 if not (web/'firmware').exists():(web/'firmware').symlink_to(root/'web/firmware',target_is_directory=True)
 shutil.copytree(runtime/('baseline-dist' if arm=='baseline' else 'dist'),web/'dist',dirs_exist_ok=True)
 seed=root/'web/jit'
 shutil.copytree(seed,web/'jit',dirs_exist_ok=True)
 script=(root/'tools/build-offline.py').read_text().replace("web = root / 'web'",'web = Path('+repr(str(web))+')')
 exec(compile(script,str(root/'tools/build-offline.py'),'exec'),{'__file__':str(root/'tools/build-offline.py')})
 assert {f:hash(web/f) for f in files}==before
assert {f:hash(root/'web'/f) for f in files}==before,'Frontend changed during freeze'
shutil.copy2(root/'web/tests/performance.mjs',out/'performance.mjs')
subprocess.run(['patch',str(out/'performance.mjs'),str(root/'.context/jit-continuation/performance-receipts.patch')],check=True)
report={'maintainedPerformanceHarnessSha256':hash(root/'web/tests/performance.mjs'),'harnessChange':'Actual browser response SHA receipts before timing only; input/readiness/Save unchanged.','runtimeSource':str(runtime),'frontendSha256':before,'performanceHarnessSha256':hash(out/'performance.mjs'),'runtimeSha256':hash(out/'baseline/dist/emu.wasm'),'runtimeByteIdentical':(out/'baseline/dist/emu.wasm').read_bytes()==(out/'candidate/dist/emu.wasm').read_bytes(),'arms':{arm:{'runtimeSha256':hash(out/arm/'dist/emu.wasm'),'runtimeGlueSha256':hash(out/arm/'dist/emu.js'),'seedSha256':hash(out/arm/'jit/seed.wasm'),'seedBytes':(out/arm/'jit/seed.wasm').stat().st_size,'offlineSha256':hash(out/arm/'service-worker.js')} for arm in ['baseline','candidate']},'fixturePath':'.context/graphics-speed/fixed-dismiss.snap','fixtureSha256':'764486ea35ba9832f35c8118397f361026ed21119295321e0a29519509de1244'}
assert report['arms']['baseline']['runtimeSha256']=='b3237ef3843412aea164d3db111299650ea8433bf16311417116799959ab6606';assert report['arms']['candidate']['runtimeSha256']=='9220380b7a2a45b33ed965796d252af33b65ec6cae5129be34111c5d18ebcce3';assert report['arms']['baseline']['seedSha256']==report['arms']['candidate']['seedSha256']=='d9c661556ab17bb8433ed9b0c3ecfd52e2fcafe58221b4cb84a920e4976b3881';(out/'preflight.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
