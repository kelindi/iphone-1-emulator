/* Optional guarded seed: guest startup never waits for this accelerator. */
// HTTP origins use the interpreter when native hash verification is unavailable.
const seedEnabled = Boolean(globalThis.crypto?.subtle) && new URL(location.href).searchParams.get('jit') !== '0';
const SEED_MAX_BYTES = 32 * 1024 * 1024;
const SEED_MAX_REGIONS = 512;
const SEED_PREPARE_TIMEOUT_MS = 8000;
const seedAbort = new AbortController();
const seedResponses = new Set();
function checkSeedActive() {
  if (seedAbort.signal.aborted) throw Error('Optional seed preparation aborted');
}
function cancelSeedResponse(response) {
  if (!response) return;
  seedResponses.delete(response);
  // A tee branch's cancel promise can wait for the other branch. Never await
  // it here; abort plus cancellation of every owned branch bounds cleanup.
  try { response.body?.cancel()?.catch(() => {}); } catch (_) {}
}
async function fetchSeed(path, limit) {
  const response = await fetch(path, {signal: seedAbort.signal});
  seedResponses.add(response);
  try {
    checkSeedActive();
    if (!response.ok) throw Error('Seed download: ' + path);
    if (Number(response.headers?.get('Content-Length')) > limit) throw Error('Seed byte cap');
    return response;
  } catch (error) { cancelSeedResponse(response); throw error; }
}
async function readSeedResponse(response, limit) {
  checkSeedActive();
  if (!response.body?.getReader) {
    const bytes = await response.arrayBuffer();
    checkSeedActive();
    if (bytes.byteLength > limit) throw Error('Seed byte cap');
    seedResponses.delete(response);
    return bytes;
  }
  const reader = response.body.getReader(), chunks = [];
  let length = 0, complete = false;
  try {
    for (;;) {
      const {done, value} = await reader.read();
      checkSeedActive();
      if (done) break;
      length += value.byteLength;
      if (length > limit) throw Error('Seed byte cap');
      chunks.push(value);
    }
    const bytes = new Uint8Array(length);
    let offset = 0;
    for (const chunk of chunks) { bytes.set(chunk, offset); offset += chunk.byteLength; }
    complete = true;
    return bytes.buffer;
  } finally {
    if (!complete) { try { reader.cancel().catch(() => {}); } catch (_) {} }
    reader.releaseLock();
    seedResponses.delete(response);
  }
}
async function readSeed(path, limit) {
  return readSeedResponse(await fetchSeed(path, limit), limit);
}
async function readSeedModule() {
  const response = await fetchSeed('jit/seed.wasm', SEED_MAX_BYTES);
  const mime = response.headers?.get('Content-Type')?.split(';')[0].trim().toLowerCase();
  if (typeof WebAssembly.compileStreaming === 'function' &&
      typeof response.clone === 'function' && mime === 'application/wasm') {
    let verificationResponse;
    try { verificationResponse = response.clone(); } catch (_) {}
    if (verificationResponse) {
      seedResponses.add(verificationResponse);
      const binary = await readSeedResponse(verificationResponse, SEED_MAX_BYTES);
      // Keep the actual fetched Response and its URL/cache metadata. A new
      // Response(binary) would not retain Blink's native-code-cache handler.
      return {binary, response};
    }
  }
  return {binary: await readSeedResponse(response, SEED_MAX_BYTES), response: null};
}
async function compileVerifiedSeed(binary, response) {
  checkSeedActive();
  if (response) {
    try {
      const module = await WebAssembly.compileStreaming(response);
      checkSeedActive();
      return module;
    } catch (error) {
      checkSeedActive();
      // Unsupported streaming/response MIME can still use the verified bytes.
      // A real compilation error is preserved rather than doing the work twice.
      if (!(error instanceof TypeError)) throw error;
    } finally { cancelSeedResponse(response); }
  }
  checkSeedActive();
  const module = await WebAssembly.compile(binary);
  checkSeedActive();
  return module;
}
async function prepareSeed() {
  let timer;
  try {
    return await Promise.race([
      (async () => {
        const [{binary, response}, definitions, abiBytes] = await Promise.all([
          readSeedModule(), readSeed('jit/regions.json', 512 * 1024), readSeed('jit/abi.json', 16384)
        ]);
        const abi = JSON.parse(new TextDecoder().decode(abiBytes));
        const hash = async bytes => Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', bytes)),
          b => b.toString(16).padStart(2, '0')).join('');
        if (await hash(binary) !== abi.moduleSha256 || await hash(definitions) !== abi.regionsSha256)
          throw Error('Seed asset identity mismatch');
        const defs = JSON.parse(new TextDecoder().decode(definitions));
        checkSeedActive();
        return [{module: await compileVerifiedSeed(binary, response), bytes: binary.byteLength}, defs, abi];
      })(),
      new Promise((_, reject) => { timer = setTimeout(() => {
        seedAbort.abort();
        for (const response of seedResponses) cancelSeedResponse(response);
        reject(Error('Optional seed preparation timed out'));
      }, SEED_PREPARE_TIMEOUT_MS); })
    ]);
  } catch (error) {
    seedAbort.abort();
    for (const response of seedResponses) cancelSeedResponse(response);
    return {error};
  } finally { clearTimeout(timer); }
}
const seedPromise = seedEnabled ? prepareSeed() : null;
class SeedJIT {
  constructor(module) {
    this.module = module; this.enabled = false; this.supported = false;
    this.generation = 0; this.functions = new Set(); this.bytes = 0;
    this.retired = this.mask = 0; this.ready = Promise.resolve();
    const required = ['_emu_jit_enable', '_emu_jit_reset',
      '_emu_jit_abi', '_emu_jit_offset', '_emu_jit_install', '_emu_jit_retired',
      '_emu_jit_stat', '_emu_jit_stale', '_emu_jit_span', '_malloc', '_free', 'addFunction', 'removeFunction'];
    this.supported = required.every(name => typeof module[name] === 'function');
    if (!this.supported || !seedEnabled) return;
    try {
      module._emu_jit_enable(0);
      if (!module.jitMemory?.buffer) throw Error('Seed shared memory is unavailable');
      this.retired = module._malloc(4096 * 4);
      if (!this.retired) throw Error('Seed retirement buffer allocation failed');
      this.mask = module._malloc(64);
      if (!this.mask) throw Error('Seed mask allocation failed');
      this.enabled = true; this.ready = this.reset();
    } catch (error) { this.disable(error); }
  }
  reset() {
    const generation = ++this.generation, m = this.module;
    if (this.supported) { m._emu_jit_enable(0); m._emu_jit_reset(); }
    for (const fn of this.functions) m.removeFunction(fn);
    this.functions.clear(); this.bytes = 0;
    return this.enabled ? this.install(generation) : Promise.resolve();
  }
  disable(error) {
    this.error = String(error.stack || error); this.enabled = false;
    this.reset();
    if (this.retired) this.module._free(this.retired);
    if (this.mask) this.module._free(this.mask);
    this.retired = this.mask = 0;
    console.error('Seed disabled:', this.error);
  }
  async install(generation) {
    try {
      const seed = await seedPromise;
      if (generation !== this.generation) return;
      if (seed.error) throw seed.error;
      const [{module, bytes}, defs, abi] = seed;
      if (abi.revision !== this.module._emu_jit_abi() || !Array.isArray(abi.offsets) ||
          abi.offsets.length !== 16 || abi.offsets.some((offset, i) => offset !== this.module._emu_jit_offset(i)))
        throw Error('Seed runtime ABI mismatch');
      if (!Array.isArray(defs) || defs.length > SEED_MAX_REGIONS) throw Error('Seed function cap');
      const uint32 = value => Number.isInteger(value) && value >= 0 && value <= 0xffffffff;
      const keys = new Set(), indices = new Set();
      for (const r of defs) {
        if (!r || !uint32(r.pc) || (r.pc & 1023) || !uint32(r.index) || typeof r.thumb !== 'boolean' ||
            !Array.isArray(r.masks) || r.masks.length !== 16 || !r.masks.every(uint32))
          throw Error('Invalid seed region');
        const key = `${r.pc}/${r.thumb}`;
        if (keys.has(key) || indices.has(r.index)) throw Error('Duplicate seed region or export');
        keys.add(key); indices.add(r.index);
      }
      const instance = await WebAssembly.instantiate(module, {env: {
        memory: this.module.jitMemory, stale: this.module._emu_jit_stale, span: this.module._emu_jit_span
      }});
      if (generation !== this.generation) return;
      for (const r of defs) {
        const generated = instance.exports['b' + r.index];
        if (typeof generated !== 'function') throw Error('Missing seed function export');
        const fn = this.module.addFunction(generated, 'iii');
        this.functions.add(fn); // Own the slot even when native admission throws.
        new Uint32Array(this.module.jitMemory.buffer, this.mask, 16).set(r.masks);
        if (!this.module._emu_jit_install(r.pc, r.thumb ? 1 : 0, fn, this.mask))
          throw Error('Seed table admission');
      }
      this.bytes = bytes;
      this.module._emu_jit_enable(1);
    } catch (error) { if (generation === this.generation) this.disable(error); }
  }
  pump() {
    if (!this.enabled) return;
    const n = this.module._emu_jit_retired(this.retired);
    for (const fn of new Uint32Array(this.module.jitMemory.buffer, this.retired, n))
      if (this.functions.delete(fn)) this.module.removeFunction(fn);
    if (!this.functions.size) this.bytes = 0;
  }
  stats() {
    return {enabled: this.enabled, error: this.error, generation: this.generation,
      regions: this.functions.size, bytes: this.bytes,
      retired: this.supported ? this.module._emu_jit_stat(0) : 0,
      calls: this.supported ? this.module._emu_jit_stat(1) : 0};
  }
}
