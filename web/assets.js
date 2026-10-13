// Verify immutable firmware before caching or decoding it. A bad cached entry
// gets one network retry; storage denial never prevents loading the phone.
import {sha256} from './sha256.js';
const CACHE = 'iphone-1-assets-1a543a-v1';
const FIRMWARE_ORIGINS = new Set(['https://iphone.files.garden']);
const hex = bytes => [...new Uint8Array(bytes)].map(n => n.toString(16).padStart(2,'0')).join('');
export async function assetBytes(names, report = () => {}, hashes = {}) {
  const parts = Array.isArray(names) ? names : [names];
  if (!parts.length || parts.some(x => typeof x !== 'string')) throw new Error('Invalid firmware manifest');
  let cache;
  try { cache = await caches.open(CACHE); } catch {}
  const chunks = [];
  let received = 0;
  for (const name of parts) {
    const url = new URL(name, location.href);
    if (url.origin !== location.origin && !FIRMWARE_ORIGINS.has(url.origin))
      throw new Error('Firmware must come from an approved host');
    let cached;
    try { cached = await cache?.match(url.href); } catch {}
    for (let attempt = 0; attempt < 2; attempt++) {
      const fromCache = Boolean(cached);
      const response = cached || await fetch(url, {cache:attempt ? 'reload' : 'default'});
      if (!response.ok) throw new Error(`${name}: HTTP ${response.status}`);
      const reader = response.body.getReader(), pieces = [];
      let partLength = 0;
      for (;;) {
        const {done, value} = await reader.read();
        if (done) break;
        pieces.push(value); partLength += value.byteLength;
        report(received + partLength);
      }
      const buffer = await new Blob(pieces).arrayBuffer();
      const valid = !hashes[name] || hex(await sha256(buffer)) === hashes[name];
      if (!valid) {
        try { await cache?.delete(url.href); } catch {}
        cached = null;
        if (attempt === 0 && fromCache) continue;
        throw new Error(`${name}: integrity check failed. Retry the download.`);
      }
      if (!fromCache && cache) {
        try { await cache.put(url.href, new Response(buffer)); } catch {}
      }
      chunks.push(buffer); received += partLength; break;
    }
  }
  const blob = new Blob(chunks);
  if (/\.gz(?:\.part\d+)?$/.test(parts[0])) {
    return new Uint8Array(await new Response(blob.stream().pipeThrough(new DecompressionStream('gzip'))).arrayBuffer());
  }
  return new Uint8Array(await blob.arrayBuffer());
}
