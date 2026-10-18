#!/usr/bin/env node
/** Regenerate the guarded seed from retained opcode positions and the live C ABI. */
import {readFile, writeFile, mkdir} from 'node:fs/promises';
import {gunzipSync} from 'node:zlib';
import {createHash} from 'node:crypto';
import {dirname, resolve, join} from 'node:path';
import {fileURLToPath, pathToFileURL} from 'node:url';
import wabtFactory from 'wabt';
import compiler from './compiler.cjs';

const repo = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
const runtimePath = resolve(repo, process.env.JIT_RUNTIME || 'web/dist/emu.js');
const output = resolve(repo, process.env.JIT_OUTPUT || 'web/jit');
const createEmu = (await import(pathToFileURL(runtimePath))).default;
const runtime = await createEmu({locateFile: path => join(dirname(runtimePath), path)});
if (typeof runtime._emu_jit_abi !== 'function' || typeof runtime._emu_jit_offset !== 'function')
  throw Error('Build the browser runtime with S5LBOX_WASM_JIT before generating its seed');
const offsets = Array.from({length: 18}, (_, i) => runtime._emu_jit_offset(i));
compiler.configure(offsets);
const input = JSON.parse(gunzipSync(await readFile(process.env.SEED_INPUT || join(repo, 'tools/jit/seed-input.json.gz'))));
if (input.version !== 1 || !Array.isArray(input.regions) || input.regions.length > 1024)
  throw Error('Unsupported seed input');
const {wat, regions} = compiler.compilePages(
  input.regions.map(source => ({...source, ops: new Map(source.ops)})));
const wabt = await wabtFactory();
const parsed = wabt.parseWat('seed.wat', wat, {tail_call: true});
let binary;
try { parsed.validate(); binary = Buffer.from(parsed.toBinary({write_debug_names: !!process.env.JIT_NAMES}).buffer); }
finally { parsed.destroy(); }
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
