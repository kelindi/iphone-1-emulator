export const requiredAssets = ['index.html','phone.css','app.js','assets.js','storage.js',
  'checkpoint-worker.js','emu-worker.js','seed-jit.js','manifest.json','icon.svg',
  'dist/emu.js','dist/emu.wasm','jit/abi.json','jit/regions.json','jit/seed.wasm'];

export function validateResponses(rows, pins, origin) {
  const seen = new Set(); let accepted = 0;
  for (const row of rows) {
    if (typeof row.url !== 'string') throw Error('Response URL must be a string');
    const url = new URL(row.url);
    if (url.origin !== origin) continue;
    const path = url.pathname === '/' ? 'index.html' : decodeURIComponent(url.pathname.slice(1));
    const pin = pins[path];
    if (!pin || row.status !== 200 || row.bodyError) throw Error('Unexpected or unreadable local response: '+path);
    if (row.bodyBytes !== pin.bytes || row.bodySHA256 !== pin.sha256) throw Error('Local response bytes differ: '+path);
    seen.add(path); accepted++;
  }
  for (const name of requiredAssets) if (!seen.has(name)) throw Error('Missing actual served asset: '+name);
  return {passed:true,requiredAssets:[...requiredAssets],acceptedLocalResponses:accepted,
    scope:'Actual received local response bodies match frozen source bytes; service-worker installation and offline transport are not claimed.'};
}
