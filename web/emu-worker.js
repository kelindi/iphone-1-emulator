/* The worker owns all guest state. Presentation never drives the guest clock. */
importScripts('dist/emu.js', 'seed-jit.js');
let jit;
let module, api, paused = true, started = false, scheduled = false;
let framePointer, widthPointer, heightPointer, serialPointer, contactsPointer, audioPointer;
const AUDIO_FRAMES = 4096;
let frameBuffer = new ArrayBuffer(320 * 480 * 4);
let lastPixels = new Uint32Array(320 * 480);
const darkPixels = new Uint32Array(320 * 480).fill(0xff000000);
let lastFrame = 0, lastStats = 0, lastTicks = 0, steps = 0, uniqueFrames = 0;
let frameSequence = 0;
let capturing = false, resolveCaptureTurn;
const captureTurns = new MessageChannel();
captureTurns.port1.onmessage = () => { const resolve = resolveCaptureTurn; resolveCaptureTurn = null; resolve(); };
const yieldCapture = () => new Promise(resolve => { resolveCaptureTurn = resolve; captureTurns.port2.postMessage(0); });
const send = (type, values = {}, transfers = []) => postMessage({type, ...values}, transfers);
const error = () => module.ccall('emu_error', 'string', [], []);
// A message turn yields to input without setTimeout's nested-timer clamp,
// which otherwise idles this worker for ~4ms after every short CPU slice.
const clock = new MessageChannel();
let pacingTimer = 0, scheduleGeneration = 0;
clock.port1.onmessage = ({data}) => {
  if (data !== scheduleGeneration) return;
  scheduled = false; tick();
};
function invalidateSchedule() {
  clearTimeout(pacingTimer); pacingTimer = 0;
  scheduleGeneration++; scheduled = false;
}
function schedule() {
  if (scheduled || paused) return;
  scheduled = true;
  const generation = ++scheduleGeneration;
  const delay = api?.clockDelay() || 0;
  if (delay > 0) pacingTimer = setTimeout(() => {
    if (generation !== scheduleGeneration) return;
    scheduled = false; pacingTimer = 0; tick();
  }, Math.ceil(delay));
  else clock.port2.postMessage(generation);
}
function check(result) { if (result < 0) throw new Error(error() || `Emulator error ${result}`); return result; }
function alloc(data) {
  const ptr = module._malloc(data.byteLength);
  if (!ptr) throw new Error('Not enough memory for this phone');
  module.HEAPU8.set(data, ptr);
  return ptr;
}
function present(force = false) {
  if (!frameBuffer) return;
  const result = check(api.frame(framePointer, widthPointer, heightPointer));
  // A disabled LCD must show black, rather than leaving the last lit frame
  // visible indefinitely when the guest puts the phone to sleep.
  const pixels = result ? module.HEAPU32.subarray(framePointer >>> 2, (framePointer >>> 2) + lastPixels.length) : darkPixels;
  let changed = force;
  if (!changed) for (let i = 0; i < pixels.length; i++) {
    if (pixels[i] !== lastPixels[i]) { changed = true; break; }
  }
  if (!changed) return;
  lastPixels.set(pixels);
  new Uint32Array(frameBuffer).set(pixels);
  uniqueFrames++;
  const buffer = frameBuffer;
  frameBuffer = null;
  send('frame', {buffer, sequence:++frameSequence}, [buffer]);
}
// Guest PCM leaves in guest-time order; the page schedules it on WebAudio.
function drainAudio(deliver = true) {
  for (;;) {
    const frames = api.audio(audioPointer, AUDIO_FRAMES);
    if (!frames) return;
    if (!deliver) continue;
    const pcm = module.HEAPU8.slice(audioPointer, audioPointer + frames * 4).buffer;
    send('audio', {rate:api.audioRate(), buffer:pcm}, [pcm]);
  }
}
function tick() {
  if (paused) return;
  try {
    if (api.clockDelay() > 0) { schedule(); return; }
    const began = performance.now();
    do {
      const ran = api.run(50000);
      steps += ran;
      if (!ran) throw new Error(`Guest stopped (status ${api.status()}). Export a save or restart.`);
      if (api.clockDelay() > 0) break;
    } while (performance.now() - began < 6);
    jit?.pump();
    drainAudio();
    const now = performance.now();
    if (now - lastFrame >= 1000 / 60) { present(); lastFrame = now; }
    if (now - lastStats >= 1000) {
      const elapsed = (now - lastStats) / 1000;
      const ticks = api.ticks();
      const n = api.serial(serialPointer, 8192);
      send('metrics', {mips:steps / elapsed / 1e6, fps:uniqueFrames / elapsed,
        guestRate:(ticks - lastTicks) / 6000000 / elapsed, cycles:api.cycles(),
        memory:module.HEAPU8.byteLength, jit:jit?.stats(),
        serial:n ? new TextDecoder().decode(module.HEAPU8.subarray(serialPointer, serialPointer + n)) : ''});
      steps = uniqueFrames = 0;
      lastTicks = ticks;
      lastStats = now;
    }
    schedule();
  } catch (e) { paused = true; send('fatal', {message:e.message}); }
}
function setPaused(value) {
  invalidateSchedule();
  paused = value;
  check(api.hostClock(paused ? 0 : 1));
  lastStats = performance.now(); lastTicks = api.ticks(); steps = uniqueFrames = 0;
  schedule();
}
// Enter SpringBoard's own lock screen through the physical Sleep/Home buttons.
// Run before exposing input or frames, so restored apps do not flash on opening.
async function openLocked() {
  send('loading', {message:'Waking your iPhone…'});
  check(api.hostClock(1));
  const advanceUntil = async predicate => {
    const deadline = performance.now() + 20000;
    while (!predicate()) {
      if (performance.now() > deadline) throw new Error('Your iPhone could not wake. Please retry.');
      const began = performance.now();
      if (!api.clockDelay()) {
        do {
          if (!api.run(50000)) throw new Error('Your iPhone stopped while waking. Please retry.');
        } while (performance.now() - began < 6 && !api.clockDelay());
        jit?.pump();
      }
      await new Promise(resolve => setTimeout(resolve, Math.max(0, Math.ceil(api.clockDelay()))));
    }
  };
  const waitTicks = async ticks => {
    const target = api.ticks() + ticks;
    await advanceUntil(() => api.ticks() >= target);
  };
  const press = async which => {
    check(api.button(which, 1));
    try { await waitTicks(1080000); } // 180 ms of the device's 6 MHz clock.
    finally { check(api.button(which, 0)); }
  };
  const lit = () => check(api.frame(framePointer, widthPointer, heightPointer)) !== 0;
  if (lit()) {
    await press(0);
    await advanceUntil(() => !lit());
  }
  await waitTicks(1200000);
  await press(1); // Home wakes a sleeping phone without toggling Sleep again.
  await advanceUntil(lit);
  await waitTicks(2400000);
}
// iPhone OS 3 firmware is prepared locally from the user's own IPSW and is
// never published. Its files are streamed straight into the WASM heap rather
// than through assets.js: no Cache Storage copy, no second in-memory buffer.
// A path list is one byte stream; a .gz first part makes it one gzip stream.
async function streamInto(paths, base, size, label, report) {
  const urls = (Array.isArray(paths) ? paths : [paths]).map(p => new URL(p, base));
  if (urls.some(url => url.origin !== location.origin))
    throw new Error('Local firmware must come from this site');
  let part = 0, reader = null, received = 0;
  const raw = new ReadableStream({async pull(controller) {
    for (;;) {
      if (!reader) {
        if (part >= urls.length) { controller.close(); return; }
        const response = await fetch(urls[part++], {cache:'no-store'});
        if (!response.ok) throw new Error(`${label}: HTTP ${response.status}`);
        reader = response.body.getReader();
      }
      const {done, value} = await reader.read();
      if (done) { reader = null; continue; }
      received += value.byteLength; report(received);
      controller.enqueue(value); return;
    }
  }});
  const stream = /\.gz$/.test(urls[0].pathname) ? raw.pipeThrough(new DecompressionStream('gzip')) : raw;
  const ptr = module._malloc(size);
  if (!ptr) throw new Error(`Not enough memory for ${label}`);
  let offset = 0;
  try {
    for (const out = stream.getReader();;) {
      const {done, value} = await out.read();
      if (done) break;
      if (offset + value.byteLength > size) throw new Error(`${label} is larger than its manifest says`);
      module.HEAPU8.set(value, ptr + offset);
      offset += value.byteLength;
    }
    if (offset !== size) throw new Error(`${label} is ${offset} bytes, expected ${size}`);
  } catch (e) { module._free(ptr); throw e; }
  return ptr;
}
async function startIos3(message, manifest, base) {
  send('loading', {message:'Starting the emulator…'});
  module = await createEmu({locateFile:path => new URL(`dist/${path}`, location.href).href,
    printErr:text => send('log', {message:text})});
  const total = manifest.downloadBytes || null;
  let downloaded = 0;
  const report = label => received =>
    send('loading', {message:`Loading ${label} · ${((downloaded + received) / 1048576).toFixed(1)} MB`,
      received:downloaded + received, total});
  const load = async (entry, label) => {
    const ptr = await streamInto(entry.path, base, entry.size, label, report(label));
    downloaded += entry.downloadSize || entry.size;
    return ptr;
  };
  const k = await load(manifest.kernel, 'iPhone OS');
  const d = await load(manifest.devicetree, 'hardware profile');
  const disk = await load(manifest.disk, 'disk');
  try {
    // emu_init_ios3 takes ownership of the disk buffer on every path.
    check(module.ccall('emu_init_ios3', 'number', Array(6).fill('number'),
      [k, manifest.kernel.size, d, manifest.devicetree.size, disk, manifest.disk.size]));
  } finally { module._free(k); module._free(d); }
  if (manifest.snapshot) {
    const s = await load(manifest.snapshot, 'your iPhone');
    try { check(module.ccall('emu_load_snapshot', 'number', ['number','number'], [s, manifest.snapshot.size])); }
    finally { module._free(s); }
  } else send('loading', {message:'Starting iPhone OS from power-on. This takes a few minutes.'});
  check(module.ccall('emu_set_cpi', 'number', ['number'], [message.cpi || 1]));
  return Boolean(manifest.snapshot);
}
async function start(message) {
  if (started) throw new Error('Worker already initialized');
  started = true;
  const {assetBytes} = await import('./assets.js');
  // The published default keeps its exact request; another firmware's
  // manifest must come from this site.
  const manifestURL = message.manifest ? new URL(message.manifest, location.href) : null;
  if (manifestURL && manifestURL.origin !== location.origin)
    throw new Error('Firmware manifest must come from this site');
  const response = await fetch(manifestURL || 'manifest.json', {cache:'no-cache'});
  if (!response.ok) throw new Error(response.status === 404 && message.manifest
    ? 'This firmware is not prepared on this computer. See docs/ios3-browser.md.'
    : `Could not load phone manifest (${response.status})`);
  const manifest = await response.json();
  if (manifest.kind === 'ios3') {
    await startIos3(message, manifest, manifestURL);
    // Only this firmware's own seed (?seed=, recorded from its code) runs it.
    await finishStart(message, false, Boolean(new URL(location.href).searchParams.get('seed')));
    return;
  }
  const downloads = [manifest.kernel, manifest.devicetree,
    ...(!message.checkpoint ? [manifest.snapshot || manifest.rootfs] : [])].flat();
  const sizes = downloads.map(path => manifest.assetBytes?.[path]);
  const total = sizes.every(size => Number.isFinite(size) && size > 0)
    ? sizes.reduce((sum, size) => sum + size, 0) : null;
  let downloaded = 0, reported = 0;
  const load = async (paths, label) => {
    let partReceived = 0;
    const bytes = await assetBytes(paths, received => {
      partReceived = received;
      // A corrupt cache retry must not send progress backwards or count twice.
      reported = Math.max(reported, downloaded + received);
      send('loading', {message:`Downloading ${label} · ${(reported / 1048576).toFixed(1)} MB`,
        received:reported, total});
    }, manifest.assetSha256 || {});
    downloaded += partReceived;
    return bytes;
  };
  const kernel = await load(manifest.kernel, 'iPhone OS');
  const tree = await load(manifest.devicetree, 'hardware profile');
  let snapshot = message.checkpoint ? new Uint8Array(message.checkpoint) : null;
  let rootfs = null;
  if (!snapshot && manifest.snapshot) snapshot = await load(manifest.snapshot, 'your iPhone');
  if (!snapshot) rootfs = await load(manifest.rootfs, 'disk');
  send('loading', {message:'Waking your iPhone…'});
  module = await createEmu({locateFile:path => new URL(`dist/${path}`, location.href).href,
    printErr:text => send('log', {message:text})});
  const k = alloc(kernel), d = alloc(tree), r = rootfs ? alloc(rootfs) : 0;
  try {
    check(module.ccall('emu_init', 'number', Array(7).fill('number'),
      [k, kernel.length, d, tree.length, r, rootfs?.length || 0, 256]));
  } finally { module._free(k); module._free(d); if (r) module._free(r); }
  if (snapshot) {
    const s = alloc(snapshot);
    try { check(module.ccall('emu_load_snapshot', 'number', ['number','number'], [s, snapshot.length])); }
    finally { module._free(s); }
  }
  check(module.ccall('emu_set_cpi', 'number', ['number'], [message.cpi || 1]));
  await finishStart(message, Boolean(snapshot));
}
// `wake`: a restored OS 1.0 phone enters its own lock screen first. iPhone OS 3
// snapshots are already taken there, and a cold boot draws it by itself.
// `seed`: the default regions were recorded from OS 1.0. Another OS runs them
// only from its own seed directory; without one it stays on the interpreter.
async function finishStart(message, wake, seed = true) {
  // Optional code preparation runs alongside the genuine guest startup.
  jit = seed ? new SeedJIT(module) : null;
  api = Object.fromEntries([
    ['hostClock','emu_host_clock',['number']], ['clockDelay','emu_clock_delay',[]],
    ['run','emu_run',['number']], ['status','emu_status',[]],
    ['frame','emu_frame',['number','number','number']], ['ticks','emu_ticks',[]],
    ['cycles','emu_cycles',[]], ['serial','emu_serial_read',['number','number']],
    ['audio','emu_audio_read',['number','number']], ['audioRate','emu_audio_rate',[]],
    ['contacts','emu_touch_contacts',['number','number']], ['button','emu_button',['number','number']],
    ['release','emu_release_inputs',[]], ['idle','emu_inputs_idle',[]],
    ['buttonState','emu_button_state',['number']], ['save','emu_save_snapshot',[]],
    ['beginCapture','emu_snapshot_begin',[]], ['advanceCapture','emu_snapshot_advance',['number']],
    ['captureData','emu_snapshot_data',[]],
    ['size','emu_snapshot_size',[]], ['freeSave','emu_free_snapshot',[]]
  ].map(([key,name,args]) => [key, module.cwrap(name, 'number', args)]));
  framePointer = module._malloc(320 * 480 * 4);
  widthPointer = module._malloc(4); heightPointer = module._malloc(4);
  serialPointer = module._malloc(8192); contactsPointer = module._malloc(5 * 6 * 4);
  audioPointer = module._malloc(AUDIO_FRAMES * 4);
  if (![framePointer,widthPointer,heightPointer,serialPointer,contactsPointer,audioPointer].every(Boolean))
    throw new Error('Not enough memory for display buffers');
  check(api.release());
  if (wake) await openLocked();
  drainAudio(false); // The wake sequence's own clicks happen before the page is shown.
  present(true);
  send('ready', {id:message.id, restored:Boolean(message.checkpoint), silent:Boolean(api.buttonState(4))});
  setPaused(false);
}
onmessage = async ({data:message}) => {
  if (message.type === 'recycle') { frameBuffer = message.buffer; return; }
  try {
    if (message.type === 'start') { await start(message); return; }
    if (!api) throw new Error('iPhone is still starting');
    let result;
    switch (message.type) {
      case 'contacts': {
        const records = message.contacts;
        if (!Array.isArray(records) || records.length > 5) throw new Error('Invalid contacts');
        const packed = new Uint32Array(records.flatMap(c => [c.id,c.x,c.y,c.pressure || 255,8,8]));
        module.HEAPU32.set(packed, contactsPointer >>> 2);
        check(api.contacts(contactsPointer, records.length));
        break;
      }
      case 'button': check(api.button(message.which, Number(message.pressed))); break;
      case 'release': check(api.release()); break;
      case 'pause': check(api.release()); setPaused(Boolean(message.value)); present(true); break;
      case 'idle': send('reply', {id:message.id, idle:Boolean(api.idle())}); return;
      case 'restore': {
        jit?.reset();
        const data = new Uint8Array(message.buffer), ptr = alloc(data);
        try { check(module.ccall('emu_load_snapshot','number',['number','number'],[ptr,data.length])); }
        finally { module._free(ptr); }
        invalidateSchedule();
        check(api.hostClock(paused ? 0 : 1));
        schedule();
        check(api.release()); present(true);
        lastTicks = api.ticks(); lastStats = performance.now(); steps = uniqueFrames = 0;
        send('reply', {id:message.id, silent:Boolean(api.buttonState(4))});
        return;
      }
      case 'snapshot': {
        if (capturing) throw new Error('A checkpoint is already being captured');
        capturing = true;
        const began = performance.now();
        let buffer, captureMs, maxChunkMs = 0;
        try {
          check(api.beginCapture());
          invalidateSchedule();
          // Ordinary foreground capture retains its elapsed-time anchor.
          // Pause/hidden/restore still explicitly reset the host clock.
          schedule();
          captureMs = performance.now() - began;
          // The machine's complete state is already frozen in owned memory.
          // Finalize the unchanged v39 checksum and transfer copy in bounded
          // turns, interleaved with CPU, input, and frame work.
          for (;;) {
            await yieldCapture();
            const start = performance.now();
            const complete = check(api.advanceCapture(1048576));
            maxChunkMs = Math.max(maxChunkMs, performance.now() - start);
            if (complete) break;
          }
          const ptr = api.captureData(), length = api.size();
          if (!ptr || !length) throw new Error('Checkpoint finalization failed');
          const allocationStart = performance.now();
          buffer = new ArrayBuffer(length);
          const output = new Uint8Array(buffer);
          maxChunkMs = Math.max(maxChunkMs, performance.now() - allocationStart);
          for (let offset = 0; offset < length; offset += 1048576) {
            await yieldCapture();
            const start = performance.now(), end = Math.min(length, offset + 1048576);
            output.set(module.HEAPU8.subarray(ptr + offset, ptr + end), offset);
            maxChunkMs = Math.max(maxChunkMs, performance.now() - start);
          }
        } finally { api.freeSave(); capturing = false; }
        send('reply', {id:message.id, buffer, captureMs, maxChunkMs, finalizeMs:performance.now() - began - captureMs}, [buffer]);
        return;
      }
      default: throw new Error(`Unknown request: ${message.type}`);
    }
    send('reply', {id:message.id, result});
  } catch (e) {
    send(message.type === 'start' ? 'fatal' : 'reply', {id:message.id, error:e.message, message:e.message});
  }
};
