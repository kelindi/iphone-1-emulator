// Generated with the complete bundle's content hash by tools/build-offline.py.
// Never activate an update over a running phone: its JS and WASM must agree.
const CACHE = 'iphone-1-shell-fe9da99cc4bda14564e9a1fb';
const FILES = {"index.html": "c84736e55d5f8d86626fec743517c581536a59423b22c5d038b00f5e65ab8ff7", "phone.css": "c0c8c1a30bf7975a079523f7698d2dd08bc07dc581294157d49540be70cca00a", "icon.svg": "fa2f3b0f47103f5d1b9f501eb48f57b4f3f6eb9c029948dc9f8692545f4a07aa", "app.js": "155171619c0b57120b5f2db086303fa924d9c75fbe390b1e8c23017de7569500", "analytics.js": "b8abd26f2240c02176427c265959b3bd3db9020e8fb53d1dd53d84677e9f1013", "storage.js": "5597ab191281d9baa1f37dba5f8208a9e6a5ad5230c41f732e50f4ccb8daf9f2", "checkpoint-worker.js": "16630c38df0adad3e37adcaf4454b4e97996db326cbc8f6b21ee460c8e709a58", "assets.js": "57239124797b93f1ce026c797a9f83137de1750decbe68acb1c3484c27686c8d", "sha256.js": "88562060586dd595d47e0c70225eb09ce54a9678bdd35bebdf27a2d9b41c3ee0", "emu-worker.js": "9fd5274088b29ab076b288878c960205315423684a8487af4423e2e0c7206403", "manifest.json": "b25f9b40b90b5bb5bfa2ecc27c440298cb1d1fd574992ebe9e9bd427779ecf3a", "dist/emu.js": "0cbde693f7389512766da416bd586bff2ed123afce4e9967dc0e25f4274d7c8b", "dist/emu.wasm": "b7665d306c33590fe11b12c18377f40fb57f6ecacbd97728701f3bdfffcefff4", "seed-jit.js": "3052d755885278c064cc31df4c7f6c49ee2b7de5d9855f6b042130055e331c7a", "jit/seed.wasm": "d2f556b579ed492676510cc156f0c7f481ed6d00b6e4a2d0a4bfb1795e458b4b", "jit/regions.json": "4f30941218b20418e2ec1386d19c5ffa0bfb256a3855df66cb7d8348a77a5c69", "jit/abi.json": "d60d8f33223c2ebba87bca3a7ca473288c15cf7f21a825098201b2f00640d2d8"};
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
