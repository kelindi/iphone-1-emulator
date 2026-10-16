// Generated with the complete bundle's content hash by tools/build-offline.py.
// Never activate an update over a running phone: its JS and WASM must agree.
const CACHE = 'iphone-1-shell-d161171c3d28f4d2412b7b6d';
const FILES = {"index.html": "6383584cf69ab6c8855d62143ec66958c8a794556aca1dc70f4541bcc08ac24e", "phone.css": "9ffb1a2eafc395ec2cdb41ca6c7a6b97ee7faa58cebee72cd2a294000cce4a10", "icon.svg": "fa2f3b0f47103f5d1b9f501eb48f57b4f3f6eb9c029948dc9f8692545f4a07aa", "app.js": "6d8651098cb496d06e795cc72aa2dbcc704847b3868503ed3fb9f090cd3a2b3e", "analytics.js": "b8abd26f2240c02176427c265959b3bd3db9020e8fb53d1dd53d84677e9f1013", "storage.js": "5597ab191281d9baa1f37dba5f8208a9e6a5ad5230c41f732e50f4ccb8daf9f2", "checkpoint-worker.js": "16630c38df0adad3e37adcaf4454b4e97996db326cbc8f6b21ee460c8e709a58", "assets.js": "57239124797b93f1ce026c797a9f83137de1750decbe68acb1c3484c27686c8d", "sha256.js": "88562060586dd595d47e0c70225eb09ce54a9678bdd35bebdf27a2d9b41c3ee0", "emu-worker.js": "37c9952f6f1473c7dddb895128bb8439ac1e781292af84b9691fe0b90780b5eb", "manifest.json": "b25f9b40b90b5bb5bfa2ecc27c440298cb1d1fd574992ebe9e9bd427779ecf3a", "firmwares.json": "81f8af7d2e22503f0dc93c96ddbb44b9e75da8c317d0512fef78eb14531d902f", "dist/emu.js": "ed356e356d00ca4fc6c5c38ac04f962262c2d1a27504f6bbf9721b6201cbb041", "dist/emu.wasm": "bac0f41c23cf201e4d3a602652991a8d6e379e2ff0ce1471f8bee9990065a91d", "seed-jit.js": "83cd519074ce870ea6affb92fe6d62806e634b7b7fbf3eb1f5210f9f49978a54", "jit/seed.wasm": "d9c661556ab17bb8433ed9b0c3ecfd52e2fcafe58221b4cb84a920e4976b3881", "jit/regions.json": "c9d097ee798b3536e98fffe2f11b6aa408567ecc3e0a88e9250726f0eb640da9", "jit/abi.json": "78842488106b0f8912201561e4dbebcd0e61a8beaa1976f8785cfa82247ea698"};
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
