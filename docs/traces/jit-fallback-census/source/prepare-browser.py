from pathlib import Path
import shutil,hashlib,json
p=Path('.context/jit-fallback-census');site=p/'site';site.mkdir(exist_ok=True)
for name in ['index.html','app.js','phone.css','emu-worker.js','seed-jit.js','storage.js','checkpoint-worker.js','manifest.json']:
 shutil.copy2(p/'frozen-web'/name,site/name)
for name in ['assets.js','offline.js']:
 if Path('web',name).exists():shutil.copy2(Path('web',name),site/name)
for d in ['images','icons']:
 if Path('web',d).exists():shutil.copytree(Path('web',d),site/d,dirs_exist_ok=True)
for d in ['jit','dist']:(site/d).mkdir(exist_ok=True)
for name in ['seed.wasm','regions.json','abi.json']:shutil.copy2(Path('web/jit',name),site/'jit'/name)
for name in ['emu.js','emu.wasm']:shutil.copy2(p/'observer'/name,site/'dist'/name)
original=(site/'emu-worker.js').read_text()
hook='''      case 'fc': {
        if (message.operation === 'start') module._emu_fc_start();
        if (message.operation === 'stop') module._emu_fc_stop();
        const view = new DataView(module.jitMemory.buffer), ptr = module._emu_fc_summary();
        const keys = ['calls','ok','fetched','nofetch','nonok','overflow','mapOverflow','epoch','installs'];
        const census = Object.fromEntries(keys.map((key, i) => [key, Number(view.getBigUint64(ptr + i*8, true))]));
        census.rows = view.getUint32(ptr+72,true); census.enabled=view.getUint32(ptr+76,true);
        let buffer;
        if (message.operation === 'stop') buffer = module.HEAPU8.slice(module._emu_fc_rows(), module._emu_fc_rows()+module._emu_fc_capacity()*40).buffer;
        send('fc', {id:message.id, retired:diagnosticRetired, cycles:String(api.cycles()), jit:jit?.stats(), at:performance.now(), census, buffer}, buffer?[buffer]:[]);
        return;
      }
'''
s='let diagnosticRetired = 0;\n'+original
old='      const ran = api.run(50000);';assert s.count(old)==1;s=s.replace(old,old+' diagnosticRetired += ran;')
old='    switch (message.type) {\n';assert s.count(old)==1;s=s.replace(old,old+hook)
restored=s.replace(hook,'').replace(' diagnosticRetired += ran;','').removeprefix('let diagnosticRetired = 0;\n');assert restored==original
(site/'emu-worker.js').write_text(s)
for name in ['frame-packet.mjs','frames.mjs','cache-server.mjs']:shutil.copy2(Path('.context/jit-sw-cache-v4',name),p/name)
ref=p/'references';ref.mkdir(exist_ok=True)
for name in ['C0-settings.rgba','C0-home.rgba']:shutil.copy2(Path('.context/jit-return-visit-v3/references',name),ref/name)
sha=lambda x:hashlib.sha256(x.read_bytes()).hexdigest()
report=dict(origin='http://127.0.0.1:8211',port=8211,debugPort=9251,fixture='.context/graphics-speed/fixed-dismiss.snap',fixtureSha256=sha(Path('.context/graphics-speed/fixed-dismiss.snap')),expectedPausedImportActive=512,definitionCount=512,ordinaryStartupExpected=511,capacity=524288,maxProbes=64,site={str(f.relative_to(site)):sha(f) for f in sorted(site.rglob('*')) if f.is_file()},originalWorkerSha256=hashlib.sha256(original.encode()).hexdigest(),workerInverseProjectionByteExact=True,uiScope='Frozen just-verified RTC/loader release UI before independent visible-recovery edit; no app/index/CSS changes by census',references={f.name:sha(f) for f in ref.iterdir()})
(p/'browser-pins.json').write_text(json.dumps(report,indent=2)+'\n')
