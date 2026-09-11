from pathlib import Path
p=Path('.context/connected-engine');s=Path('.context/core-hotspot/screen.mjs').read_text()
s=s.replace("const dir=resolve('.context/core-hotspot')","const dir=resolve('.context/connected-engine')")
s=s.replace("const seedBytes=await readFile('web/jit/seed.wasm'),seed=await WebAssembly.compile(seedBytes),defs=JSON.parse(await readFile('web/jit/regions.json')),abi=JSON.parse(await readFile('web/jit/abi.json'));","const seedBytes=await readFile('web/jit/seed.wasm'),candidateBytes=await readFile(dir+'/seed/seed.wasm'),seeds={control:await WebAssembly.compile(seedBytes),candidate:await WebAssembly.compile(candidateBytes)},defs=JSON.parse(await readFile('web/jit/regions.json')),abi=JSON.parse(await readFile('web/jit/abi.json'));\nassert.deepEqual(JSON.parse(await readFile(dir+'/seed/regions.json')),defs);const seedReport=JSON.parse(await readFile(dir+'/seed/build-report.json'));assert.equal(sha(candidateBytes),seedReport.moduleSha256);assert.equal(sha(await readFile(dir+'/generator.cjs')),seedReport.generatorSha256);")
s=s.replace("seedSha256:sha(seedBytes),runs:[]","seedSha256:sha(seedBytes),candidateSeedSha256:sha(candidateBytes),generatorSha256:seedReport.generatorSha256,started:new Date().toISOString(),runs:[]")
s=s.replace('`${dir}/${label}/emu.js`','`${dir}/runtime/emu.js`').replace('`${dir}/${label}/${p}`','`${dir}/runtime/${p}`')
s=s.replace('WebAssembly.instantiate(seed,{env:{memory:m.jitMemory,stale:m._emu_jit_stale,span:m._emu_jit_span}})','WebAssembly.instantiate(seeds[e.label],{env:{memory:m.jitMemory,stale:m._emu_jit_stale,span:m._emu_jit_span,connected_fetch:m._emu_jit_connected_fetch}})')
s=s.replace("row.cpuMs+=(delta.user+delta.system)/1000;","row.cpuMs+=(delta.user+delta.system)/1000;")
a=s.index('try{const a=await setup(')
s=s[:a]+'''const architectural=checkpoints=>checkpoints.map(({n,bytes,sha256})=>({n,bytes,sha256}));
try{const a=await setup('control'),b=await setup('candidate');
 const x=await run(a,100000000,'seeded'),y=await run(b,100000000,'seeded');assert.deepEqual(architectural(y.checkpoints),architectural(x.checkpoints),'complete real guest states');
 const wx=await run(a,400000000,'warmup'),wy=await run(b,400000000,'warmup');assert.deepEqual(architectural(wx.checkpoints),architectural(wy.checkpoints),'warmup state');
 const ref=architectural(wx.checkpoints);for(const e of [a,b,b,a]){const r=await run(e,400000000,'timed-seeded');assert.deepEqual(architectural(r.checkpoints),ref,'timed state');}
 const timed=report.runs.filter(x=>x.mode==='timed-seeded');const avg=(label,field)=>timed.filter(x=>x.label===label).reduce((s,x)=>s+x[field],0)/2;report.cpuReductionPercent=100*(1-avg('candidate','cpuMs')/avg('control','cpuMs'));report.wallReductionPercent=100*(1-avg('candidate','wallMs')/avg('control','wallMs'));report.passed=true;
}catch(e){report.error=e.stack;process.exitCode=1;}finally{report.ended=new Date().toISOString();await save();console.log(JSON.stringify({passed:report.passed,cpuReductionPercent:report.cpuReductionPercent,wallReductionPercent:report.wallReductionPercent,error:report.error}));}
'''
# Claims refer only to architecture, not derived instrumentation counters.
s=s.replace('Current-core relative state verification and Node fixed-work screening; no browser speed claim.','Same augmented core and original versus connected seed; full snapshot hashes compared, JIT counters reported but expected to differ.100M four checkpoints;400M warmup/version then400M ABBA timing. Fixed guest input, no host pacing; no browser speed claim.')
(p/'screen.mjs').write_text(s)
