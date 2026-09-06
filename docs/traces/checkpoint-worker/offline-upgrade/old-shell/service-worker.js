// Generated with the complete bundle's content hash by tools/build-offline.py.
// Never activate an update over a running phone: its JS and WASM must agree.
const CACHE = 'iphone-1-shell-9f4dbb3e5ab97a91138875d9';
const FILES = {"index.html": "230ef7d151d8f0eba3cd81870c9b6ecd6e0e6051b498df6fa8d7176d23d63608", "phone.css": "a10b70ceace9b945c37bc4d69cbedd27ffe24687d0fb60cd72c8ecac9846838e", "icon.svg": "fa2f3b0f47103f5d1b9f501eb48f57b4f3f6eb9c029948dc9f8692545f4a07aa", "app.js": "49eeeb6e03fe1a954e35c3e6d032105f8c9da43d46923d1236b31b804e547804", "storage.js": "1a8b05e60445f54cebf06142be71ce2df588297330b12697f868b8b15ef76f24", "assets.js": "dc9207bba2fdd5749790ac913df8eab79544af52a4b4fde975e494cb1935a952", "emu-worker.js": "931332f99b31bdd4652a5c2afe0b30e6f25ee72790e648be3b3738adc3a41ba3", "manifest.json": "7cd45549d310f17808c261f0ed97a05b164bc5df6f4eecab66bafccdd5058fcc", "dist/emu.js": "a3c73a4d064c15bbd4c1de9797a88addb2e595a686f2eaf1a66e8b6c0c29a6b2", "dist/emu.wasm": "603679fbc7961ddad95ac8822257074fecde5940bc013246d520ea3b6240c84f", "seed-jit.js": "3ddac5082690e4ea0f0e710105ccbc7ec1a2f1afa6da74986b0c56691f173d2f", "jit/seed.wasm": "d9c661556ab17bb8433ed9b0c3ecfd52e2fcafe58221b4cb84a920e4976b3881", "jit/regions.json": "c9d097ee798b3536e98fffe2f11b6aa408567ecc3e0a88e9250726f0eb640da9", "jit/abi.json": "78842488106b0f8912201561e4dbebcd0e61a8beaa1976f8785cfa82247ea698"};
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
  catch (error) { await caches.delete(CACHE); throw error; }
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
