const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict'),crypto=require('node:crypto');
const label=process.argv[2];assert.ok(['baseline','candidate'].includes(label));
const hash=b=>crypto.createHash('sha256').update(b).digest('hex');
(async()=>{
 const fixture=fs.readFileSync('.context/graphics-speed/fixed-dismiss.snap');
 assert.equal(hash(fixture),'764486ea35ba9832f35c8118397f361026ed21119295321e0a29519509de1244');
 const kernel=fs.readFileSync('web/firmware/kernel_1A543a.macho'),tree=fs.readFileSync('web/firmware/devicetree_m68ap.bin');
 const module=await require(path.join(__dirname,label,'emu.js'))({printErr:()=>{}});
 const call=(name,args=[])=>module.ccall(name,'number',args.map(()=> 'number'),args);
 const alloc=b=>{const p=module._malloc(b.length);assert.ok(p);module.HEAPU8.set(b,p);return p;};
 const k=alloc(kernel),d=alloc(tree);assert.equal(call('emu_init',[k,kernel.length,d,tree.length,0,0,256]),0);module._free(k);module._free(d);
 const p=alloc(fixture);assert.equal(call('emu_load_snapshot',[p,fixture.length]),0);module._free(p);
 const observations=[];
 for(let i=0;i<23;i++) {
  const start=performance.now();assert.equal(call('emu_snapshot_begin'),0);
  const elapsed=performance.now()-start;
  observations.push({index:i,warmup:i<3,captureMs:elapsed});
  call('emu_free_snapshot');
 }
 const result={label,startedAt:new Date().toISOString(),node:process.version,fixtureSha:hash(fixture),runtimeSha:hash(fs.readFileSync(path.join(__dirname,label,'emu.wasm'))),scope:'Synchronous snapshot_capture_begin only on a fixed paused phone; initialization, restore, finalization and compression excluded. First three observations retained as warmup.',observations};
 fs.writeFileSync(process.argv[3],JSON.stringify(result,null,2)+'\n');console.log(JSON.stringify({label,firstMs:observations[0].captureMs,warmMeanMs:observations.slice(3).reduce((a,x)=>a+x.captureMs,0)/20}));
})().catch(e=>{console.error(e);process.exitCode=1;});
