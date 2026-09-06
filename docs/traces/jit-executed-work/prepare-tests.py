from pathlib import Path
p=Path('.context/jit-executed-work');s=Path('.context/jit-fallback-census/test-boundaries.cjs').read_text()
s=s.replace("compiler.configure(offsets);", "compiler.configure(offsets);if(compiler.configureCensus)compiler.configureCensus({base:m._emu_fc_generated(),enabled:m._emu_fc_enabled_pointer(),overflow:m._emu_fc_generated_overflow()});")
s=s.replace('let reference;const rows=[];', 'let reference,referenceWork;const rows=[];')
s=s.replace('let observer=null;', 'let observer=null;')
needle="assert.equal(observer.installs,0);if(!tryOnly)assert.equal(ran,observer.ok+m._emu_jit_stat(0));"
extra='''assert.equal(observer.installs,0);if(!tryOnly)assert.equal(ran,observer.ok+m._emu_jit_stat(0));
    const work=new Map(),credit=(pc,opcode,flags,status,count)=>{const key=[pc,opcode,flags,status].join(':');work.set(key,(work.get(key)||0n)+count);};
    for(let i=0;i<m._emu_fc_capacity();i++){const ptr=base+i*40;if(v.getUint32(ptr+28,true))credit(v.getUint32(ptr,true),v.getUint32(ptr+4,true),v.getUint32(ptr+8,true),v.getUint32(ptr+24,true),v.getBigUint64(ptr+32,true));}
    let generatedSum=0n;const gen=m._emu_fc_generated(),meta=new Map();for(const [index,b]of built.entries())for(const [pc,opcode]of b.ops){const offset=(pc-b.pc)/(b.thumb?2:4);if((b.masks[offset>>>5]>>>(offset&31))&1)meta.set(index*512+offset,{pc,opcode,flags:1|(b.thumb?256:0)});}
    for(let i=0;i<m._emu_fc_generated_capacity();i++){const count=v.getBigUint64(gen+i*8,true);if(!count)continue;assert(meta.has(i),'Count in unsupported/unmapped generated slot');const x=meta.get(i);generatedSum+=count;credit(x.pc,x.opcode,x.flags,0,count);}
    observer.generatedSum=Number(generatedSum);observer.generatedOverflow=Number(v.getBigUint64(m._emu_fc_generated_overflow(),true));assert.equal(observer.generatedOverflow,0);assert.equal(observer.generatedSum,m._emu_jit_stat(0),'Generated per-PC counts must conserve original retirement');
    const sorted=[...work].sort((a,b)=>a[0].localeCompare(b[0])).map(([key,count])=>({key,count:String(count)}));observer.work=sorted;observer.workSha256=crypto.createHash('sha256').update(JSON.stringify(sorted)).digest('hex');
    if(!engine)referenceWork=sorted;else assert.deepEqual(sorted,referenceWork,'Per-PC/opcode completed-work footprint must match actual interpreter');'''
assert s.count(needle)==1;s=s.replace(needle,extra)
s=s.replace("const report={passed:true,cases,", "const report={passed:true,cases,executedWorkSemantics:'Per-PC/opcode/mode/status/event footprint from actual interpreter and each generated n++ conserves exact retired counts; no physical mapping or side-effect claim.',")
(p/'test-boundaries.cjs').write_text(s)
