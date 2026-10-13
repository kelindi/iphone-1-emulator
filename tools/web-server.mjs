import {createServer} from 'node:http';
import {createReadStream} from 'node:fs';
import {stat, realpath} from 'node:fs/promises';
import {resolve, extname, sep} from 'node:path';

// A local development server; never exposes files outside the chosen web root.
const root = await realpath(resolve(process.env.WEB_ROOT || 'web'));
const port = Number(process.env.PORT || process.env.CONDUCTOR_PORT || 8080);
const types = {'.html':'text/html', '.txt':'text/plain', '.xml':'application/xml', '.js':'text/javascript', '.mjs':'text/javascript',
  '.css':'text/css', '.json':'application/json', '.wasm':'application/wasm',
  '.svg':'image/svg+xml', '.png':'image/png', '.jpg':'image/jpeg', '.mp4':'video/mp4'};
createServer(async (req, res) => {
  try {
    if (req.method !== 'GET' && req.method !== 'HEAD') {
      res.writeHead(405); res.end(); return;
    }
    const pathname = decodeURIComponent(new URL(req.url, 'http://localhost').pathname);
    const file = await realpath(resolve(root, `.${pathname.endsWith('/') ? pathname + 'index.html' : pathname}`));
    if (!file.startsWith(root + sep)) { res.writeHead(403); res.end(); return; }
    const info = await stat(file);
    if (!info.isFile()) throw new Error('not a file');
    res.writeHead(200, {'Content-Type':types[extname(file)] || 'application/octet-stream',
      'Content-Length':info.size, 'Cache-Control':'no-cache', 'X-Content-Type-Options':'nosniff'});
    if (req.method === 'HEAD') res.end();
    else createReadStream(file).on('error', () => res.destroy()).pipe(res);
  } catch { res.writeHead(404); res.end('Not found'); }
}).listen(port, '0.0.0.0', () => {
  console.log(`iPhone at http://localhost:${port} (root ${root})`);
});
