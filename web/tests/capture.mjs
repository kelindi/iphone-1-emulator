// Compare the original blocking serializer with incremental v39 finalization
// on the genuine shipped phone, while guest instructions run between chunks.
import assert from 'node:assert/strict';
import {readFile, writeFile, mkdir} from 'node:fs/promises';
import {gunzipSync} from 'node:zlib';
import createEmu from '../dist/emu.js';

const root = new URL('../', import.meta.url);
const manifest = JSON.parse(await readFile(new URL('manifest.json',root)));
const localAsset = path => new URL(`firmware/${new URL(path,root).pathname.split('/').pop()}`,root);
const kernel = await readFile(localAsset(manifest.kernel));
const tree = await readFile(localAsset(manifest.devicetree));
const snapshot = gunzipSync(Buffer.concat(await Promise.all([manifest.snapshot].flat().map(path=>readFile(localAsset(path))))));
const module = await createEmu({printErr:()=>{}});
const api = (name,args=[],values=[])=>module.ccall(name,'number',args,values);
const allocate = bytes => { const ptr=module._malloc(bytes.length); assert.ok(ptr); module.HEAPU8.set(bytes,ptr); return ptr; };
const k=allocate(kernel),d=allocate(tree);
assert.equal(api('emu_init',Array(7).fill('number'),[k,kernel.length,d,tree.length,0,0,256]),0);
module._free(k);module._free(d);
const s=allocate(snapshot);
assert.equal(api('emu_load_snapshot',['number','number'],[s,snapshot.length]),0);
module._free(s);
const results=[];
for(let repeat=0;repeat<3;repeat++) {
  const began=performance.now();
  const reference=api('emu_save_snapshot'); assert.ok(reference);
  const blockingMs=performance.now()-began;
  const expected=Buffer.from(module.HEAPU8.subarray(reference,reference+api('emu_snapshot_size')));
  api('emu_free_snapshot');
  const captureBegan=performance.now();
  assert.equal(api('emu_snapshot_begin'),0);
  const captureMs=performance.now()-captureBegan;
  assert.equal(api('emu_snapshot_data'),0,'incomplete capture must remain private');
  assert.equal(api('emu_snapshot_advance',['number'],[0]),0);
  let chunks=0,maxChunkMs=0,ran=0;
  for(;;) {
    const beganChunk=performance.now();
    const done=api('emu_snapshot_advance',['number'],[1048576]);
    maxChunkMs=Math.max(maxChunkMs,performance.now()-beganChunk);
    assert.ok(done===0||done===1); chunks++;
    if(done)break;
    ran+=api('emu_run',['number'],[50000]);
  }
  assert.ok(ran>0,'guest must run during checksum finalization');
  const ptr=api('emu_snapshot_data'), length=api('emu_snapshot_size');
  assert.ok(ptr);
  assert.equal(length,expected.length);
  assert.ok(expected.equals(Buffer.from(module.HEAPU8.subarray(ptr,ptr+length))),
    'incremental bytes must equal blocking snapshot despite guest execution');
  results.push({repeat:repeat+1,blockingMs,captureMs,maxChunkMs,chunks,guestInstructionsDuringFinalize:ran,bytes:length,identical:true});
  api('emu_free_snapshot');
}
const dir=process.env.ARTIFACT_DIR||'.context/capture-report';await mkdir(dir,{recursive:true});
await writeFile(`${dir}/report.json`,JSON.stringify({result:'pass',runtime:process.version,results},null,2)+'\n');
console.log(JSON.stringify(results,null,2));
