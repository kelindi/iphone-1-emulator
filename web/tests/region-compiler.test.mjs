import {test} from 'node:test';
import assert from 'node:assert/strict';
import {readFile, access} from 'node:fs/promises';
import {gunzipSync} from 'node:zlib';
import {dirname, join, resolve} from 'node:path';
import {fileURLToPath, pathToFileURL} from 'node:url';
import {createRequire} from 'node:module';

const repo = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
const runtimePath = join(repo, 'web/dist/emu.js');
const built = await access(runtimePath).then(() => true, () => false);

// The C region compiler must emit exactly what the reference JavaScript
// compiler emits through WABT, region by region.
test('C region compiler matches the JavaScript reference byte for byte', {skip: !built && 'web runtime not built'}, async () => {
  const createEmu = (await import(pathToFileURL(runtimePath))).default;
  const runtime = await createEmu({locateFile: p => join(dirname(runtimePath), p)});
  const require = createRequire(import.meta.url);
  const compiler = require('../../tools/jit/compiler.cjs');
  const wabt = await (await import('wabt')).default();
  const offsets = Array.from({length: 16}, (_, i) => runtime._emu_jit_offset(i));
  compiler.configure(offsets);
  const input = JSON.parse(gunzipSync(await readFile(join(repo, 'tools/jit/seed-input.json.gz'))));
  const all = input.regions.map(r => ({...r, ops: new Map(r.ops)}));
  const arm = all.filter(r => !r.thumb), thumb = all.filter(r => r.thumb);
  const sample = [...arm.slice(0, 24), ...thumb.slice(0, 24), ...all.filter((_, i) => i % 97 === 0)];
  const {wat, regions} = compiler.compilePages(sample);
  const parsed = wabt.parseWat('seed.wat', wat, {tail_call: true});
  let expected;
  try { parsed.validate(); expected = Buffer.from(parsed.toBinary({}).buffer); } finally { parsed.destroy(); }

  const words = [sample.length];
  for (const r of sample) {
    words.push(r.pc >>> 0, r.thumb ? 1 : 0, r.index, r.ops.size);
    for (const [pc, w] of r.ops) words.push(pc >>> 0, w >>> 0);
  }
  const data = Uint32Array.from(words), ptr = runtime._malloc(data.byteLength);
  try {
    runtime.HEAPU32.set(data, ptr >>> 2);
    assert.equal(runtime._emu_jit_compile(ptr, data.length), 1);
    const at = runtime._emu_jit_compiled(0) >>> 0, len = runtime._emu_jit_compiled(1) >>> 0;
    assert.ok(Buffer.from(runtime.HEAPU8.subarray(at, at + len)).equals(expected), 'module bytes differ');
    const defs = runtime._emu_jit_compiled(2) >>> 2;
    assert.equal(runtime._emu_jit_compiled(3), regions.length);
    regions.forEach((r, i) => {
      const d = runtime.HEAPU32.subarray(defs + i * 19, defs + i * 19 + 19);
      assert.deepEqual({pc: d[0], thumb: !!d[1], index: d[2], masks: Array.from(d.subarray(3))}, r);
    });
    // Malformed input is rejected rather than read past its end.
    assert.equal(runtime._emu_jit_compile(ptr, 3), 0);
  } finally {
    runtime._emu_jit_compiled_free();
    runtime._free(ptr);
  }
});
