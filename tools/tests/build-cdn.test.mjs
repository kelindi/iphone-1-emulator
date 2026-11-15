import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtemp, readFile, rm, readdir} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {createHash} from 'node:crypto';
import {buildCdn, validateAssetBase} from '../build-cdn.mjs';

test('asset base requires secure explicit origin; loopback is allowed for local tests', () => {
  assert.equal(validateAssetBase('https://iphone.files.garden/emulator'), 'https://iphone.files.garden/emulator/');
  assert.equal(validateAssetBase('http://127.0.0.1:8081'), 'http://127.0.0.1:8081/');
  for (const base of ['http://example.com/', 'javascript:alert(1)', 'https://user:pw@example.com/',
    'https://example.com/?redirect=x', 'https://example.com/#x']) assert.throws(()=>validateAssetBase(base));
});
test('split build is deterministic, immutable and preserves same-origin worker/profile ownership', async () => {
  const temp = await mkdtemp(join(tmpdir(),'iphone-cdn-'));
  try {
    const a = await buildCdn({assetBase:'https://iphone.files.garden/',out:join(temp,'a')});
    const b = await buildCdn({assetBase:'https://iphone.files.garden/',out:join(temp,'b')});
    assert.deepEqual(a,b);
    const read = (side,name)=>readFile(join(temp,'a',side,name),'utf8');
    const prefix = `releases/${a.version}/`;
    assert.match(await read('assets',prefix+'app.js'), /new URL\('\.\/emu-worker.js', location.href\)/);
    assert.match(await read('assets',prefix+'storage.js'), /new URL\('\.\/checkpoint-worker.js', location.href\)/);
    assert.match(await read('site','emu-worker.js'), /importScripts\("https:\/\/iphone.files.garden\/releases\//);
    assert.match(await read('site','checkpoint-worker.js'), /import "https:\/\/iphone.files.garden\/releases\//);
    assert.match(await read('assets',prefix+'emu-worker.js'), /fetch\('manifest.json'/);
    assert.match(await read('assets',prefix+'seed-jit.js'), /fetch\(new URL\(path, "https:/);
    const serviceWorker = await read('site','service-worker.js');
    assert.match(serviceWorker, /if \(!urls.includes\(path\)\) return/);
    assert.match(serviceWorker, /const bytes = await response\.arrayBuffer\(\);/);
    assert.doesNotMatch(serviceWorker, /response\.clone\(\)\.arrayBuffer\(\)/);
    assert.match(serviceWorker, /const cachedHeaders = new Headers\(response\.headers\);/);
    assert.match(serviceWorker, /cachedHeaders\.delete\('content-encoding'\);/);
    assert.match(serviceWorker, /cachedHeaders\.delete\('content-length'\);/);
    assert.match(serviceWorker, /await cache\.put\(url, new Response\(bytes, \{\n        status: response\.status,\n        statusText: response\.statusText,\n        headers: cachedHeaders\n      \}\)\);/);
    assert.doesNotMatch(serviceWorker, /await cache\.put\(url, response\);/);
    assert.doesNotMatch(serviceWorker, /skipWaiting|__FILES__|__VERSION__/);
    assert.equal(await read('site','manifest.json'), await readFile(new URL('../../web/manifest.json',import.meta.url),'utf8'));
    assert(! (await readdir(join(temp,'a','site'))).includes('dist'));
    for (const [name, hash] of Object.entries(a.sha256)) {
      const file = name.startsWith(a.remoteBase) ? join(temp,'a','assets',prefix,name.slice(a.remoteBase.length)) : join(temp,'a','site',name);
      assert.equal(createHash('sha256').update(await readFile(file)).digest('hex'),hash,name);
    }
    await assert.rejects(buildCdn({assetBase:'https://iphone.files.garden/',out:join(temp,'a')}),/already exists/);
  } finally { await rm(temp,{recursive:true,force:true}); }
});

test('refuses generated output inside the checked-in web bundle', async () => {
  await assert.rejects(buildCdn({assetBase:'https://iphone.files.garden/',
    out:new URL('../../web/generated-cdn-test',import.meta.url).pathname}),/outside the source/);
});
