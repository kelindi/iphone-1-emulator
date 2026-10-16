// Whole-machine equivalence for a prepared iPhone OS 3 firmware and its own
// seed: the interpreter and generated code must leave identical machine
// snapshots AND identical root volumes after the same input script.
//   FW_DIR=web/firmware/7E18 INSTRUCTIONS=600000000 node tools/jit/replay-ios3.mjs
import createEmu from '../../web/dist/emu.js';
import {fileURLToPath} from 'node:url';
import {readFile} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import {resolve, join} from 'node:path';
const repo = fileURLToPath(new URL('../../', import.meta.url));
const fw = resolve(repo, process.env.FW_DIR || 'web/firmware/7E18');
const manifest = JSON.parse(await readFile(join(fw, 'manifest.json'), 'utf8'));
if (manifest.kind !== 'ios3' || !manifest.snapshot) throw Error('Needs a prepared ios3 firmware with a snapshot');
const m = await createEmu({locateFile: p => resolve(repo, 'web/dist', p)});
const alloc = b => { const p = m._malloc(b.length); if (!p) throw Error('allocation'); m.HEAPU8.set(b, p); return p; };
const [kernel, tree, disk, fixture] = await Promise.all(
  [manifest.kernel, manifest.devicetree, manifest.disk, manifest.snapshot].map(e => readFile(join(fw, e.path))));
const k = alloc(kernel), d = alloc(tree), diskPtr = alloc(disk);
if (m._emu_init_ios3(k, kernel.length, d, tree.length, diskPtr, disk.length))
  throw Error('init: ' + m.UTF8ToString?.(m._emu_error()));
m._free(k); m._free(d);
const s = alloc(fixture);
const compiled = await WebAssembly.compile(await readFile(join(fw, 'jit/seed.wasm')));
const defs = JSON.parse(await readFile(join(fw, 'jit/regions.json'), 'utf8'));
const total = Number(process.env.INSTRUCTIONS || 600e6);
if (!Number.isSafeInteger(total) || total < 1 || total > 0xffffffff) throw Error('INSTRUCTIONS must be 1..4294967295');
const sha = bytes => createHash('sha256').update(bytes).digest('hex');
console.log(JSON.stringify({firmware: manifest.name, snapshotSha256: sha(fixture), instructions: total, regions: defs.length}));
// Input script relative to the snapshot: open Calculator, go Home, open Settings.
const script = new Map([[0, () => m._emu_touch(1, 268, 248)], [5e6, () => m._emu_touch(0, 268, 248)],
  [300e6, () => m._emu_button(1, 1)], [305e6, () => m._emu_button(1, 0)],
  [450e6, () => m._emu_touch(1, 40, 330)], [455e6, () => m._emu_touch(0, 40, 330)]]);
const expected = [], ids = [], mask = m._malloc(64);
const timings = {};
let dumps = 0;
const repeat = Number(process.env.REPEAT || 1);  // timing reps per mode; checks still compare every rep
const modes = (process.env.CONTROL ? [0, 0] : [0, 1]).flatMap(v => Array(repeat).fill(v));
const repTimes = [];
for (const enabled of modes) {
  m._emu_jit_reset(); for (const fn of ids.splice(0)) m.removeFunction(fn);
  m.HEAPU8.set(disk, diskPtr);   // every run starts from the shipped volume
  if (m._emu_load_snapshot(s, fixture.length)) throw Error('restore');
  if (enabled && process.env.SEED_MODE !== 'noinstall') {
    const instance = await WebAssembly.instantiate(compiled, {env: {memory: m.jitMemory, stale: m._emu_jit_stale, span: m._emu_jit_span}});
    for (const r of defs) {
      const fn = m.addFunction(instance.exports['b' + r.index], 'iii'); ids.push(fn);
      new Uint32Array(m.jitMemory.buffer, mask, 16).set(r.masks);
      if (!m._emu_jit_install(r.pc, r.thumb ? 1 : 0, fn, mask)) throw Error('install');
    }
  }
  m._emu_jit_enable(process.env.SEED_MODE === 'noenable' ? 0 : enabled);
  const start = performance.now();
  for (let done = 0; done < total;) {
    script.get(done)?.();
    const next = [...script.keys(), total].filter(t => t > done).reduce((a, b) => Math.min(a, b), done + 100e6);
    const count = Math.min(1e6, next - done);
    if (m._emu_run(count) !== count) throw Error('run');
    done += count;
    if (done % 100e6 === 0 || done === total) {
      const p = m._emu_save_snapshot(), length = m._emu_snapshot_size();
      if (!p || !length) throw Error('snapshot');
      // tlb_hits/misses/flushes end the CPU section, right before the MACH
      // tag; snapshot.c calls them measurements of the run, not machine state
      // (installing a seed flushes the TLB once more). The trailing 8 bytes
      // checksum the stream. Everything else must match.
      const snap = Buffer.from(m.HEAPU8.subarray(p, p + length));
      const mach = snap.indexOf('MACH', 40);
      if (mach < 24) throw Error('snapshot layout');
      snap.fill(0, mach - 24, mach); snap.fill(0, snap.length - 8);
      const hash = sha(snap) + '/' + sha(m.HEAPU8.subarray(diskPtr, diskPtr + disk.length));
      if (process.env.DUMP_DIR) (await import('node:fs')).writeFileSync(`${process.env.DUMP_DIR}/run${dumps++}_${done}.snap`, m.HEAPU8.subarray(p, p + length));
      m._emu_free_snapshot();
      console.log(JSON.stringify({enabled, done, hash: hash.slice(0, 12) + '|' + hash.slice(65, 77), generated: m._emu_jit_stat(0), calls: m._emu_jit_stat(1), ms: Math.round(performance.now() - start)}));
      const checkpoint = Math.ceil(done / 100e6) - 1;
      if (expected.length <= checkpoint) expected.push(hash);
      else if (hash !== expected[checkpoint]) throw Error(`Machine or disk mismatch at ${done}`);
    }
  }
  timings[enabled ? 'seed' : 'interpreter'] = performance.now() - start;
  repTimes.push(`${enabled ? 'seed' : 'interp'} ${Math.round(performance.now() - start)} ms`);
  if (enabled && m._emu_jit_stat(0) <= 0) throw Error('Generated code never retired an instruction');
  if (!enabled && m._emu_jit_stat(0) !== 0) throw Error('Interpreter control retired generated instructions');
}
m._emu_jit_reset(); for (const fn of ids) m.removeFunction(fn); m._free(mask); m._free(s);
console.log('reps:', repTimes.join(', '));
console.log(`Full machine and disk equivalence passed; interpreter ${(timings.interpreter / 1000).toFixed(1)} s, ` +
  `seed ${(timings.seed / 1000).toFixed(1)} s (${(timings.interpreter / timings.seed).toFixed(2)}x)`);
