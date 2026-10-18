// Deterministic browser-style CPU throughput: the browser's 256-instruction
// active-clock batches, with a simulated host clock so every configuration
// retires identical guest work. Only interleaved ratios are meaningful.
//   SEEDS=dir[,dir...]  seed directories (default web/jit); config 0 = interpreter.
//                       `dir+rt` also compiles hot regions while running; `rt` only does that.
//   PASSES=0,1,0,1      config order; INSTRUCTIONS per pass (default 100M)
//   VCLOCK=ms           simulated host-clock step per read (default 0.01)
import createEmu from '../../web/dist/emu.js';
import {gunzipSync} from 'node:zlib';
import {fileURLToPath} from 'node:url';
import {readFile} from 'node:fs/promises';
import {basename, resolve} from 'node:path';
const repo = fileURLToPath(new URL('../../', import.meta.url));
const path = p => resolve(repo, p);
const realNow = performance.now.bind(performance);
const step = +(process.env.VCLOCK || 0.01);
let virtualMs = 0;
performance.now = () => (virtualMs += step);
const m = await createEmu({locateFile: p => path('web/dist/' + p)});
const alloc = b => { const p = m._malloc(b.length); if (!p) throw Error('allocation'); m.HEAPU8.set(b, p); return p; };
const kernel = await readFile(path('web/firmware/kernel_1A543a.macho')), tree = await readFile(path('web/firmware/devicetree_m68ap.bin'));
const k = alloc(kernel), d = alloc(tree);
if (m._emu_init(k, kernel.length, d, tree.length, 0, 0, 256)) throw Error('init');
m._free(k); m._free(d);
const manifest = JSON.parse(await readFile(path('web/manifest.json'), 'utf8'));
const parts = Array.isArray(manifest.snapshot) ? manifest.snapshot : [manifest.snapshot];
let fixture = process.env.FIXTURE_SNAPSHOT ? await readFile(resolve(process.env.FIXTURE_SNAPSHOT)) :
  Buffer.concat(await Promise.all(parts.map(p => readFile(/^https?:/.test(p) ? path('web/firmware/' + basename(new URL(p).pathname)) : path('web/' + p)))));
if (fixture[0] === 0x1f && fixture[1] === 0x8b) fixture = gunzipSync(fixture);
const s = alloc(fixture);
const dirs = (process.env.SEEDS || 'web/jit').split(',');
const seeds = await Promise.all(dirs.map(async spec => {
  const dir = spec.replace(/\+?rt$/, ''), runtime = /(^|\+)rt$/.test(spec);
  return dir ? {runtime, compiled: await WebAssembly.compile(await readFile(path(dir + '/seed.wasm'))),
    defs: JSON.parse(await readFile(path(dir + '/regions.json'), 'utf8'))} : {runtime, compiled: null, defs: []};
}));
const total = Number(process.env.INSTRUCTIONS || 100e6), mask = m._malloc(64), ids = [], res = {};
for (const config of (process.env.PASSES || '0,1,0,1,0,1,0,1').split(',').map(Number)) {
  m._emu_jit_reset(); for (const fn of ids.splice(0)) m.removeFunction(fn);
  if (m._emu_load_snapshot(s, fixture.length)) throw Error('restore');
  const seed = config ? seeds[config - 1] : null;
  const table = new WebAssembly.Table({element: 'anyfunc', initial: seed?.defs.length || 0});
  const env = {memory: m.jitMemory, stale: m._emu_jit_stale, refill: m._emu_jit_refill, link: m._emu_jit_link(), table};
  let runner = false, pumpMs = 0, compiled = 0;
  const admit = run => { const fn = m.addFunction(run, 'iiii'); ids.push(fn); if (!m._emu_jit_runner(fn)) throw Error('runner'); runner = true; };
  const install = (pc, thumb, index, masks) => {
    new Uint32Array(m.jitMemory.buffer, mask, 16).set(masks);
    if (!m._emu_jit_install(pc, thumb, index, mask)) throw Error('install');
  };
  // Compile queued hot regions between batches, as the worker's pump() does.
  const pump = () => {
    const t0 = realNow();
    while (m._emu_jit_compile_hot(16, table.length)) {
      const len = m._emu_jit_compiled(1) >>> 0;
      if (!len) continue;
      const at = m._emu_jit_compiled(0) >>> 0, n = m._emu_jit_compiled(3) >>> 0, d = m._emu_jit_compiled(2) >>> 2;
      table.grow(n);
      const inst = new WebAssembly.Instance(new WebAssembly.Module(m.HEAPU8.slice(at, at + len)), {env});
      if (!runner) admit(inst.exports.run);
      for (let i = 0; i < n; i++) { const r = m.HEAPU32.slice(d + i * 19, d + i * 19 + 19); install(r[0], r[1], r[2], r.subarray(3)); }
      compiled += n;
    }
    m._emu_jit_compiled_free();
    pumpMs += realNow() - t0;
  };
  if (seed?.compiled) {
    const instance = await WebAssembly.instantiate(seed.compiled, {env});
    admit(instance.exports.run);
    for (const r of seed.defs) install(r.pc, r.thumb ? 1 : 0, r.index, r.masks);
  }
  if (seed?.runtime) m._emu_jit_hot(1);
  if (m._emu_host_clock(1)) throw Error('clock');
  m._emu_jit_enable(config ? 1 : 0); m._emu_touch(1, 275, 250);
  let runMs = 0, done = 0;
  while (done < total) {
    const t0 = realNow(), n = m._emu_run(Math.min(50000, total - done));
    runMs += realNow() - t0;
    if (n <= 0) throw Error('run');
    if (done < 5e6 && done + n >= 5e6) m._emu_touch(0, 275, 250);
    done += n;
    if (seed?.runtime) pump();
  }
  const mips = +(done / runMs / 1e3).toFixed(1);
  (res[config] ??= []).push(mips);
  console.log(JSON.stringify({config, mips, generated: m._emu_jit_stat(0), calls: m._emu_jit_stat(1),
    ...(seed?.runtime && {compiled, pumpMs: Math.round(pumpMs)})}));
}
// The first pass of each seed includes V8 compilation and tier-up.
const median = v => { const w = [...v].sort((a, b) => a - b); return w[w.length >> 1]; };
const warm = Object.fromEntries(Object.entries(res).map(([c, v]) => [c, median(c === '0' ? v : v.slice(1))]));
const summary = {interpreter: warm[0], seeds: dirs.map((dir, i) => ({dir, warm: warm[i + 1],
  ratio: warm[0] && warm[i + 1] ? +(warm[i + 1] / warm[0]).toFixed(3) : null, passes: res[i + 1]}))};
console.log(JSON.stringify(summary));
