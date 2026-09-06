from pathlib import Path
p=Path('.context/jit-mixed-coverage');s=Path('.context/jit-fallback-census/replay.mjs').read_text()
s=s.replace("resolve('.context/jit-fallback-census')","resolve('.context/jit-mixed-coverage')").replace("['control','observer']","['original','candidate']").replace("kind+'/emu.js'","kind+'/dist/emu.js'").replace("kind+'/emu.wasm'","kind+'/dist/emu.wasm'")
s=s.replace("'web/jit/seed.wasm'","base+'/'+kind+'/seed.wasm'").replace("'web/jit/regions.json'","base+'/'+kind+'/regions.json'").replace("'web/jit/abi.json'","base+'/'+kind+'/abi.json'")
s=s.replace('m._emu_jit_stat(2),512','m._emu_jit_stat(2),defs.length')
s=s.replace('report.checkpoints.push({done,rawSha256:',"await fs.writeFile(base+'/'+kind+'-raw-'+done+'.snap.gz',gzipSync(bytes));report.checkpoints.push({done,rawSha256:")
s=s.replace('report.passed=true;',"const control=JSON.parse(await fs.readFile('.context/jit-fallback-census/control-100m.json'));assert.equal(report.fixtureSha256,control.fixtureSha256);assert.deepEqual(report.checkpoints.map(({done,rawSha256,bytes})=>({done,rawSha256,bytes})),control.checkpoints.map(({done,rawSha256,bytes})=>({done,rawSha256,bytes})));assert.deepEqual(report.frame,control.frame);report.controlEvidence='.context/jit-fallback-census/control-100m.json';report.counterPolicy='Generated retirement and calls may increase with added admission; full raw CPU/TLB/device snapshot bytes must remain exact.';report.passed=true;")
(p/'replay.mjs').write_text(s)
