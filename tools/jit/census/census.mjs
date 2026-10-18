// Interpreter execution census over scripted phone workloads from the shipped
// Home snapshot, with the browser's active-clock batches and a simulated host
// clock (as in bench.mjs). Writes per-workload (pc, ISA, opcode) counts.
//   WORKLOADS=name,...  subset of workloads.mjs; OUT=dir (default .context/census)
//   SHOTS=1             also write a PNG at every `shot` event
import {gunzipSync, gzipSync, deflateSync} from 'node:zlib';
import {fileURLToPath} from 'node:url';
import {readFile, writeFile, mkdir} from 'node:fs/promises';
import {basename, resolve} from 'node:path';
import {createRequire} from 'node:module';
import {workloads} from './workloads.mjs';
const repo = fileURLToPath(new URL('../../../', import.meta.url));
const path = p => resolve(repo, p);
const out = path(process.env.OUT || '.context/census');
await mkdir(out, {recursive: true});
let virtualMs = 0;
performance.now = () => (virtualMs += 0.01);
const createCensus = createRequire(import.meta.url)(resolve(out, 'census.cjs'));
const m = await createCensus({locateFile: p => resolve(out, p)});
const alloc = b => { const p = m._malloc(b.length); if (!p) throw Error('allocation'); m.HEAPU8.set(b, p); return p; };
const kernel = await readFile(path('web/firmware/kernel_1A543a.macho')), tree = await readFile(path('web/firmware/devicetree_m68ap.bin'));
const k = alloc(kernel), d = alloc(tree);
if (m._emu_init(k, kernel.length, d, tree.length, 0, 0, 256)) throw Error('init');
const manifest = JSON.parse(await readFile(path('web/manifest.json'), 'utf8'));
const parts = Array.isArray(manifest.snapshot) ? manifest.snapshot : [manifest.snapshot];
let fixture = Buffer.concat(await Promise.all(parts.map(p => readFile(/^https?:/.test(p) ? path('web/firmware/' + basename(new URL(p).pathname)) : path('web/' + p)))));
if (fixture[0] === 0x1f && fixture[1] === 0x8b) fixture = gunzipSync(fixture);
const s = alloc(fixture), frame = m._malloc(320 * 480 * 4), dims = m._malloc(8);

const crcTable = Array.from({length: 256}, (_, n) => { for (let k = 0; k < 8; k++) n = n & 1 ? 0xedb88320 ^ (n >>> 1) : n >>> 1; return n >>> 0; });
const crc = b => { let c = ~0; for (const x of b) c = crcTable[(c ^ x) & 255] ^ (c >>> 8); return ~c >>> 0; };
const chunk = (type, data) => { const len = Buffer.alloc(4), t = Buffer.concat([Buffer.from(type), data]), c = Buffer.alloc(4);
  len.writeUInt32BE(data.length); c.writeUInt32BE(crc(t)); return Buffer.concat([len, t, c]); };
async function shot(name) {
  if (m._emu_frame(frame, dims, dims + 4) !== 1) return console.log(`${name}: display off`);
  const [w, h] = m.HEAPU32.subarray(dims >>> 2, (dims >>> 2) + 2), rgba = m.HEAPU8.subarray(frame, frame + w * h * 4);
  const raw = Buffer.alloc((w * 4 + 1) * h);
  for (let y = 0; y < h; y++) raw.set(rgba.subarray(y * w * 4, (y + 1) * w * 4), y * (w * 4 + 1) + 1);
  const ihdr = Buffer.alloc(13); ihdr.writeUInt32BE(w); ihdr.writeUInt32BE(h, 4); ihdr.set([8, 6, 0, 0, 0], 8);
  await writeFile(`${out}/${name}.png`, Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]),
    chunk('IHDR', ihdr), chunk('IDAT', deflateSync(raw)), chunk('IEND', Buffer.alloc(0))]));
}

const wanted = process.env.WORKLOADS ? process.env.WORKLOADS.split(',') : Object.keys(workloads);
for (const name of wanted) {
  const w = workloads[name]; if (!w) throw Error('Unknown workload ' + name);
  const events = [...w.events].sort((a, b) => a[0] - b[0]);
  if (m._emu_load_snapshot(s, fixture.length)) throw Error('restore');
  if (m._emu_host_clock(1)) throw Error('clock');
  m._census_clear(); m._census_enable(1);
  let done = 0, e = 0, shotN = 0;
  const t0 = Date.now();
  while (done < w.instructions) {
    while (e < events.length && events[e][0] <= done) {
      const [, type, ...a] = events[e++];
      if (type === 'touch') m._emu_touch(...a);
      else if (type === 'button') m._emu_button(...a);
      else if (type === 'shot' && process.env.SHOTS) await shot(`${name}-${shotN++}`);
    }
    const next = Math.min(w.instructions, e < events.length ? events[e][0] : Infinity);
    const n = m._emu_run(Math.min(50000, next - done));
    if (n <= 0) throw Error('run');
    done += n;
  }
  m._census_enable(0);
  if (process.env.SHOTS) await shot(`${name}-end`);
  if (m._census_overflow()) throw Error('census table overflow');
  const base = m._census_table() >>> 2, slots = m._census_slots(), u32 = m.HEAPU32, f64 = m.HEAPF64, rows = [];
  for (let i = 0; i < slots; i++) { const c = f64[(base >>> 1) + i * 2 + 1]; if (c) rows.push([u32[base + i * 4], u32[base + i * 4 + 1], c]); }
  const total = rows.reduce((a, r) => a + r[2], 0);
  await writeFile(`${out}/${name}.json.gz`, gzipSync(JSON.stringify({name, instructions: done, counted: total, rows})));
  console.log(JSON.stringify({name, instructions: done, counted: total, distinct: rows.length, seconds: (Date.now() - t0) / 1e3}));
}
