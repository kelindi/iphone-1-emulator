import {track} from './analytics.js';
import {acquireProfile, readCheckpoint, writeOwnedCheckpoint, clearCheckpoints, checkpointBytes, PROFILE_VERSION} from './storage.js';

const $ = id => document.getElementById(id);
const canvas = $('screen'), context = canvas.getContext('2d', {alpha:false});
const pixels = context.createImageData(320, 480);
const contacts = new Map(), keys = new Map(), pending = new Map();
let worker, sequence = 0, ready = false, paused = false, profileOwned = false;
let saving = false, liveSave = false, lastInput = 0, lastSave = 0, savedRecord = null, silent = false;
let serial = '', metrics = {}, captureMs = null, captureChunkMs = null, finalizeMs = null, startTime = 0;
let preparingOffline = false;
function setProgress(percent) {
  const progress = $('progress');
  progress.setAttribute('aria-valuenow', String(percent));
  progress.querySelector('span').style.transform = `scaleX(${percent / 100})`;
  $('boot-percent').textContent = `${percent}%`;
}
const setStatus = message => {
  $('status').textContent = message;
  if (!ready && !$('boot-loader').hidden) {
    $('boot-label').textContent = message;
    $('progress').setAttribute('aria-valuetext', message);
  }
};
const describeError = error => error?.message || error?.name || String(error || 'Unknown error');
const queryCpi = Number.parseInt(new URLSearchParams(location.search).get('cpi'), 10);
const cpi = Number.isInteger(queryCpi) && queryCpi >= 1 && queryCpi <= 16 ? queryCpi : 1;

function call(type, fields = {}, transfer = []) {
  if (!worker) return Promise.reject(new Error('iPhone is not running'));
  const id = ++sequence;
  return new Promise((resolve, reject) => {
    pending.set(id, {resolve, reject});
    worker.postMessage({type, id, ...fields}, transfer);
  });
}
function controls() {
  document.querySelectorAll('[data-button]').forEach(button => { button.disabled = !ready || paused; });
  // Ordinary checkpoints own their captured bytes while the guest stays live.
  // Import/reset still serialize controls through the separate saving guard.
  $('pause').disabled = !ready || (saving && !liveSave);
  $('pause').textContent = paused ? 'Resume' : 'Pause';
  $('save').disabled = !ready || saving || !profileOwned;
  $('export').disabled = saving || (!ready && !savedRecord);
  $('import').disabled = !ready || saving || !profileOwned;
  $('reset').disabled = saving || !profileOwned;
  $('recover').disabled = saving || !profileOwned;
  $('offline').disabled = !ready || preparingOffline || !('serviceWorker' in navigator);
  canvas.style.pointerEvents = ready && !paused ? '' : 'none';
}
function fail(error) {
  track('emulator failed', {error_type:error?.name || 'Error', startup:!ready});
  ready = false;
  canvas.dataset.ready = 'false'; document.body.dataset.ready = 'false';
  $('boot-loader').hidden = false;
  $('boot-loader').dataset.state = 'error';
  setStatus(describeError(error));
  worker?.terminate();
  worker = null;
  for (const req of pending.values()) req.reject(new Error(describeError(error)));
  pending.clear();
  $('progress').hidden = true;
  $('retry').hidden = false; $('retry').disabled = false;
  $('recover').hidden = !savedRecord;
  controls();
}
function showRunning() {
  if (paused) setStatus('Paused. Your iPhone will wait here.');
  else if (!profileOwned) setStatus('Ready · browser saving is unavailable in this tab. Export keeps a backup.');
  else if (lastSave) setStatus(`Saved at ${new Date(lastSave).toLocaleTimeString([], {hour:'2-digit',minute:'2-digit'})}`);
  else setStatus('Ready. Your iPhone, just as you left it.');
}
function inputError(error) {
  setStatus(`Input could not be delivered: ${error.message}`);
  releaseInputs().catch(() => {});
}
function releaseInputs() {
  contacts.clear(); keys.clear();
  document.querySelectorAll('.pressed').forEach(button => button.classList.remove('pressed'));
  return ready ? call('release') : Promise.resolve();
}
async function start(recovery = null) {
  track('emulator started', {recovery:Boolean(recovery)});
  ready = false; paused = false;
  $('boot-loader').hidden = false;
  $('boot-loader').dataset.state = 'loading';
  worker?.terminate();
  for (const req of pending.values()) req.reject(new Error('Phone restarted'));
  pending.clear(); contacts.clear(); keys.clear();
  canvas.dataset.ready = 'false'; document.body.dataset.ready = 'false';
  controls(); $('progress').hidden = false; $('retry').hidden = true; $('recover').hidden = true;
  setProgress(0);
  setStatus('Downloading iPhone OS…');
  try {
    savedRecord = recovery || await readCheckpoint();
    if (savedRecord && savedRecord.version !== PROFILE_VERSION)
      throw new Error('This save needs a different emulator version. Export it before resetting.');
    lastSave = savedRecord?.savedAt || 0;
  } catch (error) {
    if (savedRecord) { fail(error); return; }
    profileOwned = false;
  }
  const workerURL = new URL('./emu-worker.js', import.meta.url);
  // A diagnostic opt-out compares the same build's literal interpreter loop.
  if (new URLSearchParams(location.search).get('jit') === '0')
    workerURL.searchParams.set('jit', '0');
  worker = new Worker(workerURL);
  worker.onerror = event => fail(new Error(event.message || 'Emulator worker failed'));
  worker.onmessage = ({data:message}) => {
    if (message.type === 'frame') {
      pixels.data.set(new Uint8Array(message.buffer));
      context.putImageData(pixels, 0, 0);
      canvas.dataset.frame = String(message.sequence);
      worker.postMessage({type:'recycle', buffer:message.buffer}, [message.buffer]);
    } else if (message.type === 'loading') {
      setStatus(message.message);
      if (message.total > 0) setProgress(Math.min(99, Math.floor(message.received / message.total * 100)));
    }
    else if (message.type === 'ready') {
      setProgress(100);
      ready = true; paused = false; lastInput = performance.now();
      $('boot-loader').hidden = true;
      canvas.dataset.ready = 'true'; document.body.dataset.ready = 'true';
      $('progress').hidden = true;
      silent = Boolean(message.silent);
      document.querySelector('[data-toggle]').setAttribute('aria-pressed', String(silent));
      controls(); showRunning();
      track('emulator ready', {startup_ms:Math.round(performance.now() - startTime), restored:Boolean(savedRecord)});
      window.dispatchEvent(new CustomEvent('iphone-ready', {detail:{startupMs:performance.now() - startTime}}));
    } else if (message.type === 'reply') {
      const req = pending.get(message.id); pending.delete(message.id);
      if (req) message.error ? req.reject(new Error(message.error)) : req.resolve(message);
    } else if (message.type === 'metrics') {
      metrics = message; serial = (serial + message.serial).slice(-16000);
      $('diagnostics').textContent = `${message.mips.toFixed(2)} M instructions/s\n` +
        `${message.fps.toFixed(1)} changed frames/s · ${message.guestRate.toFixed(2)}× guest time\n` +
        `Instruction ${message.cycles.toLocaleString()} · WASM heap ${(message.memory / 1048576).toFixed(0)} MiB\n` +
        `Snapshot capture: ${captureMs?.toFixed(0) || '—'} ms\n\n${serial}`;
    } else if (message.type === 'log') serial = (serial + message.message + '\n').slice(-16000);
    else if (message.type === 'fatal') {
      for (const req of pending.values()) req.reject(new Error(message.message));
      pending.clear(); fail(new Error(message.message));
    }
  };
  startTime = performance.now();
  const checkpoint = savedRecord ? await checkpointBytes(savedRecord) : null;
  worker.postMessage({type:'start', cpi, checkpoint}, checkpoint ? [checkpoint] : []);
}

function point(event) {
  const rect = canvas.getBoundingClientRect();
  return {x:Math.max(0, Math.min(319, Math.floor((event.clientX - rect.left) * 320 / rect.width))),
    y:Math.max(0, Math.min(479, Math.floor((event.clientY - rect.top) * 480 / rect.height)))};
}
function sendContacts() {
  lastInput = performance.now();
  call('contacts', {contacts:[...contacts.values()]}).catch(inputError);
}
canvas.addEventListener('pointerdown', event => {
  if (!ready || paused || (event.pointerType === 'mouse' && event.button !== 0)) return;
  const used = new Set([...contacts.values()].map(c => c.id));
  const id = [1,2,3,4,5].find(n => !used.has(n));
  if (!id) return;
  contacts.set(event.pointerId, {id, ...point(event)});
  canvas.setPointerCapture(event.pointerId); sendContacts(); event.preventDefault();
  if (!canvas.dataset.analyticsInteracted) {
    canvas.dataset.analyticsInteracted = 'true';
    track('emulator interacted', {input_type:event.pointerType});
  }
});
canvas.addEventListener('pointermove', event => {
  const contact = contacts.get(event.pointerId);
  if (!contact || !ready || paused) return;
  Object.assign(contact, point(event)); sendContacts(); event.preventDefault();
});
function lift(event) {
  if (!contacts.delete(event.pointerId)) return;
  sendContacts(); event.preventDefault();
}
for (const event of ['pointerup','pointercancel','lostpointercapture']) canvas.addEventListener(event, lift);
function setButton(which, pressed) {
  if (!ready || paused) return;
  if (pressed || which === 4) track('phone control used', {control:['sleep_wake','home','volume_up','volume_down','silent'][which], ...(which === 4 ? {enabled:pressed} : {})});
  lastInput = performance.now();
  document.querySelector(`[data-button="${which}"]`)?.classList.toggle('pressed', pressed);
  call('button', {which, pressed}).catch(inputError);
}
function toggleSilent() {
  if (!ready || paused) return;
  silent = !silent;
  document.querySelector('[data-toggle]').setAttribute('aria-pressed', String(silent));
  setButton(4, silent);
}
document.querySelectorAll('[data-button]').forEach(button => {
  const which = Number(button.dataset.button);
  if (button.hasAttribute('data-toggle')) { button.addEventListener('click', toggleSilent); return; }
  button.addEventListener('pointerdown', event => {
    if (event.button !== 0) return;
    button.setPointerCapture(event.pointerId); setButton(which, true); event.preventDefault();
  });
  for (const type of ['pointerup','pointercancel','lostpointercapture'])
    button.addEventListener(type, () => setButton(which, false));
  button.addEventListener('click', event => {
    if (event.detail === 0) { setButton(which, true); setButton(which, false); }
  });
});
const keyButtons = new Map([['escape',1],['h',1],['p',0],['+',2],['=',2],['-',3],['_',3]]);
document.addEventListener('keydown', event => {
  if (event.repeat || event.ctrlKey || event.metaKey || event.altKey ||
      event.target.closest('input,textarea,[contenteditable="true"]')) return;
  const key = event.key.toLowerCase();
  if (key === 'm') { toggleSilent(); event.preventDefault(); return; }
  const which = keyButtons.get(key);
  if (which === undefined) return;
  keys.set(event.code, which); setButton(which, true); event.preventDefault();
});
document.addEventListener('keyup', event => {
  const which = keys.get(event.code);
  if (which === undefined) return;
  keys.delete(event.code); setButton(which, false); event.preventDefault();
});
window.addEventListener('blur', () => releaseInputs().catch(() => {}));

async function capture() {
  const result = await call('snapshot');
  captureMs = result.captureMs; captureChunkMs = result.maxChunkMs; finalizeMs = result.finalizeMs;
  return result.buffer;
}
async function save(manual = false) {
  if (!ready || saving || !profileOwned) return;
  if (!manual && (paused || contacts.size || keys.size || performance.now() - lastInput < 2000)) return;
  saving = true; liveSave = true; controls();
  let phase = 'input release';
  try {
    if (manual) await releaseInputs();
    const {idle} = await call('idle');
    if (!idle) { if (manual) setStatus('Finishing your last touch. Save again in a moment.'); return; }
    if (manual) setStatus('Saving your iPhone…');
    phase = 'snapshot capture';
    const buffer = await capture();
    phase = 'browser storage write';
    lastSave = await writeOwnedCheckpoint(buffer);
    track('phone saved', {manual});
    phase = 'browser storage read';
    savedRecord = await readCheckpoint(); showRunning();
    navigator.storage?.persist?.().catch(() => {});
  } catch (error) {
    track('phone save failed', {phase, manual, error_type:error?.name || 'Error'});
    console.error(`Save failed during ${phase}`, error?.name, error?.message, error);
    setStatus(`Save failed: ${describeError(error)}. Export a backup to keep this session.`);
  }
  finally { saving = false; liveSave = false; controls(); }
}
function download(blob, name) {
  const url = URL.createObjectURL(blob), a = document.createElement('a');
  a.href = url; a.download = name; a.click(); setTimeout(() => URL.revokeObjectURL(url), 30000);
}
$('save').addEventListener('click', () => save(true));
// Updates wait for every phone tab to close, so a running guest keeps one
// matching shell/manifest/WASM bundle. Keep this notice separate from save status.
const updateNotice = document.createElement('p');
updateNotice.id = 'update-notice';
updateNotice.setAttribute('role', 'status');
updateNotice.hidden = true;
$('status').after(updateNotice);
const watchedRegistrations = new WeakSet();
function watchOfflineUpdates(registration) {
  const showUpdate = () => {
    if (!registration.waiting || !registration.active) return;
    updateNotice.textContent = 'Phone update ready. Save, close all phone tabs, then reopen to update.';
    updateNotice.hidden = false;
  };
  showUpdate();
  if (watchedRegistrations.has(registration)) return;
  watchedRegistrations.add(registration);
  const watchInstalling = () => {
    registration.installing?.addEventListener('statechange', showUpdate);
    showUpdate();
  };
  registration.addEventListener('updatefound', watchInstalling);
  watchInstalling();
}
async function checkOfflineUpdate() {
  if (!('serviceWorker' in navigator)) return;
  try {
    // Looking up an existing registration does not opt new visitors into offline
    // installation. Ignore registrations inherited from a parent application.
    const scope = new URL('./', location.href).href;
    const registration = await navigator.serviceWorker.getRegistration(scope);
    if (!registration || registration.scope !== scope) return;
    watchOfflineUpdates(registration);
    await registration.update();
  } catch {
    // Offline visits remain usable; the next connected visit checks again.
  }
}
checkOfflineUpdate();
$('offline').addEventListener('click', async () => {
  preparingOffline = true; controls();
  try {
    setStatus('Preparing your phone for offline visits…');
    const manifest = await (await fetch('manifest.json', {cache:'no-cache'})).json();
    const {assetBytes} = await import('./assets.js');
    for (const paths of [manifest.kernel, manifest.devicetree, manifest.snapshot || manifest.rootfs]) {
      await assetBytes(paths, bytes => setStatus(`Keeping your phone offline · ${(bytes / 1048576).toFixed(1)} MB`), manifest.assetSha256 || {});
    }
    const cache = await caches.open('iphone-1-assets-1a543a-v1');
    for (const path of [manifest.kernel, manifest.devicetree, manifest.snapshot || manifest.rootfs].flat())
      if (!await cache.match(new URL(path, location.href))) throw new Error('Browser storage could not retain all phone files');
    const registration = await navigator.serviceWorker.register('./service-worker.js');
    watchOfflineUpdates(registration);
    const candidate = registration.installing || registration.waiting || registration.active;
    if (!candidate) throw new Error('Offline installation did not start');
    if (!['installed','activated'].includes(candidate.state)) await new Promise((resolve, reject) => {
      candidate.addEventListener('statechange', () => {
        if (['installed','activated'].includes(candidate.state)) resolve();
        if (candidate.state === 'redundant') reject(new Error('Offline installation failed'));
      });
    });
    track('offline enabled');
    setStatus('Your phone is available offline. Save to keep your latest changes.');
  } catch (error) { track('offline setup failed', {error_type:error?.name || 'Error'}); setStatus(`Offline setup failed: ${describeError(error)}. Try again when connected.`); }
  finally { preparingOffline = false; controls(); }
});
$('export').addEventListener('click', async () => {
  saving = true; controls();
  try {
    const blob = ready ? new Blob([await capture()]) : savedRecord.data;
    const extension = !ready && savedRecord.encoding === 'gzip' ? '.snap.gz' : '.snap';
    download(blob, `iphone-${new Date().toISOString().slice(0,10)}${extension}`);
    track('backup exported');
    setStatus('Backup downloaded. Keep it somewhere safe.');
  } catch (error) { track('backup export failed', {error_type:error?.name || 'Error'}); setStatus(`Export failed: ${error.message}`); }
  finally { saving = false; controls(); }
});
$('pause').addEventListener('click', async () => {
  try {
    await releaseInputs(); paused = !paused;
    await call('pause', {value:paused}); controls();
    track(paused ? 'emulator paused' : 'emulator resumed');
    paused ? setStatus('Paused. Your iPhone will wait here.') : showRunning();
  } catch (error) { fail(error); }
});
$('import').addEventListener('change', async event => {
  const file = event.target.files[0]; event.target.value = '';
  if (!file || !ready || !profileOwned) return;
  saving = true; controls();
  try {
    if (file.size > 512 * 1048576) throw new Error('Backup exceeds the 512 MB import limit');
    let buffer = await file.arrayBuffer();
    if (new Uint8Array(buffer)[0] === 0x1f && new Uint8Array(buffer)[1] === 0x8b) {
      const reader = new Blob([buffer]).stream().pipeThrough(new DecompressionStream('gzip')).getReader();
      const chunks = []; let length = 0;
      try { for (;;) {
        const {done,value} = await reader.read(); if (done) break;
        length += value.length;
        if (length > 512 * 1048576) throw new Error('Expanded backup exceeds the 512 MB import limit');
        chunks.push(value);
      } } finally { await reader.cancel(); }
      buffer = await new Blob(chunks).arrayBuffer();
    }
    await releaseInputs();
    await writeOwnedCheckpoint(await capture());
    const restored = await call('restore', {buffer}, [buffer]);
    silent = Boolean(restored.silent);
    document.querySelector('[data-toggle]').setAttribute('aria-pressed', String(silent));
    lastSave = await writeOwnedCheckpoint(await capture()); savedRecord = await readCheckpoint();
    track('backup imported');
    setStatus('Your backup is restored.');
  } catch (error) { track('backup import failed', {error_type:error?.name || 'Error'}); setStatus(`Import failed: ${describeError(error)}. Your previous save is retained.`); }
  finally { saving = false; controls(); }
});
$('reset').addEventListener('click', async () => {
  if (!profileOwned || !confirm('Reset this phone and delete its browser saves? Export first to keep a backup.')) return;
  saving = true; controls();
  try { await clearCheckpoints(); track('phone reset'); savedRecord = null; lastSave = 0; await start(); }
  catch (error) { fail(error); }
  finally { saving = false; controls(); }
});
$('retry').addEventListener('click', () => { track('emulator retry requested'); start().catch(fail); });
$('recover').addEventListener('click', async () => {
  try {
    const previous = await readCheckpoint('previous');
    if (!previous) throw new Error('There is no earlier saved checkpoint. Export the current save before resetting.');
    track('previous save restored');
    await start(previous);
  } catch (error) { fail(error); }
});
setInterval(() => save(false), 30000);
document.addEventListener('visibilitychange', () => {
  if (!ready) return;
  releaseInputs().catch(() => {});
  if (document.hidden) { save(false); call('pause', {value:true}).catch(() => {}); }
  else call('pause', {value:paused}).catch(fail);
});
window.iphone = {get ready() { return ready; }, get paused() { return paused; },
  get metrics() { return {...metrics, captureMs, captureChunkMs, finalizeMs}; }, get profileOwned() { return profileOwned; },
  get savedAt() { return lastSave; }};
try { profileOwned = await acquireProfile(); } catch { profileOwned = false; }
start().catch(fail);
