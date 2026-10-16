// Generated with the complete bundle's content hash by tools/build-offline.py.
// Never activate an update over a running phone: its JS and WASM must agree.
const CACHE = 'iphone-1-shell-4bed542b470f342c2e32eff1';
const FILES = {"index.html": "c84736e55d5f8d86626fec743517c581536a59423b22c5d038b00f5e65ab8ff7", "phone.css": "c0c8c1a30bf7975a079523f7698d2dd08bc07dc581294157d49540be70cca00a", "icon.svg": "fa2f3b0f47103f5d1b9f501eb48f57b4f3f6eb9c029948dc9f8692545f4a07aa", "app.js": "a1c5ea794a0375a04b91450c4bb3a8a29b72f4eacddd14b52acf610915916835", "analytics.js": "b8abd26f2240c02176427c265959b3bd3db9020e8fb53d1dd53d84677e9f1013", "storage.js": "5597ab191281d9baa1f37dba5f8208a9e6a5ad5230c41f732e50f4ccb8daf9f2", "checkpoint-worker.js": "16630c38df0adad3e37adcaf4454b4e97996db326cbc8f6b21ee460c8e709a58", "assets.js": "57239124797b93f1ce026c797a9f83137de1750decbe68acb1c3484c27686c8d", "sha256.js": "88562060586dd595d47e0c70225eb09ce54a9678bdd35bebdf27a2d9b41c3ee0", "emu-worker.js": "57fff7998cf5e5ead22724bcbe3273c5fd843757862332d838654b1e23f22344", "manifest.json": "b25f9b40b90b5bb5bfa2ecc27c440298cb1d1fd574992ebe9e9bd427779ecf3a", "dist/emu.js": "ba708966e83720d215680103f6de4eb3005465f62771711cb49f84342682c3f3", "dist/emu.wasm": "60bed944ee6511484a8cb303125a0efb05998ce4f192408464887c5fbb97f975", "seed-jit.js": "fe9ff4333b0aea3e26ac2fb30aabec08536f600fc0b97b7e2a213f1c504b0da5", "jit/seed.wasm": "d9c661556ab17bb8433ed9b0c3ecfd52e2fcafe58221b4cb84a920e4976b3881", "jit/regions.json": "c9d097ee798b3536e98fffe2f11b6aa408567ecc3e0a88e9250726f0eb640da9", "jit/abi.json": "78842488106b0f8912201561e4dbebcd0e61a8beaa1976f8785cfa82247ea698"};
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
