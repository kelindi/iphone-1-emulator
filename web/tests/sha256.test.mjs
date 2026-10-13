import test from 'node:test';
import assert from 'node:assert/strict';
import {createHash, randomBytes, webcrypto} from 'node:crypto';
import {sha256} from '../sha256.js';

test('HTTP fallback matches SHA-256 across padding boundaries and large input', async t => {
  t.mock.getter(globalThis, 'crypto', () => ({}));
  for (const size of [0, 3, 55, 56, 63, 64, 65, 127, 128, 1000000]) {
    const bytes = size === 3 ? Buffer.from('abc') : randomBytes(size);
    const result = await sha256(bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength));
    assert.equal(Buffer.from(result).toString('hex'), createHash('sha256').update(bytes).digest('hex'));
  }
});

test('secure contexts use native Web Crypto', async t => {
  let calls = 0;
  t.mock.getter(globalThis, 'crypto', () => ({subtle:{digest(...args) {
    calls++; return webcrypto.subtle.digest(...args);
  }}}));
  assert.equal(Buffer.from(await sha256(new ArrayBuffer(0))).toString('hex'),
    createHash('sha256').update('').digest('hex'));
  assert.equal(calls, 1);
});
