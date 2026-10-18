// Generated with the complete bundle's content hash by tools/build-offline.py.
// Never activate an update over a running phone: its JS and WASM must agree.
const CACHE = 'iphone-1-shell-4fb2d053f4cb0a2f52af3994';
const FILES = {"index.html": "c84736e55d5f8d86626fec743517c581536a59423b22c5d038b00f5e65ab8ff7", "phone.css": "c0c8c1a30bf7975a079523f7698d2dd08bc07dc581294157d49540be70cca00a", "icon.svg": "fa2f3b0f47103f5d1b9f501eb48f57b4f3f6eb9c029948dc9f8692545f4a07aa", "app.js": "155171619c0b57120b5f2db086303fa924d9c75fbe390b1e8c23017de7569500", "analytics.js": "b8abd26f2240c02176427c265959b3bd3db9020e8fb53d1dd53d84677e9f1013", "storage.js": "5597ab191281d9baa1f37dba5f8208a9e6a5ad5230c41f732e50f4ccb8daf9f2", "checkpoint-worker.js": "16630c38df0adad3e37adcaf4454b4e97996db326cbc8f6b21ee460c8e709a58", "assets.js": "57239124797b93f1ce026c797a9f83137de1750decbe68acb1c3484c27686c8d", "sha256.js": "88562060586dd595d47e0c70225eb09ce54a9678bdd35bebdf27a2d9b41c3ee0", "emu-worker.js": "152e6d632868357cdc9a8aa7ed54e393baf87f111d1354a7a0493662a8b65ce4", "manifest.json": "b25f9b40b90b5bb5bfa2ecc27c440298cb1d1fd574992ebe9e9bd427779ecf3a", "dist/emu.js": "0cbde693f7389512766da416bd586bff2ed123afce4e9967dc0e25f4274d7c8b", "dist/emu.wasm": "b7665d306c33590fe11b12c18377f40fb57f6ecacbd97728701f3bdfffcefff4", "seed-jit.js": "79d7967c4545310d00d856ccfb334022d2b1335f183d48d5e3c9339adff086ed", "jit/seed.wasm": "36174f4d8793526a66e856e9a148842455a52f67fb062bf215b9b9e351d33e4a", "jit/regions.json": "f0e2b0348e9b12e1fc3c7a993aad47f9b1e101fda6b5ffadd70da699140e6e18", "jit/abi.json": "be87dcb4c5cef24f6f504d6be44b662469afddb91f78898a8738011995c74e6b"};
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
