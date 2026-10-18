#!/usr/bin/env node
/** Regenerate the guarded seed from retained opcode positions and the live C ABI. */
import {readFile, writeFile, mkdir} from 'node:fs/promises';
import {gunzipSync} from 'node:zlib';
import {createHash} from 'node:crypto';
import {dirname, resolve, join} from 'node:path';
import {fileURLToPath, pathToFileURL} from 'node:url';

const repo = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
const runtimePath = resolve(repo, process.env.JIT_RUNTIME || 'web/dist/emu.js');
const output = resolve(repo, process.env.JIT_OUTPUT || 'web/jit');
const createEmu = (await import(pathToFileURL(runtimePath))).default;
const runtime = await createEmu({locateFile: path => join(dirname(runtimePath), path)});
if (typeof runtime._emu_jit_abi !== 'function' || typeof runtime._emu_jit_offset !== 'function')
  throw Error('Build the browser runtime with S5LBOX_WASM_JIT before generating its seed');
const offsets = Array.from({length: 16}, (_, i) => runtime._emu_jit_offset(i));
const input = JSON.parse(gunzipSync(await readFile(process.env.SEED_INPUT || join(repo, 'tools/jit/seed-input.json.gz'))));
if (input.version !== 1 || !Array.isArray(input.regions) || input.regions.length > 1024)
  throw Error('Unsupported seed input');
const sources = input.regions.map(source => ({...source, ops: new Map(source.ops)}));
// The C region compiler in the runtime is the default; JIT_COMPILER=js runs
// the reference JavaScript compiler through WABT, which must match it exactly.
const {binary, regions} = process.env.JIT_COMPILER === 'js' ? await compileJs() : compileC();
function compileC() {
  const words = [sources.length];
  for (const source of sources) {
    words.push(source.pc >>> 0, source.thumb ? 1 : 0, source.index, source.ops.size);
    for (const [pc, word] of source.ops) words.push(pc >>> 0, word >>> 0);
  }
  const data = Uint32Array.from(words), ptr = runtime._malloc(data.byteLength);
  try {
    runtime.HEAPU32.set(data, ptr >>> 2);
    if (!runtime._emu_jit_compile(ptr, data.length)) throw Error('Region compiler failed');
    const at = runtime._emu_jit_compiled(0) >>> 0, length = runtime._emu_jit_compiled(1) >>> 0;
    const defs = runtime._emu_jit_compiled(2) >>> 2, count = runtime._emu_jit_compiled(3) >>> 0;
    const binary = Buffer.from(runtime.HEAPU8.slice(at, at + length)), regions = [];
    for (let i = 0; i < count; i++) {
      const d = runtime.HEAPU32.subarray(defs + i * 19, defs + i * 19 + 19);
      regions.push({pc: d[0], thumb: !!d[1], index: d[2], masks: Array.from(d.subarray(3))});
    }
    if (!WebAssembly.validate(binary)) throw Error('Region compiler produced an invalid module');
    return {binary, regions};
  } finally {
    runtime._emu_jit_compiled_free();
    runtime._free(ptr);
  }
}
async function compileJs() {
  const {default: compiler} = await import('./compiler.cjs');
  const {default: wabtFactory} = await import('wabt');
  compiler.configure(offsets);
  const {wat, regions} = compiler.compilePages(sources);
  const wabt = await wabtFactory();
  const parsed = wabt.parseWat('seed.wat', wat, {tail_call: true});
  try {
    parsed.validate();
    return {regions, binary: Buffer.from(parsed.toBinary({write_debug_names: !!process.env.JIT_NAMES}).buffer)};
  } finally { parsed.destroy(); }
}
if (binary.byteLength > 32 * 1024 * 1024) throw Error('Generated seed exceeds its byte cap');
const definitions = Buffer.from(JSON.stringify(regions) + '\n');
const hash = bytes => createHash('sha256').update(bytes).digest('hex');
const abi = {revision: runtime._emu_jit_abi(), offsets,
  moduleSha256: hash(binary), regionsSha256: hash(definitions)};
await mkdir(output, {recursive: true});
await writeFile(join(output, 'seed.wasm'), binary);
await writeFile(join(output, 'regions.json'), definitions);
await writeFile(join(output, 'abi.json'), JSON.stringify(abi, null, 2) + '\n');
console.log(`Generated ${regions.length} linked regions (${binary.byteLength} bytes; ${abi.moduleSha256})`);
