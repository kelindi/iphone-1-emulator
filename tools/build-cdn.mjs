/** Opt-in split deployment. Never changes the checked-in web/ bundle or uploads files. */
import {readFile, writeFile, mkdir, access} from 'node:fs/promises';
import {resolve, dirname, join, sep} from 'node:path';
import {fileURLToPath, pathToFileURL} from 'node:url';
import {createHash} from 'node:crypto';

const repo = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const digest = bytes => createHash('sha256').update(bytes).digest('hex');
const remoteFiles = ['phone.css', 'icon.svg', 'og-image.png', 'app.js', 'analytics.js',
  'storage.js', 'checkpoint-worker.js', 'assets.js', 'sha256.js', 'emu-worker.js',
  'seed-jit.js', 'dist/emu.js', 'dist/emu.wasm', 'jit/seed.wasm', 'jit/regions.json', 'jit/abi.json'];

export function validateAssetBase(value) {
  const url = new URL(value);
  const local = ['localhost', '127.0.0.1', '[::1]'].includes(url.hostname);
  if (!(url.protocol === 'https:' || (url.protocol === 'http:' && local)) ||
      url.username || url.password || url.search || url.hash)
    throw new Error('Asset base must be HTTPS (or loopback HTTP), without credentials, query or fragment');
  if (!url.pathname.endsWith('/')) url.pathname += '/';
  return url.href;
}
function replaceOnce(text, before, after, name) {
  if (text.split(before).length !== 2) throw new Error(`Source changed: expected one ${name}`);
  return text.replace(before, after);
}
export async function buildCdn({assetBase, out, source = join(repo, 'web')}) {
  const base = validateAssetBase(assetBase);
  source = resolve(source); out = resolve(out);
  if (out === source || out.startsWith(source + sep))
    throw new Error('Output must be outside the source web directory');
  // Refuse existing output rather than deleting or overwriting a user's files.
  try { await access(out); throw new Error(`Output already exists: ${out}`); }
  catch (error) { if (error.code !== 'ENOENT') throw error; }
  const sources = new Map(await Promise.all([...remoteFiles, 'index.html', 'manifest.json',
    'service-worker.template.js'].map(async name => [name, await readFile(join(source, name))])));
  const hash = createHash('sha256').update(await readFile(fileURLToPath(import.meta.url))).update(base);
  for (const [name, bytes] of sources) hash.update(name).update(bytes);
  const version = hash.digest('hex').slice(0, 24);
  const prefix = `releases/${version}/`;
  const remoteBase = new URL(prefix, base).href;
  const asset = name => new URL(name, remoteBase).href;
  const remote = new Map(remoteFiles.map(name => [name, sources.get(name)]));
  const change = (name, before, after) => remote.set(name, Buffer.from(replaceOnce(
    remote.get(name).toString(), before, after, `${name}: ${before}`)));
  // Worker constructors must stay on the page's origin. Their tiny entry points
  // import the implementation from the immutable release; no blob worker needed.
  change('app.js', "new URL('./emu-worker.js', import.meta.url)", "new URL('./emu-worker.js', location.href)");
  change('storage.js', "new URL('./checkpoint-worker.js', import.meta.url)",
    "new URL('./checkpoint-worker.js', location.href)");
  change('emu-worker.js', "importScripts('dist/emu.js', 'seed-jit.js');",
    `importScripts(${JSON.stringify(asset('dist/emu.js'))}, ${JSON.stringify(asset('seed-jit.js'))});`);
  change('emu-worker.js', "import('./assets.js')", `import(${JSON.stringify(asset('assets.js'))})`);
  change('emu-worker.js', 'new URL(`dist/${path}`, location.href).href',
    `new URL(path, ${JSON.stringify(asset('dist/'))}).href`);
  change('seed-jit.js', 'fetch(path, {signal: seedAbort.signal})',
    `fetch(new URL(path, ${JSON.stringify(remoteBase)}), {signal: seedAbort.signal})`);

  let html = sources.get('index.html').toString();
  for (const [attr, name] of [['href', 'phone.css'], ['href', 'icon.svg'], ['src', 'app.js']])
    html = replaceOnce(html, `${attr}="${name}"`, `${attr}="${asset(name)}"`, `index ${name}`);
  html = html.replaceAll('https://2007.mobile/og-image.png', asset('og-image.png'));
  const site = new Map([
    ['index.html', Buffer.from(html)], ['manifest.json', sources.get('manifest.json')],
    ['emu-worker.js', Buffer.from(`// Same-origin classic worker entry; query options remain on location.href.\nimportScripts(${JSON.stringify(asset('emu-worker.js'))});\n`)],
    ['checkpoint-worker.js', Buffer.from(`// Same-origin module worker entry.\nimport ${JSON.stringify(asset('checkpoint-worker.js'))};\n`)]
  ]);
  for (const name of ['robots.txt', 'sitemap.xml']) {
    try { site.set(name, await readFile(join(source, name))); }
    catch (error) { if (error.code !== 'ENOENT') throw error; }
  }
  const hashes = Object.fromEntries([
    ...[...site].map(([name, bytes]) => [name, digest(bytes)]),
    ...[...remote].map(([name, bytes]) => [asset(name), digest(bytes)])
  ]);
  let sw = sources.get('service-worker.template.js').toString();
  sw = replaceOnce(sw,
    'if (url.origin !== scope.origin || !url.pathname.startsWith(scope.pathname)) return;',
    '// Cross-origin assets are served only when their exact URL is in this release manifest.',
    'offline origin guard');
  sw = replaceOnce(sw,
    "const path = url.pathname === scope.pathname ? new URL('index.html', scope).href : url.origin + url.pathname;",
    "const path = url.origin === scope.origin ? (url.pathname === scope.pathname ? new URL('index.html', scope).href : url.origin + url.pathname) : url.href;",
    'offline URL normalization');
  // Cloudflare can serve an encoded cross-origin response whose decoded body cannot
  // be put back into CacheStorage as the original response. The split worker reads
  // the verified bytes once and caches a reconstructed, decoded response instead.
  sw = replaceOnce(sw,
    'const bytes = await response.clone().arrayBuffer();',
    'const bytes = await response.arrayBuffer();',
    'split worker verified body read');
  sw = replaceOnce(sw,
    '      await cache.put(url, response);',
    `      const cachedHeaders = new Headers(response.headers);
      cachedHeaders.delete('content-encoding');
      cachedHeaders.delete('content-length');
      await cache.put(url, new Response(bytes, {
        status: response.status,
        statusText: response.statusText,
        headers: cachedHeaders
      }));`,
    'split worker decoded cache response');
  sw = sw.replace('__VERSION__', version).replace('__FILES__', JSON.stringify(hashes));
  site.set('service-worker.js', Buffer.from(sw));
  site.set('vercel.json', Buffer.from(JSON.stringify({headers:[
    {source:'/(.*)', headers:[{key:'Cache-Control',value:'public, max-age=0, must-revalidate'}]},
    {source:'/service-worker.js',headers:[{key:'Cache-Control',value:'no-cache'}]}
  ]}, null, 2)+'\n'));
  for (const [root, entries] of [[join(out, 'site'), site], [join(out, 'assets', prefix), remote]]) {
    for (const [name, bytes] of entries) {
      const file = join(root, name); await mkdir(dirname(file), {recursive:true}); await writeFile(file, bytes);
    }
  }
  const report = {version, assetBase:base, remoteBase, site:'site', assets:'assets',
    remoteFiles:remote.size, remoteBytes:[...remote.values()].reduce((sum,b)=>sum+b.length,0),
    assetHeaders:{'Access-Control-Allow-Origin':'*', 'Cross-Origin-Resource-Policy':'cross-origin',
      'Cache-Control':'public, max-age=31536000, immutable'},
    sha256:hashes};
  await writeFile(join(out,'release.json'), JSON.stringify(report,null,2)+'\n');
  return report;
}
if (process.argv[1] && pathToFileURL(resolve(process.argv[1])).href === import.meta.url) {
  const args = process.argv.slice(2), options = {};
  for (let i=0; i<args.length; i+=2) {
    if (!['--asset-base','--out'].includes(args[i]) || !args[i+1])
      throw new Error('Usage: node tools/build-cdn.mjs --asset-base https://iphone.files.garden/ --out .context/cdn-build');
    options[args[i]] = args[i+1];
  }
  if (!options['--asset-base'] || !options['--out']) throw new Error('--asset-base and --out are required');
  console.log(JSON.stringify(await buildCdn({assetBase:options['--asset-base'],out:options['--out']}),null,2));
}
