// Turn census counts into tools/jit/seed-input.json.gz: per 1 KiB region the
// most frequent opcode at each position, regions ranked by the census weight
// of their compilable positions. Every workload carries equal total weight.
//   REGIONS=n (default 1024)  MAX_BYTES (default 30 MiB estimate)  KEEP_OLD=1 merges old input positions
import {readFile, writeFile, readdir} from 'node:fs/promises';
import {gunzipSync, gzipSync} from 'node:zlib';
import {fileURLToPath, pathToFileURL} from 'node:url';
import {dirname, join, resolve} from 'node:path';
import compiler from '../compiler.cjs';
const repo = resolve(dirname(fileURLToPath(import.meta.url)), '../../..');
const dir = resolve(repo, process.env.CENSUS || '.context/census');
const createEmu = (await import(pathToFileURL(resolve(repo, 'web/dist/emu.js')))).default;
const runtime = await createEmu({locateFile: p => resolve(repo, 'web/dist', p)});
compiler.configure(Array.from({length: 16}, (_, i) => runtime._emu_jit_offset(i)));
const names = (process.env.WORKLOADS ? process.env.WORKLOADS.split(',') : (await readdir(dir)).filter(f => f.endsWith('.json.gz') && f !== 'probe.json.gz').map(f => f.slice(0, -8)));
const weights = Object.fromEntries((process.env.WEIGHTS || '').split(',').filter(Boolean).map(s => s.split('=')).map(([k, v]) => [k, +v]));
const pos = new Map(); // pc|thumb -> Map(op -> weight)
for (const name of names) {
  const c = JSON.parse(gunzipSync(await readFile(join(dir, name + '.json.gz'))));
  const scale = (weights[name] ?? 1) * 1e8 / c.counted;
  for (const [key, op, count] of c.rows) {
    let ops = pos.get(key); if (!ops) pos.set(key, ops = new Map());
    ops.set(op, (ops.get(op) || 0) + count * scale);
  }
  console.error(`${name}: ${c.rows.length} positions, ${c.counted} counted`);
}
const regions = new Map(); // base|thumb -> {pc, thumb, ops: Map(pc -> op), w: Map(pc -> weight)}
for (const [key, ops] of pos) {
  const thumb = key & 1, pc = key & ~1, rk = ((pc & ~1023) | thumb) >>> 0;
  let best = 0, bw = -1, total = 0; for (const [op, w] of ops) { total += w; if (w > bw) { bw = w; best = op; } }
  let r = regions.get(rk); if (!r) regions.set(rk, r = {pc: (pc & ~1023) >>> 0, thumb: !!thumb, ops: new Map(), w: new Map()});
  r.ops.set(pc >>> 0, best); r.w.set(pc >>> 0, bw);
}
if (process.env.KEEP_OLD) {
  const old = JSON.parse(gunzipSync(await readFile(resolve(repo, process.env.KEEP_OLD))));
  for (const o of old.regions) {
    const rk = (o.pc | (o.thumb ? 1 : 0)) >>> 0;
    let r = regions.get(rk); if (!r) regions.set(rk, r = {pc: o.pc, thumb: o.thumb, ops: new Map(), w: new Map()});
    for (const [pc, op] of o.ops) if (!r.ops.has(pc)) r.ops.set(pc, op);
  }
}
const ranked = [];
for (const r of regions.values()) {
  const built = compiler.compileRegion({pc: r.pc, thumb: r.thumb, index: 0, ops: r.ops});
  if (!built) continue;
  const width = r.thumb ? 2 : 4; let score = 0;
  for (const [pc, w] of r.w) { const bit = (pc - r.pc) / width; if (built.masks[bit >>> 5] & (1 << (bit & 31))) score += w; }
  if (score > 0 || !r.w.size) ranked.push({r, score, bytes: built.wat.length});
}
ranked.sort((a, b) => b.score - a.score);
const cap = +(process.env.REGIONS || 1024), maxWat = +(process.env.MAX_WAT || Infinity);
const chosen = []; let wat = 0, covered = 0, all = 0;
for (const x of ranked) { all += x.score; if (chosen.length < cap && wat + x.bytes <= maxWat) { chosen.push(x); wat += x.bytes; covered += x.score; } }
chosen.sort((a, b) => a.r.thumb - b.r.thumb || a.r.pc - b.r.pc);
const input = {version: 1, description: `Most frequent opcodes at interpreted positions of ${chosen.length} 1 KiB regions, ranked by compilable executed weight over ${names.length} scripted workloads (tools/jit/census) from the shipped Home snapshot; derived from genuine 1A543a guest execution. No guest data or Apple binary edits.`,
  regions: chosen.map(({r}, index) => ({pc: r.pc, thumb: r.thumb, index, ops: [...r.ops].sort((a, b) => a[0] - b[0])}))};
const output = resolve(repo, process.env.OUTPUT || 'tools/jit/seed-input.json.gz');
await writeFile(output, gzipSync(JSON.stringify(input), {level: 9}));
console.log(JSON.stringify({candidates: ranked.length, chosen: chosen.length, watBytes: wat, coveredShare: +(covered / all).toFixed(4), lastScore: chosen.at(-1)?.score, output}));
