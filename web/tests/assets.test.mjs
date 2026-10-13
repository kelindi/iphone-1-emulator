import test from 'node:test';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {gzipSync} from 'node:zlib';
import {assetBytes} from '../assets.js';

const origin = 'https://phone.example';
const digest = value => createHash('sha256').update(value).digest('hex');
function environment(t, network, stored = new Map()) {
  const requests = [];
  t.mock.method(globalThis, 'fetch', async url => {
    requests.push(String(url));
    const bytes = network.get(new URL(url).pathname);
    return bytes ? new Response(bytes) : new Response('', {status:404});
  });
  const oldLocation = Object.getOwnPropertyDescriptor(globalThis, 'location');
  const oldCaches = Object.getOwnPropertyDescriptor(globalThis, 'caches');
  Object.defineProperty(globalThis, 'location', {configurable:true, value:{href:origin + '/phone/emu-worker.js', origin}});
  Object.defineProperty(globalThis, 'caches', {configurable:true, value:{async open() { return {
    async match(url) { return stored.has(url) ? new Response(stored.get(url)) : undefined; },
    async put(url, response) { stored.set(url, new Uint8Array(await response.arrayBuffer())); },
    async delete(url) { return stored.delete(url); }
  }; }}});
  t.after(() => {
    if (oldLocation) Object.defineProperty(globalThis, 'location', oldLocation); else delete globalThis.location;
    if (oldCaches) Object.defineProperty(globalThis, 'caches', oldCaches); else delete globalThis.caches;
  });
  return {requests, stored};
}

test('corrupt cached firmware is discarded and verified network bytes replace it', async t => {
  const good = Buffer.from('genuine firmware test bytes'), name = 'firmware/kernel';
  const url = origin + '/phone/' + name;
  const {requests, stored} = environment(t, new Map([['/phone/' + name, good]]), new Map([[url, Buffer.from('corrupt')]]));
  assert.deepEqual(Buffer.from(await assetBytes(name, undefined, {[name]:digest(good)})), good);
  assert.deepEqual(requests, [url]);
  assert.deepEqual(Buffer.from(stored.get(url)), good);
  await assetBytes(name, undefined, {[name]:digest(good)});
  assert.equal(requests.length, 1, 'verified cache is reusable without another download');
});

test('corrupt network firmware is never cached or returned', async t => {
  const name = 'firmware/kernel';
  const {stored} = environment(t, new Map([['/phone/' + name, Buffer.from('corrupt')]]));
  await assert.rejects(assetBytes(name, undefined, {[name]:digest('expected')}), /integrity check failed/);
  assert.equal(stored.size, 0);
});

test('mobile HTTP without Web Crypto still verifies firmware and rejects corruption', async t => {
  t.mock.getter(globalThis, 'crypto', () => ({}));
  const name = 'firmware/kernel', good = Buffer.from('verified over HTTP');
  const {stored} = environment(t, new Map([['/phone/' + name, good]]));
  assert.deepEqual(Buffer.from(await assetBytes(name, undefined, {[name]:digest(good)})), good);
  await assert.rejects(assetBytes(name, undefined, {[name]:digest('different bytes')}), /integrity check failed/);
  assert.equal(stored.size, 0);
});

test('multipart gzip checks each part before reconstructing the original bytes', async t => {
  const raw = Buffer.from('original phone snapshot'.repeat(500)), packed = gzipSync(raw);
  const names = ['firmware/home.gz.part0','firmware/home.gz.part1'];
  const parts = [packed.subarray(0,13), packed.subarray(13)];
  environment(t, new Map(names.map((name,i) => ['/phone/' + name, parts[i]])));
  const hashes = Object.fromEntries(names.map((name,i) => [name,digest(parts[i])]));
  assert.deepEqual(Buffer.from(await assetBytes(names, undefined, hashes)), raw);
});

test('cross-origin firmware is rejected before fetching', async t => {
  const {requests} = environment(t, new Map());
  await assert.rejects(assetBytes('https://unrelated.example/firmware'), /approved host/);
  assert.equal(requests.length, 0);
});

test('configured R2 firmware is accepted and integrity checked', async t => {
  const name = 'https://iphone.files.garden/kernel';
  const good = Buffer.from('verified R2 firmware');
  const {requests} = environment(t, new Map([['/kernel', good]]));
  assert.deepEqual(Buffer.from(await assetBytes(name, undefined, {[name]:digest(good)})), good);
  assert.deepEqual(requests, [name]);
});
