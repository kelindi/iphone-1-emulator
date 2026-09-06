from pathlib import Path
import shutil,json,hashlib
b=Path('.context/jit-executed-work');old=Path('.context/jit-fallback-census');out=b/'warm-browser';assert not out.exists();out.mkdir();site=out/'site';shutil.copytree(old/'site',site)
original=(old/'frozen-web/emu-worker.js').read_text()
hook='''      case 'fc': {
        if (!['start','stop','read'].includes(message.operation)) throw Error('Invalid census operation');
        if (message.operation === 'start') { module._emu_fc_start(); diagnosticWindowStart = diagnosticRetired; }
        if (message.operation === 'stop') module._emu_fc_stop();
        const view = new DataView(module.jitMemory.buffer), ptr = module._emu_fc_summary();
        const keys = ['calls','ok','fetched','nofetch','nonok','overflow','mapOverflow','epoch','installs'];
        const census = Object.fromEntries(keys.map((key, i) => [key, Number(view.getBigUint64(ptr + i*8, true))]));
        census.rows=view.getUint32(ptr+72,true); census.enabled=view.getUint32(ptr+76,true);
        census.generatedOverflow=String(view.getBigUint64(module._emu_fc_generated_overflow(),true));
        const parts = message.operation !== 'stop' ? [] : [
          ['rows',module._emu_fc_rows(),module._emu_fc_capacity()*40],
          ['generated',module._emu_fc_generated(),module._emu_fc_generated_capacity()*8],
          ['summary',ptr,80]
        ].map(([name,p,size])=>({name,buffer:module.HEAPU8.slice(p,p+size).buffer}));
        send('fc', {id:message.id, retired:diagnosticRetired, cycles:String(api.cycles()), guestStatus:api.status(), ticks:String(api.ticks()), jit:jit?.stats(), census, parts,
          binding:{base:module._emu_fc_generated(),enabled:module._emu_fc_enabled_pointer(),overflow:module._emu_fc_generated_overflow()}},parts.map(p=>p.buffer));
        return;
      }
'''
watch=''' diagnosticRetired += ran;
      const censusView = new DataView(module.jitMemory.buffer), censusPtr = module._emu_fc_summary();
      if (censusView.getUint32(censusPtr+76,true) && (
          censusView.getBigUint64(censusPtr+40,true) || censusView.getBigUint64(censusPtr+48,true) ||
          censusView.getBigUint64(censusPtr+64,true) || censusView.getBigUint64(module._emu_fc_generated_overflow(),true) ||
          diagnosticRetired-diagnosticWindowStart >= 150000000)) {
        module._emu_fc_stop(); throw Error('Count-only observation loss or150M window cap; stopped before next chunk');
      }'''
prefix='let diagnosticRetired = 0, diagnosticWindowStart = 0;\n';source=prefix+original
source=source.replace('      const ran = api.run(50000);','      const ran = api.run(50000);'+watch).replace('    switch (message.type) {\n','    switch (message.type) {\n'+hook)
assert source.replace(watch,'').replace(hook,'').removeprefix(prefix)==original
(site/'emu-worker.js').write_text(source);(out/'worker-hook.txt').write_text(hook);(out/'worker-watch.txt').write_text(watch)
for n in ['emu.js','emu.wasm']:shutil.copyfile(b/'observer'/n,site/'dist'/n)
shutil.copyfile(b/'seed.wasm',site/'jit/seed.wasm');abi=json.loads((site/'jit/abi.json').read_text());abi['moduleSha256']=hashlib.sha256((b/'seed.wasm').read_bytes()).hexdigest();(site/'jit/abi.json').write_text(json.dumps(abi,indent=2)+'\n')
for n in ['frame-packet.mjs','frames.mjs','cache-server.mjs']:shutil.copyfile(old/n,out/n)
shutil.copytree(old/'references',out/'references')
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();pins=json.loads((old/'browser-pins.json').read_text());pins.update(origin='http://127.0.0.1:8234',port=8234,debugPort=9266,generatedCapacity=512*512,windowInstructionCap=150000000,windowWallCapSeconds=15,outerWallCapSeconds=240,uiScope='Exact prior frozen fallback-census UI, before085 visible recovery; current production UI untouched',seedBinding=json.loads((b/'seed-report.json').read_text())['config'],site={str(p.relative_to(site)):sha(p)for p in site.rglob('*')if p.is_file()})
(out/'pins.json').write_text(json.dumps(pins,indent=2)+'\n');(out/'source-projection.json').write_text(json.dumps({'passed':True,'originalWorkerSha256':sha(old/'frozen-web/emu-worker.js'),'inverseExact':True,'originalSeedLoaderUnchanged':sha(site/'seed-jit.js')==sha(old/'site/seed-jit.js'),'originalRegionsExact':sha(site/'jit/regions.json')==sha(old/'site/jit/regions.json'),'abiOnlyObserverModuleHashChanged':{k:v for k,v in abi.items()if k!='moduleSha256'}=={k:v for k,v in json.loads((old/'site/jit/abi.json').read_text()).items()if k!='moduleSha256'},'scope':'Source/site preparation only; no browser launched'},indent=2)+'\n')
