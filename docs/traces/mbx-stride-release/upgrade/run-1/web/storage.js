export const PROFILE_VERSION = 'm68-1a543a-snapshot39';
const DB_NAME = 'iphone-1';
let database;
const MAX_CHECKPOINT_BYTES = 512 * 1048576;
const COMPRESSION_CHUNK_BYTES = 1048576;

function request(req) {
  return new Promise((resolve, reject) => {
    req.onsuccess = () => resolve(req.result);
    req.onerror = () => reject(req.error);
  });
}
async function db() {
  if (database) return database;
  const req = indexedDB.open(DB_NAME, 1);
  req.onupgradeneeded = () => req.result.createObjectStore('checkpoints');
  database = await request(req);
  database.onversionchange = () => { database.close(); database = undefined; };
  return database;
}
export async function readCheckpoint(generation = 'current') {
  const connection = await db();
  const result = await request(connection.transaction('checkpoints').objectStore('checkpoints').get(generation));
  return result || null;
}
/** Expand only when the emulator needs bytes. Existing records without an
 * encoding field contain raw v39 snapshots and remain readable. */
export async function checkpointBytes(record) {
  if (!record?.data || typeof record.data.arrayBuffer !== 'function')
    throw new Error('Saved checkpoint has no readable data');
  const encoding = record.encoding || 'raw';
  if (encoding === 'raw') {
    if (record.data.size > MAX_CHECKPOINT_BYTES)
      throw new Error('Saved checkpoint exceeds the 512 MB limit');
    return record.data.arrayBuffer();
  }
  if (encoding !== 'gzip') throw new Error(`Unsupported checkpoint encoding: ${encoding}`);
  if (typeof DecompressionStream !== 'function')
    throw new Error('This browser cannot open compressed saves. Export the backup and use a browser with gzip support.');
  // New compressed records include the raw size. Allocate once instead of
  // accumulating expanded chunks and duplicating another whole guest image.
  if (!Number.isSafeInteger(record.size) || record.size < 1 || record.size > MAX_CHECKPOINT_BYTES)
    throw new Error('Saved checkpoint has an invalid expanded size');
  const output = new Uint8Array(record.size);
  const reader = record.data.stream().pipeThrough(new DecompressionStream('gzip')).getReader();
  let length = 0;
  try {
    for (;;) {
      const {done,value} = await reader.read();
      if (done) break;
      if (length + value.length > output.length)
        throw new Error('Saved checkpoint expands beyond its recorded size');
      output.set(value, length); length += value.length;
    }
    if (length !== output.length) throw new Error('Saved checkpoint expanded size does not match its metadata');
  } finally { await reader.cancel().catch(() => {}); }
  return output.buffer;
}

export async function encodeCheckpoint(buffer) {
  if (!(buffer instanceof ArrayBuffer) || !buffer.byteLength || buffer.byteLength > MAX_CHECKPOINT_BYTES)
    throw new Error('Checkpoint must contain between 1 byte and 512 MB');
  const began = performance.now();
  if (typeof CompressionStream !== 'function' || typeof DecompressionStream !== 'function')
    return {encoding:'raw', data:new Blob([buffer], {type:'application/octet-stream'}), compressionMs:0};
  let offset = 0;
  // Stream typed-array views directly: creating a temporary 233 MB Blob can
  // itself hit Chrome private-mode Blob memory limits before compression.
  const source = new ReadableStream({
    pull(controller) {
      if (offset === buffer.byteLength) { controller.close(); return; }
      const length = Math.min(COMPRESSION_CHUNK_BYTES, buffer.byteLength - offset);
      controller.enqueue(new Uint8Array(buffer, offset, length));
      offset += length;
    }
  });
  const data = await new Response(source.pipeThrough(new CompressionStream('gzip'))).blob();
  return {encoding:'gzip', data, compressionMs:performance.now() - began};
}

// A captured snapshot belongs exclusively to the save operation. This explicit
// path may detach it; callers needing to reuse their input use writeCheckpoint.
const WORKER_READY_MS = 5000;
const WORKER_ENCODE_MS = 120000;
async function encodeOwnedCheckpoint(buffer) {
  const size = buffer.byteLength;
  if (typeof Worker !== 'function') return encodeCheckpoint(buffer);
  let worker;
  try {
    return await new Promise((resolve, reject) => {
      let sent = false, settled = false;
      let timer;
      const finish = (error, result) => {
        if (settled) return;
        settled = true;
        clearTimeout(timer);
        error ? reject(error) : resolve(result);
      };
      worker = new Worker(new URL('./checkpoint-worker.js', import.meta.url),
        {type:'module', name:'iPhone save compression'});
      timer = setTimeout(() => finish(new Error('Save compression worker did not start')), WORKER_READY_MS);
      worker.onerror = event => {
        event.preventDefault?.();
        finish(new Error(event.message || 'Save compression worker failed'));
      };
      worker.onmessageerror = () => finish(new Error('Save compression worker returned unreadable data'));
      worker.onmessage = ({data}) => {
        if (settled) return;
        if (!sent && data?.type === 'ready' && data.version === 1) {
          clearTimeout(timer);
          timer = setTimeout(() => finish(new Error('Save compression timed out; please save again')), WORKER_ENCODE_MS);
          sent = true;
          try { worker.postMessage({type:'encode', buffer}, [buffer]); }
          catch (error) { finish(error); }
          return;
        }
        if (sent && data?.type === 'encoded' && data.rawSize === size) {
          const result = data.encoded;
          if ((result?.encoding === 'gzip' || result?.encoding === 'raw') &&
              result.data instanceof Blob && result.data.size > 0 &&
              result.data.size <= MAX_CHECKPOINT_BYTES + COMPRESSION_CHUNK_BYTES &&
              (result.encoding !== 'raw' || result.data.size === size) &&
              Number.isFinite(result.compressionMs) && result.compressionMs >= 0) {
            finish(null, {encoding:result.encoding, data:result.data, compressionMs:result.compressionMs});
            return;
          }
        }
        finish(new Error(data?.type === 'error' && typeof data.message === 'string'
          ? data.message : 'Save compression worker returned invalid data'));
      };
    });
  } catch (error) {
    worker?.terminate();
    worker = undefined;
    // Startup/transport failure can use the existing codec only while we still
    // own the bytes. Once transferred, reject without touching earlier saves.
    if (buffer.byteLength === size) return encodeCheckpoint(buffer);
    throw error;
  } finally { worker?.terminate(); }
}

/** Save without consuming the caller's ArrayBuffer. */
export async function writeCheckpoint(buffer) {
  return commitCheckpoint(buffer, false);
}
/** Save a fresh, exclusively owned capture. The input may be detached, even if
 * saving fails. Retry with a new capture; prior generations remain intact. */
export async function writeOwnedCheckpoint(buffer) {
  return commitCheckpoint(buffer, true);
}
async function commitCheckpoint(buffer, owned) {
  if (!(buffer instanceof ArrayBuffer) || !buffer.byteLength || buffer.byteLength > MAX_CHECKPOINT_BYTES)
    throw new Error('Checkpoint must contain between 1 byte and 512 MB');
  const size = buffer.byteLength;
  // Complete encoding before opening the atomic two-generation transaction.
  const encoded = await (owned ? encodeOwnedCheckpoint(buffer) : encodeCheckpoint(buffer));
  const connection = await db();
  // Compressed payload and metadata commit together. Any failed/quota-limited
  // transaction leaves both earlier complete generations intact.
  return new Promise((resolve, reject) => {
    const tx = connection.transaction('checkpoints', 'readwrite');
    const store = tx.objectStore('checkpoints');
    const old = store.get('current');
    const savedAt = Date.now();
    old.onsuccess = () => {
      if (old.result) store.put(old.result, 'previous');
      store.put({version:PROFILE_VERSION, savedAt, size,
        storedSize:encoded.data.size, ...encoded}, 'current');
    };
    tx.oncomplete = () => resolve(savedAt);
    tx.onabort = () => reject(tx.error || new Error('Save was interrupted'));
    tx.onerror = () => {}; // onabort reports the terminal transaction failure.
  });
}
export async function clearCheckpoints() {
  const connection = await db();
  return new Promise((resolve, reject) => {
    const tx = connection.transaction('checkpoints', 'readwrite');
    tx.objectStore('checkpoints').clear();
    tx.oncomplete = resolve;
    tx.onabort = () => reject(tx.error || new Error('Reset failed'));
  });
}
export async function acquireProfile() {
  if (!navigator.locks) return false;
  return new Promise((resolve, reject) => {
    navigator.locks.request('iphone-1-profile', {ifAvailable:true}, lock => {
      resolve(Boolean(lock));
      // A tab owns the profile until it closes. Other tabs can explore, but
      // must not overwrite this tab's phone or reset its saves.
      if (lock) return new Promise(() => {});
    }).catch(reject);
  });
}
