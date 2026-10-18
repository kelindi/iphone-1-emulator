// Generated with the complete bundle's content hash by tools/build-offline.py.
// Never activate an update over a running phone: its JS and WASM must agree.
const CACHE = 'iphone-1-shell-95dc29d5647e95d08e4c1e41';
const FILES = {"index.html": "c84736e55d5f8d86626fec743517c581536a59423b22c5d038b00f5e65ab8ff7", "phone.css": "c0c8c1a30bf7975a079523f7698d2dd08bc07dc581294157d49540be70cca00a", "icon.svg": "fa2f3b0f47103f5d1b9f501eb48f57b4f3f6eb9c029948dc9f8692545f4a07aa", "app.js": "24018eaaed740436b19a0333747b45a3fee4a7abfc6502d370227e764477eb9f", "analytics.js": "b8abd26f2240c02176427c265959b3bd3db9020e8fb53d1dd53d84677e9f1013", "storage.js": "5597ab191281d9baa1f37dba5f8208a9e6a5ad5230c41f732e50f4ccb8daf9f2", "checkpoint-worker.js": "16630c38df0adad3e37adcaf4454b4e97996db326cbc8f6b21ee460c8e709a58", "assets.js": "57239124797b93f1ce026c797a9f83137de1750decbe68acb1c3484c27686c8d", "sha256.js": "88562060586dd595d47e0c70225eb09ce54a9678bdd35bebdf27a2d9b41c3ee0", "emu-worker.js": "152e6d632868357cdc9a8aa7ed54e393baf87f111d1354a7a0493662a8b65ce4", "manifest.json": "b25f9b40b90b5bb5bfa2ecc27c440298cb1d1fd574992ebe9e9bd427779ecf3a", "dist/emu.js": "211716d0b5b3d8ea32d9a240d3a6221aaf0ed9a68c9a9ef8bfeaa5dec4df26f2", "dist/emu.wasm": "38fb0d3318eacf9060ca02fdbdf90ef93c31d5aa9c8649a27b9a25af989d87fb", "seed-jit.js": "dc4195a291feca101797688c1d3cf270270dbb63609a8edb3a45201d62163d9a", "jit/seed.wasm": "e0edc8a38a437fca168b6e73adcb0e50e8f4f6ec62fc77c53b51f07ff096f9d1", "jit/regions.json": "5b2c2f066b7ab76da50404833b7ebb34e04f5dc9302add2f3a04aa0a609e0f13", "jit/abi.json": "8df66f8c354c8fd189cfb0e876f7401e8f4ebd212309ad2e946b427580a95f84"};
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
