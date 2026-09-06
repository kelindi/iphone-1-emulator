const fs=require('node:fs');
const path=require('node:path');
const assert=require('node:assert/strict');
const crypto=require('node:crypto');
const root=process.cwd(),local=__dirname;
const hash=b=>crypto.createHash('sha256').update(b).digest('hex');
const kernel=fs.readFileSync('web/firmware/kernel_1A543a.macho');
const tree=fs.readFileSync('web/firmware/devicetree_m68ap.bin');
const cases=[['mbx','.context/graphics-speed/fixed-dismiss.snap','764486ea35ba9832f35c8118397f361026ed21119295321e0a29519509de1244'],['software','.context/lockdown-root/usb-ready-home.snap','28591a3003b8677469b2e506185792e26ac128ba9383b7f110959f4f4fb0c82d']];
async function initialize(label) {
 const module=await require(path.join(local,label,'emu.js'))({printErr:()=>{}});
 const call=(name,args=[])=>module.ccall(name,'number',args.map(()=> 'number'),args);
 const alloc=bytes=>{const ptr=module._malloc(bytes.length);assert.ok(ptr);module.HEAPU8.set(bytes,ptr);return ptr;};
 const k=alloc(kernel),d=alloc(tree);
 assert.equal(call('emu_init',[k,kernel.length,d,tree.length,0,0,256]),0);module._free(k);module._free(d);
 return {module,call,alloc};
}
(async()=> {
 const report={scope:'Correctness under concurrent work, not performance acceptance',results:[]};
 const baseline=await initialize('baseline'),candidate=await initialize('candidate');
 for(const [name,file,expectedHash] of cases) {
  const fixture=fs.readFileSync(file);assert.equal(hash(fixture),expectedHash);
  for(const api of [baseline,candidate]) {
   const ptr=api.alloc(fixture);assert.equal(api.call('emu_load_snapshot',[ptr,fixture.length]),0);api.module._free(ptr);
  }
  for(let trial=0;trial<2;trial++) {
   const bp=baseline.call('emu_save_snapshot');assert.ok(bp);
   const size=baseline.call('emu_snapshot_size');
   const expected=Buffer.from(baseline.module.HEAPU8.subarray(bp,bp+size));
   baseline.call('emu_free_snapshot');
   const cp=candidate.call('emu_save_snapshot');assert.ok(cp);
   assert.equal(candidate.call('emu_snapshot_size'),size);
   assert.ok(expected.equals(candidate.module.HEAPU8.subarray(cp,cp+size)));
   candidate.call('emu_free_snapshot');
   assert.equal(candidate.call('emu_snapshot_begin'),0);
   assert.equal(candidate.call('emu_snapshot_data'),0);
   assert.equal(candidate.call('emu_snapshot_advance',[0]),0);
   let chunks=0,retired=0;
   while(!candidate.call('emu_snapshot_advance',[1048576])) {
    chunks++;
    const a=baseline.call('emu_run',[50000]),b=candidate.call('emu_run',[50000]);
    assert.equal(a,b);retired+=a;
   }
   const captured=candidate.call('emu_snapshot_data');assert.ok(captured);
   assert.equal(candidate.call('emu_snapshot_size'),size);
   assert.ok(expected.equals(candidate.module.HEAPU8.subarray(captured,captured+size)));
   assert.ok(retired>0);candidate.call('emu_free_snapshot');
   const result={name,trial,size,sha256:hash(expected),blockingExact:true,captureExact:true,chunks,retiredDuringFinalize:retired};
   report.results.push(result);console.log(JSON.stringify(result));
  }
 }
 report.passed=true;fs.writeFileSync(path.join(local,'fixture-verification.json'),JSON.stringify(report,null,2)+'\n');
})().catch(e=>{console.error(e);process.exitCode=1;});
