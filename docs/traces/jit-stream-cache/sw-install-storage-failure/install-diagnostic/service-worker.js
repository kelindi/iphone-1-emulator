// Generated with the complete bundle's content hash by tools/build-offline.py.
// Never activate an update over a running phone: its JS and WASM must agree.
const CACHE = 'iphone-1-shell-e00cd86dac86a6958b17aa93';
const FILES = {"index.html": "592aed2a0db86c39fb5a892a6f738f1f8c0d1cc6122267476c2ae0540b0b5a5b", "phone.css": "e6330f7313c08c2679d8fbe8e9923ea8c0a84cee8f38979b2ae95153a0456fc0", "icon.svg": "fa2f3b0f47103f5d1b9f501eb48f57b4f3f6eb9c029948dc9f8692545f4a07aa", "app.js": "b84f29e0107bf2f482628d5e47a89e8ad6a33ac4d4e0877207e526ca21bb3ba2", "storage.js": "5597ab191281d9baa1f37dba5f8208a9e6a5ad5230c41f732e50f4ccb8daf9f2", "checkpoint-worker.js": "16630c38df0adad3e37adcaf4454b4e97996db326cbc8f6b21ee460c8e709a58", "assets.js": "dc9207bba2fdd5749790ac913df8eab79544af52a4b4fde975e494cb1935a952", "emu-worker.js": "19b2020662604a34fdc685f478b753327ef3d4920f154088be3eda8133b4fb65", "manifest.json": "7cd45549d310f17808c261f0ed97a05b164bc5df6f4eecab66bafccdd5058fcc", "dist/emu.js": "a3c73a4d064c15bbd4c1de9797a88addb2e595a686f2eaf1a66e8b6c0c29a6b2", "dist/emu.wasm": "b3237ef3843412aea164d3db111299650ea8433bf16311417116799959ab6606", "seed-jit.js": "3c2cc4122a9b32ab22db44d253e26920650c885c1c019b170a2951002ce597e9", "jit/seed.wasm": "d9c661556ab17bb8433ed9b0c3ecfd52e2fcafe58221b4cb84a920e4976b3881", "jit/regions.json": "c9d097ee798b3536e98fffe2f11b6aa408567ecc3e0a88e9250726f0eb640da9", "jit/abi.json": "78842488106b0f8912201561e4dbebcd0e61a8beaa1976f8785cfa82247ea698"};
const urls = Object.keys(FILES).map(path => new URL(path, self.registration.scope).href);
self.addEventListener('install', event => event.waitUntil((async () => {
  const cache = await caches.open(CACHE);
  try {
    for (const [path, expected] of Object.entries(FILES)) {
      const url = new URL(path, self.registration.scope).href;
      const response = await fetch(url, {cache:'reload'});
      if (!response.ok) throw new Error(`Offline file unavailable: ${path}`);
      const bytes = await response.clone().arrayBuffer();
      const hash = [...new Uint8Array(await crypto.subtle.digest('SHA-256', bytes))]
        .map(n => n.toString(16).padStart(2,'0')).join('');
      if (hash !== expected) throw new Error(`Offline bundle changed during installation: ${path}`);
      await cache.put(url, response);
    }
  }
  catch (error) { for (const client of await self.clients.matchAll({includeUncontrolled:true})) client.postMessage({installError:{name:error.name,message:error.message,stack:error.stack}}); await caches.delete(CACHE); throw error; }
})()));
self.addEventListener('activate', event => event.waitUntil((async () => {
  for (const name of await caches.keys())
    if (name.startsWith('iphone-1-shell-') && name !== CACHE) await caches.delete(name);
  await self.clients.claim();
})()));
self.addEventListener('fetch', event => {
  if (event.request.method !== 'GET') return;
  const url = new URL(event.request.url);
  const scope = new URL(self.registration.scope);
  if (url.origin !== scope.origin || !url.pathname.startsWith(scope.pathname)) return;
  // Query parameters select emulator options, never a different HTML bundle.
  const path = url.pathname === scope.pathname ? new URL('index.html', scope).href : url.origin + url.pathname;
  if (!urls.includes(path)) return;
  event.respondWith((async () => (await (await caches.open(CACHE)).match(path)) || fetch(event.request))());
});
