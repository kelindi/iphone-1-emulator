from pathlib import Path
p=Path('.context/connected-engine');s=Path('.context/jit-continuation/test-continuation-frozen.cjs').read_text()
s=s.replace("root='.context/jit-continuation/';","root='.context/connected-engine/';\nconst connected=require('./generator.cjs');")
a=s.index(' async function program(regions){');b=s.index('\n function state()',a)
s=s[:a]+''' async function program(regions){
  for(const id of ids.flat())m.removeFunction(id);ids=[[],[]];built=[];
  instructions=regions.flatMap(r=>r.ops.map(([pc,w])=>({pc,w,thumb:r.thumb})));mode=regions[0].thumb;
  const originals=[];
  for(const [index,r]of regions.entries()){const b=compiler.compileRegion({...r,index,ops:new Map(r.ops)});assert.equal(b.count,r.supportedCount??r.ops.length);built.push({...b,...r});originals.push(b.wat);}
  const generated=connected.generate(regions.map((r,index)=>({...r,index})),offsets,{clusters:[regions.map((_,i)=>i)]});
  const originalWat='(module(import "env" "memory"(memory 8192))(import "env" "stale"(func $stale(param i32 i32)))'+originals.join('')+')';
  for(const [arm,wat] of [originalWat,generated.wat].entries()){
   const parsed=wabt.parseWat('connected-test.wat',wat);parsed.validate();const bytes=parsed.toBinary({}).buffer;parsed.destroy();
   const {instance}=await WebAssembly.instantiate(bytes,{env:{memory:m.jitMemory,stale:m._emu_jit_stale,span:m._emu_jit_span,connected_fetch:m._emu_jit_connected_fetch}});
   for(let i=0;i<regions.length;i++)ids[arm].push(m.addFunction(instance.exports['b'+i],'iii'));
   assert.equal(new Set(ids[arm]).size,regions.length,'Each original region owns a distinct table slot');
  }
 }
'''+s[b:]
s=s.replace('ids[i],mask','ids[engine-1][i],mask')
s=s.replace('assert.equal(staleTarget[2].calls,2);','assert.equal(staleTarget[2].calls,1);')
a=s.index(' await program([{pc:0,thumb:false,retireAfter:true');b=s.index(' await program([{pc:0,thumb:false,ops:',a+1)
s=s[:a]+" // Old continuation-only retire-after-C-return injection is replaced by\n // distinct table slot and actual stale-target lifetime coverage here.\n"+s[b:]
s=s.replace('for(const id of ids)m.removeFunction(id);','for(const id of ids.flat())m.removeFunction(id);')
s=s.replace('Actual original compiler functions with interpreter/original-runtime/candidate-runtime:','Original compiler versus connected-region compiler under the same current-core oracle:')
(p/'test-focused.cjs').write_text(s)
